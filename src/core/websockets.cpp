/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "websockets.h"

#include "slowguard.h"
#include "utils/log.h"

#include <algorithm>

namespace websockets
{
    WebSocketsManager webSocketsManager;

    class Socket final : public ISource2ModWebSocket, public net::WebSocketHandler, public std::enable_shared_from_this<Socket>
    {
    public:
        Socket(PluginId owner, std::string url, ISource2ModWebSocketListener* listener)
            : m_owner(owner), m_url(std::move(url)), m_pListener(listener)
        {
        }

        // ISource2ModWebSocket

        bool Send(const char* text) override
        {
            return m_pSocket && text && m_pSocket->Send(text, false);
        }

        bool SendBinary(const void* data, size_t length) override
        {
            return m_pSocket && (data || !length) &&
                   m_pSocket->Send(std::string(static_cast<const char*>(data), data ? length : 0), true);
        }

        void Close(int code, const char* reason) override
        {
            if (m_pSocket)
                m_pSocket->Close(code, reason ? reason : "");
        }

        ESource2ModWebSocketState GetState() const override
        {
            if (!m_pSocket)
                return ESource2ModWebSocketState::Closed;
            switch (m_pSocket->State())
            {
                case net::WebSocketState::Open:   return ESource2ModWebSocketState::Open;
                case net::WebSocketState::Closed: return ESource2ModWebSocketState::Closed;
                default:                          return ESource2ModWebSocketState::Connecting;
            }
        }

        const char* GetUrl() const override { return m_url.c_str(); }

        void Destroy() override { webSocketsManager.Destroy(this); }

        // net::WebSocketHandler -- each call holds a reference, so a listener
        // that destroys the socket from inside one is fine.

        void OnWsOpen(bool reconnected) override
        {
            Call("WebSocket open", [&](ISource2ModWebSocketListener* l) { l->OnWebSocketOpen(this, reconnected); });
        }

        void OnWsMessage(std::string data, bool binary) override
        {
            Call("WebSocket message", [&](ISource2ModWebSocketListener* l) { l->OnWebSocketMessage(this, data.c_str(), data.size(), binary); });
        }

        void OnWsClose(int code, std::string reason) override
        {
            Call("WebSocket close", [&](ISource2ModWebSocketListener* l) { l->OnWebSocketClose(this, code, reason.c_str()); });
        }

        void OnWsError(std::string error) override
        {
            FP_DEBUG("WebSocket {}: {}", m_url, error);
            Call("WebSocket error", [&](ISource2ModWebSocketListener* l) { l->OnWebSocketError(this, error.c_str()); });
        }

    private:
        friend class WebSocketsManager;

        /// No more listener calls, and the connection abandoned.
        void Detach()
        {
            m_pListener = nullptr;
            m_pSocket.reset();
        }

        template <class F>
        void Call(const char* what, F fn)
        {
            if (!m_pListener)
                return;
            auto self = shared_from_this();
            ::slow::Guard slowGuard(what, m_url.c_str(), m_owner);
            fn(m_pListener);
        }

        PluginId m_owner;
        std::string m_url;
        ISource2ModWebSocketListener* m_pListener;
        std::unique_ptr<net::WebSocket> m_pSocket;
    };

    ISource2ModWebSocket* WebSocketsManager::Connect(PluginId owner, const char* url, ISource2ModWebSocketListener* listener,
                                                     const Source2ModWebSocketOptions& options)
    {
        if (!url || !listener)
            return nullptr;

        net::WebSocketConfig config;
        config.url = url;
        for (const auto& header : options.m_Headers)
            config.headers.emplace_back(header.m_sName, header.m_sValue);
        config.autoReconnect = options.m_bAutoReconnect;
        config.pingIntervalSec = options.m_nPingIntervalSec;
        config.verifyPeer = options.m_bVerifyPeer;
        config.maxMessageSize = options.m_nMaxMessageSize;

        auto socket = std::make_shared<Socket>(owner, url, listener);
        socket->m_pSocket = net::WebSocket::Connect(std::move(config), socket);
        if (!socket->m_pSocket)
        {
            FP_WARN("WebSocket: '{}' is not a ws:// or wss:// URL", url);
            return nullptr;
        }

        m_sockets.push_back(socket);
        return socket.get();
    }

    void WebSocketsManager::Destroy(const ISource2ModWebSocket* socket)
    {
        auto it = std::find_if(m_sockets.begin(), m_sockets.end(),
                               [socket](const auto& s) { return static_cast<const ISource2ModWebSocket*>(s.get()) == socket; });
        if (it == m_sockets.end())
            return;
        // Moved out first: a listener call on the stack keeps its own reference.
        std::shared_ptr<Socket> going = std::move(*it);
        m_sockets.erase(it);
        going->Detach();
    }

    void WebSocketsManager::RemoveAllForPlugin(PluginId id)
    {
        std::vector<std::shared_ptr<Socket>> going;
        for (auto it = m_sockets.begin(); it != m_sockets.end();)
        {
            if ((*it)->m_owner == id)
            {
                going.push_back(std::move(*it));
                it = m_sockets.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto& socket : going)
            socket->Detach();
    }

    void WebSocketsManager::Shutdown()
    {
        auto going = std::move(m_sockets);
        m_sockets.clear();
        for (auto& socket : going)
            socket->Detach();
    }
}
