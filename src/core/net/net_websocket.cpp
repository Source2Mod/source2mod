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
#include "core/net/net_impl.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <type_traits>

// Beast's WebSocket client. Steam has none, so unlike HTTP there is nothing
// to fall back from: every WebSocket is this. All of a connection's state is
// touched on its strand only; the main thread posts to it and hears back
// through PostToMain.

namespace net
{
    class WebSocket::Impl
    {
    public:
        virtual ~Impl() = default;
        virtual void Start() = 0;
        virtual void Send(std::string data, bool binary) = 0;
        virtual void Close(int code, std::string reason) = 0;
        virtual void Abandon() = 0;

        std::atomic<WebSocketState> m_state{ WebSocketState::Connecting };
    };

    namespace
    {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = boost::beast::websocket;
        namespace ssl = boost::asio::ssl;
        using tcp = boost::asio::ip::tcp;

        constexpr int kConnectTimeoutSec = 30;
        constexpr int kMaxRetryDelaySec = 30;

        template <bool TLS>
        class Session final : public WebSocket::Impl, public std::enable_shared_from_this<Session<TLS>>
        {
            using Next = std::conditional_t<TLS, beast::ssl_stream<beast::tcp_stream>, beast::tcp_stream>;
            using Stream = websocket::stream<Next>;

        public:
            Session(asio::any_io_executor ex, Url url, WebSocketConfig config, std::weak_ptr<WebSocketHandler> handler)
                : m_ex(ex), m_resolver(ex), m_retryTimer(ex), m_url(std::move(url)), m_config(std::move(config)),
                  m_handler(std::move(handler))
            {
            }

            void Start() override
            {
                asio::post(m_ex, [self = this->shared_from_this()] { self->Attempt(); });
            }

            void Send(std::string data, bool binary) override
            {
                asio::post(m_ex, [self = this->shared_from_this(), data = std::move(data), binary]() mutable
                {
                    if (!self->m_bOpen)
                        return;
                    self->m_outbox.emplace_back(std::move(data), binary);
                    if (!self->m_bWriting)
                        self->WriteNext();
                });
            }

            void Close(int code, std::string reason) override
            {
                asio::post(m_ex, [self = this->shared_from_this(), code, reason = std::move(reason)]() mutable
                {
                    self->CloseOnStrand(code, std::move(reason), false);
                });
            }

            void Abandon() override
            {
                asio::post(m_ex, [self = this->shared_from_this()] { self->CloseOnStrand(1001, "", true); });
            }

        private:
            /// Hands @p fn to the handler on the main thread, unless the owner
            /// gave up on this connection or the handler is gone by then.
            template <class F>
            void Emit(F fn)
            {
                if (m_bAbandoned)
                    return;
                PostToMain([handler = m_handler, fn = std::move(fn)]() mutable
                {
                    if (auto p = handler.lock())
                        fn(*p);
                });
            }

            /// Every attempt gets a generation: a handler of an attempt that
            /// was given up on finds a newer one and does nothing.
            template <class F>
            auto Bind(F fn)
            {
                return [self = this->shared_from_this(), gen = m_nGeneration, fn](auto&&... args) mutable
                {
                    if (gen == self->m_nGeneration)
                        (self.get()->*fn)(std::forward<decltype(args)>(args)...);
                };
            }

            void Attempt()
            {
                if (m_bUserClosed)
                    return;

                m_state = WebSocketState::Connecting;
                m_nGeneration++;
                if constexpr (TLS)
                    m_pWs = std::make_unique<Stream>(m_ex, ClientTlsContext(m_config.verifyPeer));
                else
                    m_pWs = std::make_unique<Stream>(m_ex);
                m_buffer.clear();

                m_resolver.async_resolve(m_url.host, std::to_string(m_url.port), Bind(&Session::OnResolve));
            }

            void OnResolve(beast::error_code ec, tcp::resolver::results_type results)
            {
                if (ec)
                    return Fail("resolve", ec);
                beast::get_lowest_layer(*m_pWs).expires_after(std::chrono::seconds(kConnectTimeoutSec));
                beast::get_lowest_layer(*m_pWs).async_connect(results, Bind(&Session::OnConnect));
            }

