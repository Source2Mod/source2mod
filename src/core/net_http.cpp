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
#include "net_impl.h"

#include <chrono>
#include <memory>

// Beast's HTTP client: the fallback behind Steam's (http.cpp decides when).
// One connection per request, closed once the response is read.

namespace net
{
    namespace
    {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace bhttp = boost::beast::http;
        namespace ssl = boost::asio::ssl;
        using tcp = boost::asio::ip::tcp;

        constexpr int kMaxRedirects = 5;
        constexpr uint64_t kBodyLimit = 64ull << 20;

        void Start(HttpRequest request, std::function<void(HttpResult)> done, int redirects);

        void Finish(std::function<void(HttpResult)> done, HttpResult result)
        {
            PostToMain([done = std::move(done), result = std::move(result)]() mutable { done(std::move(result)); });
        }

        bool IsRedirect(int status)
        {
            return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        }

        /// @p location against the URL it came from: absolute, host-relative
        /// ("/path") or relative to the current path.
        std::string ResolveLocation(const Url& from, std::string_view location)
        {
            if (location.find("://") != std::string_view::npos)
                return std::string(location);

            std::string base = from.tls ? "https://" : "http://";
            if (from.host.find(':') != std::string::npos)
                base += "[" + from.host + "]";
            else
                base += from.host;
            if (from.port != (from.tls ? 443 : 80))
                base += ":" + std::to_string(from.port);

            if (!location.empty() && location.front() == '/')
                return base + std::string(location);

            std::string dir = from.target.substr(0, from.target.find('?'));
            dir = dir.substr(0, dir.rfind('/') + 1);
            return base + dir + std::string(location);
        }

        template <class Stream>
        class Session : public std::enable_shared_from_this<Session<Stream>>
        {
            static constexpr bool kTls = !std::is_same_v<Stream, beast::tcp_stream>;

        public:
            Session(asio::any_io_executor ex, Url url, HttpRequest request,
                    std::function<void(HttpResult)> done, int redirects)
                : m_resolver(ex), m_stream(MakeStream(ex, request.verifyPeer)), m_url(std::move(url)),
                  m_request(std::move(request)), m_done(std::move(done)), m_redirects(redirects)
            {
            }

            void Run()
            {
                const bhttp::verb verb = bhttp::string_to_verb(m_request.method);
                if (verb == bhttp::verb::unknown)
                    return Fail("unknown method " + m_request.method);

                m_req.method(verb);
                m_req.target(m_url.target);
                m_req.version(11);

                std::string host = m_url.host.find(':') != std::string::npos ? "[" + m_url.host + "]" : m_url.host;
                if (m_url.port != (m_url.tls ? 443 : 80))
                    host += ":" + std::to_string(m_url.port);
                m_req.set(bhttp::field::host, host);
                m_req.set(bhttp::field::user_agent, "Source2Mod");

                bool bHasContentType = false;
                for (const auto& [name, value] : m_request.headers)
                {
                    m_req.insert(name, value);
                    if (beast::iequals(name, "Content-Type"))
                        bHasContentType = true;
                }
                if (m_request.hasBody)
                {
                    if (!bHasContentType)
                        m_req.set(bhttp::field::content_type, m_request.contentType);
                    m_req.body() = m_request.body;
                }
                m_req.prepare_payload();

                m_parser.body_limit(kBodyLimit);
                if (verb == bhttp::verb::head)
                    m_parser.skip(true);

                m_resolver.async_resolve(m_url.host, std::to_string(m_url.port),
                                         beast::bind_front_handler(&Session::OnResolve, this->shared_from_this()));
            }

        private:
            static Stream MakeStream(asio::any_io_executor ex, bool verifyPeer)
            {
                if constexpr (kTls)
                    return Stream(ex, ClientTlsContext(verifyPeer));
                else
                    return Stream(ex);
            }

            void Expire() { beast::get_lowest_layer(m_stream).expires_after(std::chrono::seconds(m_request.timeoutSec)); }

            void OnResolve(beast::error_code ec, tcp::resolver::results_type results)
            {
                if (ec)
                    return Fail("resolve", ec);
                Expire();
                beast::get_lowest_layer(m_stream).async_connect(
                    results, beast::bind_front_handler(&Session::OnConnect, this->shared_from_this()));
            }

