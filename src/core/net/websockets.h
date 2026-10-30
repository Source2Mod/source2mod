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
#pragma once
#include "core/appsystem/appsystem.h"

#include "core/hooks/hookid.h"
#include "core/net/net.h"

#include <source2mod/ISource2ModWebSockets.h>

#include <memory>
#include <string>
#include <vector>

// ISource2ModWebSockets on net::WebSocket (net_websocket.cpp): the sockets
// plugins hold, each owned by the plugin its listener's code lies in.
namespace websockets {
    class Socket;

    class WebSocketsManager final : public ISource2ModWebSockets, public appsystem::ISource2ModAppSystem {
    public:
        appsystem::SystemId GetSystemId() const override { return appsystem::SystemId::WebSockets; }

        ISource2ModWebSocket* Connect(const char* url, ISource2ModWebSocketListener* listener,
                                      const Source2ModWebSocketOptions& options) override
        {
            // Whose class the listener is: its vtable lies in that plugin's library.
            const PluginId owner = hookid::OwnerOf(listener ? *reinterpret_cast<void* const*>(listener) : nullptr);
            return Connect(owner, url, listener, options);
        }

        /// The same with the owner spelled out: what the call above resolves
        /// to, and what the script natives call.
        ISource2ModWebSocket* Connect(PluginId owner, const char* url, ISource2ModWebSocketListener* listener,
                                      const Source2ModWebSocketOptions& options);

        /// ISource2ModWebSocket::Destroy. Only compares the pointer: a socket
        /// RemoveAllForPlugin already took is simply not found.
        void Destroy(const ISource2ModWebSocket* socket);

        /// Destroys this plugin's sockets while its library is still mapped:
        /// a listener call into an unloaded plugin would land in nothing.
        void RemoveAllForPlugin(PluginId id) override;

        /// Destroys every socket. On unload, before net::Shutdown().
        void Shutdown() override;

    private:
        std::vector<std::shared_ptr<Socket>> m_sockets;
    };

    extern WebSocketsManager webSocketsManager;
}