            void OnConnect(beast::error_code ec, tcp::endpoint)
            {
                if (ec)
                    return Fail("connect", ec);

                if constexpr (TLS)
                {
                    auto& tls = m_pWs->next_layer();
                    if (!SSL_set_tlsext_host_name(tls.native_handle(), m_url.host.c_str()))
                        return Fail("SNI", beast::error_code(static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()));
                    if (m_config.verifyPeer)
                        tls.set_verify_callback(ssl::host_name_verification(m_url.host));
                    tls.async_handshake(ssl::stream_base::client, Bind(&Session::OnTlsHandshake));
                }
                else
                {
                    Handshake();
                }
            }

            void OnTlsHandshake(beast::error_code ec)
            {
                if (ec)
                    return Fail("TLS handshake", ec);
                Handshake();
            }

            void Handshake()
            {
                // From here on the WebSocket's own timeouts and pings.
                beast::get_lowest_layer(*m_pWs).expires_never();

                websocket::stream_base::timeout timeout;
                timeout.handshake_timeout = std::chrono::seconds(kConnectTimeoutSec);
                if (m_config.pingIntervalSec > 0)
                {
                    // Beast pings at half the idle timeout and gives up at the
                    // full one.
                    timeout.idle_timeout = std::chrono::seconds(m_config.pingIntervalSec * 2);
                    timeout.keep_alive_pings = true;
                }
                else
                {
                    timeout.idle_timeout = websocket::stream_base::none();
                    timeout.keep_alive_pings = false;
                }
                m_pWs->set_option(timeout);
                m_pWs->set_option(websocket::stream_base::decorator([headers = m_config.headers](websocket::request_type& req)
                {
                    req.set(beast::http::field::user_agent, "Source2Mod");
                    for (const auto& [name, value] : headers)
                        req.set(name, value);
                }));
                m_pWs->read_message_max(m_config.maxMessageSize);

                std::string host = m_url.host.find(':') != std::string::npos ? "[" + m_url.host + "]" : m_url.host;
                if (m_url.port != (m_url.tls ? 443 : 80))
                    host += ":" + std::to_string(m_url.port);

                m_pWs->async_handshake(host, m_url.target, Bind(&Session::OnHandshake));
            }

            void OnHandshake(beast::error_code ec)
            {
                if (ec)
                    return Fail("WebSocket handshake", ec);

                m_bOpen = true;
                m_state = WebSocketState::Open;
                m_nRetryDelaySec = 1;
                const bool reconnected = m_bEverOpened;
                m_bEverOpened = true;
                Emit([reconnected](WebSocketHandler& h) { h.OnWsOpen(reconnected); });
                Read();
            }

            void Read()
            {
                m_pWs->async_read(m_buffer, Bind(&Session::OnRead));
            }

            void OnRead(beast::error_code ec, size_t)
            {
                if (ec)
                {
                    if (ec == websocket::error::closed)
                        return ConnectionGone(m_pWs->reason().code, std::string(m_pWs->reason().reason.c_str()), {});
                    // Closed from here: the read ends with whatever the close
                    // left it, not error::closed. The code is the one we sent.
                    if (m_bUserClosed)
                        return ConnectionGone(m_nCloseCode, m_sCloseReason, {});
                    return ConnectionGone(1006, {}, "read: " + ec.message());
                }

                std::string data = beast::buffers_to_string(m_buffer.cdata());
                m_buffer.consume(m_buffer.size());
                const bool binary = !m_pWs->got_text();
                Emit([data = std::move(data), binary](WebSocketHandler& h) mutable { h.OnWsMessage(std::move(data), binary); });
                Read();
            }

            void WriteNext()
            {
                m_bWriting = true;
                m_pWs->binary(m_outbox.front().second);
                m_pWs->async_write(asio::buffer(m_outbox.front().first), Bind(&Session::OnWrite));
            }

            void OnWrite(beast::error_code ec, size_t)
            {
                if (ec)
                {
                    // The read that is always pending reports the connection.
                    m_bWriting = false;
                    return;
                }
                m_outbox.pop_front();
                if (m_outbox.empty())
                    m_bWriting = false;
                else
                    WriteNext();
            }

            /// An open connection ended: closed by either side, or lost.
            void ConnectionGone(int code, std::string reason, std::string error)
            {
                m_bOpen = false;
                m_bWriting = false;
                m_outbox.clear();
                if (!error.empty() && !m_bUserClosed)
                    Emit([error](WebSocketHandler& h) { h.OnWsError(error); });
                Emit([code, reason = std::move(reason)](WebSocketHandler& h) mutable { h.OnWsClose(code, std::move(reason)); });
                ScheduleReconnect();
            }

