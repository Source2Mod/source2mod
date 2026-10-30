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
#include "utils/log.h"

#include <cctype>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <wincrypt.h>
#endif

namespace net
{
    namespace
    {
        namespace asio = boost::asio;
        namespace ssl = boost::asio::ssl;

        std::mutex s_ioLock;
        std::unique_ptr<asio::io_context> s_pIo;
        std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> s_pWork;
        std::thread s_ioThread;

        std::mutex s_mainLock;
        std::vector<std::function<void()>> s_mainQueue;

        std::once_flag s_tlsVerifyOnce;
        std::once_flag s_tlsNoVerifyOnce;
        std::unique_ptr<ssl::context> s_pTlsVerify;
        std::unique_ptr<ssl::context> s_pTlsNoVerify;

        void IoThreadMain(asio::io_context* io)
        {
            // A handler that throws would otherwise take the thread, and with
            // it every socket, down for good.
            for (;;)
            {
                try
                {
                    io->run();
                    return;
                }
                catch (const std::exception& e)
                {
                    FP_ERROR("net: IO thread caught an exception: {}", e.what());
                }
            }
        }

        bool EqualsNoCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }

        /// The system's CA certificates into @p ctx. OpenSSL's own default
        /// paths point at wherever the vendored build was configured, not at
        /// the box's store, so the store is looked up here.
        void LoadSystemCAs(ssl::context& ctx)
        {
#ifdef _WIN32
            HCERTSTORE hStore = CertOpenSystemStoreW(0, L"ROOT");
            if (!hStore)
            {
                FP_WARN("net: no Windows ROOT certificate store, TLS peers cannot be verified.");
                return;
            }
            X509_STORE* pStore = SSL_CTX_get_cert_store(ctx.native_handle());
            int nLoaded = 0;
            for (PCCERT_CONTEXT pCert = nullptr; (pCert = CertEnumCertificatesInStore(hStore, pCert)) != nullptr;)
            {
                const unsigned char* pData = pCert->pbCertEncoded;
                if (X509* x509 = d2i_X509(nullptr, &pData, pCert->cbCertEncoded))
                {
                    if (X509_STORE_add_cert(pStore, x509) == 1)
                        nLoaded++;
                    X509_free(x509);
                }
            }
            CertCloseStore(hStore, 0);
            FP_DEBUG("net: {} CA certificate(s) from the Windows ROOT store.", nLoaded);
#else
            // Debian/Ubuntu, RHEL/Fedora, openSUSE, Alpine; the Steam runtime
            // containers are Debian-based.
            static constexpr const char* kBundles[] = {
                "/etc/ssl/certs/ca-certificates.crt",
                "/etc/pki/tls/certs/ca-bundle.crt",
                "/etc/ssl/ca-bundle.pem",
                "/etc/ssl/cert.pem",
            };
            for (const char* pszBundle : kBundles)
            {
                boost::system::error_code ec;
                ctx.load_verify_file(pszBundle, ec);
                if (!ec)
                {
                    FP_DEBUG("net: CA certificates from {}.", pszBundle);
                    return;
                }
            }
            boost::system::error_code ec;
            ctx.add_verify_path("/etc/ssl/certs", ec);
            if (ec)
                FP_WARN("net: no CA bundle found, TLS peers cannot be verified.");
#endif
        }

