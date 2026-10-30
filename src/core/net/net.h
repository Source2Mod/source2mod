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

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The core's own sockets: what Steam cannot do (WebSockets) and what it cannot
// do right now (HTTP before the Steam API is up, or while it fails). Boost.Asio
// and Beast stay behind net_impl.h, included only by the net*.cpp files -- this
// header is what the rest of the core sees, and nothing of it reaches the SDK.
namespace net {
    /// A parsed http://, https://, ws:// or wss:// URL.
    struct Url {
        bool tls = false;
        bool websocket = false;
        std::string host;
        uint16_t port = 0;
        /// Path and query, "/" at the least.
        std::string target;
    };

    /// nullopt for anything that is not one of the four schemes, has no host,
    /// or a port outside 1-65535. The port defaults to 80 or 443.
    std::optional<Url> ParseUrl(std::string_view url);

    /// One HTTP(S) request for Beast: what HTTP falls back to when Steam's
    /// client cannot take it.
    struct HttpRequest {
        /// "GET", "POST", ...
        std::string method = "GET";
        std::string url;
        std::vector<std::pair<std::string, std::string>> headers;
        bool hasBody = false;
        std::string body;
        std::string contentType = "application/json";
        /// For every step on its own: resolve, connect, handshake, write, read.
        int timeoutSec = 30;
        bool verifyPeer = true;
    };

    /// What came back: success is about the transport, as in
    /// Source2ModHTTPResponse -- a 404 that arrived is a success.
    struct HttpResult {
        bool success = false;
        int status = 0;
        std::string body;
        /// Why success is false, for the log.
        std::string error;
    };

    /// Sends @p request on the IO thread and hands @p done the result on the
    /// main thread (see PostToMain for what @p done should hold). Follows up
    /// to five redirects, as Steam's client does; a 303 turns into a GET.
    void SendHttp(HttpRequest request, std::function<void(HttpResult)> done);

    /// What a WebSocket reports, always on the main thread.
    class WebSocketHandler {
    public:
        virtual ~WebSocketHandler() = default;
        /// The handshake went through; @p reconnected for every one after the first.
        virtual void OnWsOpen(bool reconnected) = 0;
        virtual void OnWsMessage(std::string data, bool binary) = 0;
        /// The connection is gone: closed by either side (@p code is the close
        /// code, 1006 when there was none), or lost. A reconnect follows unless
        /// it was closed from here or reconnecting is off.
        virtual void OnWsClose(int code, std::string reason) = 0;
        /// Why connecting or the connection failed, for the log. OnWsClose
        /// follows for a connection that was open.
        virtual void OnWsError(std::string error) = 0;
    };

    struct WebSocketConfig {
        std::string url;
        std::vector<std::pair<std::string, std::string>> headers;
        bool autoReconnect = true;
        /// Seconds without traffic before a ping goes out; 0 for none.
        int pingIntervalSec = 30;
        bool verifyPeer = true;
        size_t maxMessageSize = 16u << 20;
    };

    enum class WebSocketState { Connecting, Open, Closed };

    /// A WebSocket client. Owned on the main thread; the connection itself
    /// lives on the IO thread for as long as it runs. Reconnects with a growing
    /// delay (1 s doubling up to 30 s) after a connection is lost.
    class WebSocket {
    public:
        /// nullptr for a URL that is not ws:// or wss://.
        static std::unique_ptr<WebSocket> Connect(WebSocketConfig config, std::weak_ptr<WebSocketHandler> handler);

        /// Abandons the connection: closed, never reconnected, and the
        /// handler hears nothing more.
        ~WebSocket();

        /// Queued for sending, in order. False while not open.
        bool Send(std::string data, bool binary);

        /// A close handshake with @p code; no reconnect after it. OnWsClose
        /// reports when it is done.
        void Close(int code, std::string reason);

        WebSocketState State() const;

        class Impl;

    private:
        explicit WebSocket(std::shared_ptr<Impl> impl);
        std::shared_ptr<Impl> m_pImpl;
    };

    /// Runs @p fn on the main thread on the next RunFrame. Callable from any
    /// thread: the IO thread hands its results over this way, so a plugin's
    /// callback never runs anywhere but the main thread.
    ///
    /// @p fn should hold the core's own object (the request, the socket), not
    /// a plugin's callback: Shutdown() may destroy it after the plugins are
    /// unmapped, and the object is what knows whether its owner is still there.
    void PostToMain(std::function<void()> fn);

    /// Runs what PostToMain queued. Main thread, every server frame.
    void RunFrame();

    /// Whether the IO thread was ever started. It is not until the first
    /// socket is wanted: a server whose plugins never leave Steam's HTTP has
    /// no extra thread.
    bool IsRunning();

    /// Stops the IO thread and joins it, then drops whatever is still queued
    /// for the main thread. On unload, after everything that owns a socket.
    void Shutdown();
}