            /// An attempt failed before the connection was open.
            void Fail(const char* what, beast::error_code ec)
            {
                if (m_bUserClosed)
                    return;
                Emit([error = std::string(what) + ": " + ec.message()](WebSocketHandler& h) { h.OnWsError(error); });
                ScheduleReconnect();
            }

            void ScheduleReconnect()
            {
                if (m_bUserClosed || !m_config.autoReconnect)
                {
                    m_state = WebSocketState::Closed;
                    return;
                }

                m_state = WebSocketState::Connecting;
                m_retryTimer.expires_after(std::chrono::seconds(m_nRetryDelaySec));
                m_nRetryDelaySec = std::min(m_nRetryDelaySec * 2, kMaxRetryDelaySec);
                m_retryTimer.async_wait([self = this->shared_from_this()](beast::error_code ec)
                {
                    if (!ec && !self->m_bUserClosed)
                        self->Attempt();
                });
            }

            void CloseOnStrand(int code, std::string reason, bool abandon)
            {
                if (abandon)
                    m_bAbandoned = true;
                if (m_bUserClosed)
                    return;
                m_bUserClosed = true;
                m_nCloseCode = code;
                m_sCloseReason = reason;
                m_retryTimer.cancel();

                if (m_bOpen)
                {
                    // The pending read ends with error::closed once the peer
                    // answers, and reports the close from there.
                    websocket::close_reason why(static_cast<websocket::close_code>(code));
                    why.reason = reason;
                    m_pWs->async_close(why,
                                       [self = this->shared_from_this()](beast::error_code ec)
                    {
                        if (ec && self->m_pWs)
                            beast::get_lowest_layer(*self->m_pWs).close();
                    });
                    return;
                }

                // Connecting, or waiting to reconnect: nothing to shake hands with.
                m_nGeneration++;
                if (m_pWs)
                    beast::get_lowest_layer(*m_pWs).close();
                m_state = WebSocketState::Closed;
                Emit([code, reason = std::move(reason)](WebSocketHandler& h) mutable { h.OnWsClose(code, std::move(reason)); });
            }

            asio::any_io_executor m_ex;
            tcp::resolver m_resolver;
            asio::steady_timer m_retryTimer;
            Url m_url;
            WebSocketConfig m_config;
            std::weak_ptr<WebSocketHandler> m_handler;

            std::unique_ptr<Stream> m_pWs;
            beast::flat_buffer m_buffer;
            std::deque<std::pair<std::string, bool>> m_outbox;
            uint64_t m_nGeneration = 0;
            int m_nRetryDelaySec = 1;
            bool m_bOpen = false;
            bool m_bWriting = false;
            bool m_bEverOpened = false;
            bool m_bUserClosed = false;
            bool m_bAbandoned = false;
            int m_nCloseCode = 1000;
            std::string m_sCloseReason;
        };
    }

    std::unique_ptr<WebSocket> WebSocket::Connect(WebSocketConfig config, std::weak_ptr<WebSocketHandler> handler)
    {
        std::optional<Url> url = ParseUrl(config.url);
        if (!url || !url->websocket)
            return nullptr;

        asio::any_io_executor ex = asio::make_strand(IoContext());
        std::shared_ptr<Impl> impl;
        if (url->tls)
            impl = std::make_shared<Session<true>>(ex, std::move(*url), std::move(config), std::move(handler));
        else
            impl = std::make_shared<Session<false>>(ex, std::move(*url), std::move(config), std::move(handler));
        impl->Start();
        return std::unique_ptr<WebSocket>(new WebSocket(std::move(impl)));
    }

    WebSocket::WebSocket(std::shared_ptr<Impl> impl) : m_pImpl(std::move(impl)) {}

    WebSocket::~WebSocket()
    {
        m_pImpl->Abandon();
    }

    bool WebSocket::Send(std::string data, bool binary)
    {
        if (m_pImpl->m_state != WebSocketState::Open)
            return false;
        m_pImpl->Send(std::move(data), binary);
        return true;
    }

    void WebSocket::Close(int code, std::string reason)
    {
        m_pImpl->Close(code, std::move(reason));
    }

    WebSocketState WebSocket::State() const
    {
        return m_pImpl->m_state;
    }
}