            void OnConnect(beast::error_code ec, tcp::endpoint)
            {
                if (ec)
                    return Fail("connect", ec);

                if constexpr (kTls)
                {
                    // SNI, and the name the certificate has to carry.
                    if (!SSL_set_tlsext_host_name(m_stream.native_handle(), m_url.host.c_str()))
                        return Fail("SNI", beast::error_code(static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()));
                    if (m_request.verifyPeer)
                        m_stream.set_verify_callback(ssl::host_name_verification(m_url.host));

                    Expire();
                    m_stream.async_handshake(ssl::stream_base::client,
                                             beast::bind_front_handler(&Session::OnHandshake, this->shared_from_this()));
                }
                else
                {
                    Write();
                }
            }

            void OnHandshake(beast::error_code ec)
            {
                if (ec)
                    return Fail("TLS handshake", ec);
                Write();
            }

            void Write()
            {
                Expire();
                bhttp::async_write(m_stream, m_req, beast::bind_front_handler(&Session::OnWrite, this->shared_from_this()));
            }

            void OnWrite(beast::error_code ec, size_t)
            {
                if (ec)
                    return Fail("write", ec);
                Expire();
                bhttp::async_read(m_stream, m_buffer, m_parser, beast::bind_front_handler(&Session::OnRead, this->shared_from_this()));
            }

            void OnRead(beast::error_code ec, size_t)
            {
                if (ec)
                    return Fail("read", ec);

                // The whole response is in: no close_notify dance, the
                // connection just goes.
                beast::error_code ignored;
                beast::get_lowest_layer(m_stream).socket().shutdown(tcp::socket::shutdown_both, ignored);
                beast::get_lowest_layer(m_stream).close();

                auto& res = m_parser.get();
                const int status = static_cast<int>(res.result_int());

                if (IsRedirect(status) && res.count(bhttp::field::location) && m_redirects < kMaxRedirects)
                {
                    HttpRequest next = std::move(m_request);
                    next.url = ResolveLocation(m_url, res[bhttp::field::location]);
                    // What curl does: 303 always, and 301/302 after a POST, go on as a GET.
                    if (status == 303 || ((status == 301 || status == 302) && next.method == "POST"))
                    {
                        next.method = "GET";
                        next.hasBody = false;
                        next.body.clear();
                    }
                    return Start(std::move(next), std::move(m_done), m_redirects + 1);
                }

                HttpResult result;
                result.success = true;
                result.status = status;
                result.body = std::move(res.body());
                Finish(std::move(m_done), std::move(result));
            }

            void Fail(const char* what, beast::error_code ec) { Fail(std::string(what) + ": " + ec.message()); }

            void Fail(std::string error)
            {
                HttpResult result;
                result.error = std::move(error);
                Finish(std::move(m_done), std::move(result));
            }

            tcp::resolver m_resolver;
            Stream m_stream;
            Url m_url;
            HttpRequest m_request;
            std::function<void(HttpResult)> m_done;
            int m_redirects;

            beast::flat_buffer m_buffer;
            bhttp::request<bhttp::string_body> m_req;
            bhttp::response_parser<bhttp::string_body> m_parser;
        };

        void Start(HttpRequest request, std::function<void(HttpResult)> done, int redirects)
        {
            std::optional<Url> url = ParseUrl(request.url);
            if (!url || url->websocket)
            {
                HttpResult result;
                result.error = "not an http(s) URL: " + request.url;
                return Finish(std::move(done), std::move(result));
            }

            asio::any_io_executor ex = asio::make_strand(IoContext());
            const bool tls = url->tls;
            if (tls)
            {
                auto session = std::make_shared<Session<beast::ssl_stream<beast::tcp_stream>>>(
                    ex, std::move(*url), std::move(request), std::move(done), redirects);
                asio::post(ex, [session] { session->Run(); });
            }
            else
            {
                auto session = std::make_shared<Session<beast::tcp_stream>>(
                    ex, std::move(*url), std::move(request), std::move(done), redirects);
                asio::post(ex, [session] { session->Run(); });
            }
        }
    }

    void SendHttp(HttpRequest request, std::function<void(HttpResult)> done)
    {
        Start(std::move(request), std::move(done), 0);
    }
}