        std::unique_ptr<ssl::context> MakeClientContext(bool verifyPeer)
        {
            auto ctx = std::make_unique<ssl::context>(ssl::context::tls_client);
            SSL_CTX_set_min_proto_version(ctx->native_handle(), TLS1_2_VERSION);
            ctx->set_options(ssl::context::default_workarounds | ssl::context::no_compression);
            if (verifyPeer)
            {
                LoadSystemCAs(*ctx);
                ctx->set_verify_mode(ssl::verify_peer);
            }
            else
            {
                ctx->set_verify_mode(ssl::verify_none);
            }
            return ctx;
        }
    }

    std::optional<Url> ParseUrl(std::string_view url)
    {
        const size_t schemeEnd = url.find("://");
        if (schemeEnd == std::string_view::npos)
            return std::nullopt;

        Url out;
        const std::string_view scheme = url.substr(0, schemeEnd);
        if (EqualsNoCase(scheme, "http"))
            out = { false, false };
        else if (EqualsNoCase(scheme, "https"))
            out = { true, false };
        else if (EqualsNoCase(scheme, "ws"))
            out = { false, true };
        else if (EqualsNoCase(scheme, "wss"))
            out = { true, true };
        else
            return std::nullopt;

        std::string_view rest = url.substr(schemeEnd + 3);
        if (const size_t hash = rest.find('#'); hash != std::string_view::npos)
            rest = rest.substr(0, hash);

        const size_t authorityEnd = rest.find_first_of("/?");
        std::string_view authority = rest.substr(0, authorityEnd);
        std::string_view target = authorityEnd == std::string_view::npos ? std::string_view() : rest.substr(authorityEnd);

        // user:password@ is not sent anywhere; drop it.
        if (const size_t at = authority.rfind('@'); at != std::string_view::npos)
            authority = authority.substr(at + 1);

        std::string_view host = authority;
        std::string_view port;
        if (!authority.empty() && authority.front() == '[')
        {
            // [IPv6]:port
            const size_t close = authority.find(']');
            if (close == std::string_view::npos)
                return std::nullopt;
            host = authority.substr(1, close - 1);
            if (close + 1 < authority.size())
            {
                if (authority[close + 1] != ':')
                    return std::nullopt;
                port = authority.substr(close + 2);
            }
        }
        else if (const size_t colon = authority.rfind(':'); colon != std::string_view::npos)
        {
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        }

        if (host.empty())
            return std::nullopt;
        out.host = std::string(host);

        if (port.empty())
        {
            out.port = out.tls ? 443 : 80;
        }
        else
        {
            unsigned int value = 0;
            for (char c : port)
            {
                if (c < '0' || c > '9')
                    return std::nullopt;
                value = value * 10 + static_cast<unsigned int>(c - '0');
                if (value > 65535)
                    return std::nullopt;
            }
            if (value == 0)
                return std::nullopt;
            out.port = static_cast<uint16_t>(value);
        }

        if (target.empty())
            out.target = "/";
        else if (target.front() == '?')
            out.target = "/" + std::string(target);
        else
            out.target = std::string(target);

        return out;
    }

    asio::io_context& IoContext()
    {
        std::lock_guard lock(s_ioLock);
        if (!s_pIo)
        {
            s_pIo = std::make_unique<asio::io_context>(1);
            s_pWork = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(s_pIo->get_executor());
            s_ioThread = std::thread(IoThreadMain, s_pIo.get());
            FP_DEBUG("net: IO thread started.");
        }
        return *s_pIo;
    }

    ssl::context& ClientTlsContext(bool verifyPeer)
    {
        if (verifyPeer)
        {
            std::call_once(s_tlsVerifyOnce, [] { s_pTlsVerify = MakeClientContext(true); });
            return *s_pTlsVerify;
        }
        std::call_once(s_tlsNoVerifyOnce, [] { s_pTlsNoVerify = MakeClientContext(false); });
        return *s_pTlsNoVerify;
    }

    void PostToMain(std::function<void()> fn)
    {
        std::lock_guard lock(s_mainLock);
        s_mainQueue.push_back(std::move(fn));
    }

    void RunFrame()
    {
        std::vector<std::function<void()>> queue;
        {
            std::lock_guard lock(s_mainLock);
            if (s_mainQueue.empty())
                return;
            queue.swap(s_mainQueue);
        }
        // Outside the lock: a callback may well post again.
        for (auto& fn : queue)
            fn();
    }

    bool IsRunning()
    {
        std::lock_guard lock(s_ioLock);
        return s_pIo != nullptr;
    }

    void Shutdown()
    {
        std::unique_ptr<asio::io_context> pIo;
        {
            std::lock_guard lock(s_ioLock);
            if (s_pIo)
            {
                s_pWork.reset();
                s_pIo->stop();
            }
            pIo = std::move(s_pIo);
        }
        // The thread runs code from this library: it has to be gone before
        // Metamod unmaps it.
        if (s_ioThread.joinable())
            s_ioThread.join();
        // Destroys the handlers that never ran, and the sockets they held.
        pIo.reset();

        {
            std::lock_guard lock(s_mainLock);
            s_mainQueue.clear();
        }
    }
}
