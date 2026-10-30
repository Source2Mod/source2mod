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

/*
 * SourcePawn natives for source2mod/http.inc on the core's HTTP manager
 * (core/http.cpp). Requests are made in the calling script's name, so the core
 * drops them when it unloads.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"
#include "natives_json.h"

#include "core/net/http.h"
#include "core/data/sqlite.h"

#include <sp_callable.h>

#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        HandleType FrameType(const char* name)
        {
            return Handles().CreateType(name, nullptr, 0, false);
        }

        // ---- HTTP ---------------------------------------------------------------------

        struct Headers : HandleObject
        {
            std::vector<Source2ModHTTPHeader> list;
        };

        HandleType HeadersType()
        {
            static const HandleType t = Handles().CreateType("Source2ModHTTPHeaders", [](void* p) { delete static_cast<Headers*>(p); });
            return t;
        }

        Headers* HeadersOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Headers*>(ReadHandle(ctx, handle, HeadersType()));
        }

        struct ResponseFrame
        {
            const Source2ModHTTPResponse* response;
            JsonValues json;
        };

        HandleType ResponseType()
        {
            static const HandleType t = FrameType("Source2ModHTTPResponse");
            return t;
        }

        ResponseFrame* ResponseOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ResponseFrame*>(ReadHandle(ctx, handle, ResponseType()));
        }

        cell_t N_HeadersNew(IPluginContext* ctx, const cell_t*)
        {
            auto* h = new Headers();
            const cell_t handle = Handles().Create(HeadersType(), h, CallerId(ctx));
            if (!handle)
                delete h;
            return handle;
        }

        cell_t N_HeadersAdd(IPluginContext* ctx, const cell_t* params)
        {
            if (Headers* h = HeadersOf(ctx, params[1]))
                h->list.emplace_back(LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_HeadersCount(IPluginContext* ctx, const cell_t* params)
        {
            Headers* h = HeadersOf(ctx, params[1]);
            return h ? static_cast<cell_t>(h->list.size()) : 0;
        }

        cell_t N_HeadersClear(IPluginContext* ctx, const cell_t* params)
        {
            if (Headers* h = HeadersOf(ctx, params[1]))
                h->list.clear();
            return 0;
        }

        cell_t N_ResponseSuccess(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r && r->response->m_bSuccess;
        }

        cell_t N_ResponseStatusCode(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? r->response->m_nStatusCode : 0;
        }

        cell_t N_ResponseGetBody(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? ToBuffer(ctx, params[2], params[3], r->response->m_sBody.c_str()) : 0;
        }

        cell_t N_ResponseBodyLength(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? static_cast<cell_t>(r->response->m_sBody.size()) : 0;
        }

        cell_t N_ResponseJson(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? r->json.For(r->response->m_pJson) : 0;
        }

        Source2ModHTTPCallback HttpCallback(IPluginFunction* fn, cell_t data, PluginId owner)
        {
            return Source2ModHTTPCallback([fn, data, owner](const Source2ModHTTPResponse& response)
            {
                ResponseFrame frame{ &response, {} };
                frame.json.owner = owner;
                const cell_t handle = Handles().Create(ResponseType(), &frame, owner);
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(data);
                Invoke(fn, args);
                frame.json.FreeAll();
                Handles().Free(handle, 0, true);
            });
        }

        /// The headers argument (null allowed); false after a script error.
        bool HeadersArg(IPluginContext* ctx, cell_t handle, const std::vector<Source2ModHTTPHeader>** out)
        {
            *out = nullptr;
            if (!handle)
                return true;
            Headers* h = HeadersOf(ctx, handle);
            if (!h)
                return false;
            *out = &h->list;
            return true;
        }

        const char* BodyArg(IPluginContext* ctx, cell_t addr)
        {
            const char* body = LocalString(ctx, addr);
            return *body ? body : nullptr;
        }

        // Request(method, url, body, callback, data, headers)
        cell_t N_HttpRequest(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] < static_cast<cell_t>(ESource2ModHTTPMethod::Get) || params[1] > static_cast<cell_t>(ESource2ModHTTPMethod::Head))
                return ctx->ThrowNativeError("Invalid HTTP method %d", params[1]);
            IPluginFunction* fn = Function(ctx, params[4]);
            const std::vector<Source2ModHTTPHeader>* headers;
            if (!fn || !HeadersArg(ctx, params[6], &headers))
                return 0;
            const PluginId owner = CallerId(ctx);
            http::httpManager.Request(owner, static_cast<ESource2ModHTTPMethod>(params[1]), LocalString(ctx, params[2]), BodyArg(ctx, params[3]),
                                      HttpCallback(fn, params[5], owner), headers);
            return 0;
        }

        // Get(url, callback, data, headers)
        cell_t N_HttpGet(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            const std::vector<Source2ModHTTPHeader>* headers;
            if (!fn || !HeadersArg(ctx, params[4], &headers))
                return 0;
            const PluginId owner = CallerId(ctx);
            http::httpManager.Get(owner, LocalString(ctx, params[1]), HttpCallback(fn, params[3], owner), headers);
            return 0;
        }

#define S2M_HTTP_WITH_BODY(name)                                                                                   \
        cell_t N_Http##name(IPluginContext* ctx, const cell_t* params)                                            \
        {                                                                                                         \
            IPluginFunction* fn = Function(ctx, params[3]);                                                       \
            const std::vector<Source2ModHTTPHeader>* headers;                                                     \
            if (!fn || !HeadersArg(ctx, params[5], &headers))                                                     \
                return 0;                                                                                         \
            const PluginId owner = CallerId(ctx);                                                                 \
            http::httpManager.name(owner, LocalString(ctx, params[1]), BodyArg(ctx, params[2]),                    \
                                   HttpCallback(fn, params[4], owner), headers);                                  \
            return 0;                                                                                             \
        }
        S2M_HTTP_WITH_BODY(Post)
        S2M_HTTP_WITH_BODY(Put)
        S2M_HTTP_WITH_BODY(Patch)
        S2M_HTTP_WITH_BODY(Delete)
#undef S2M_HTTP_WITH_BODY

        cell_t N_HttpIsAvailable(IPluginContext*, const cell_t*)
        {
            return http::httpManager.IsAvailable();
        }

        cell_t N_HttpPendingCount(IPluginContext*, const cell_t*)
        {
            return http::httpManager.PendingCount();
        }

        const sp_nativeinfo_t g_httpNatives[] = {
            { "Source2ModHTTPHeaders.Source2ModHTTPHeaders", N_HeadersNew },
            { "Source2ModHTTPHeaders.Add", N_HeadersAdd },
            { "Source2ModHTTPHeaders.Count", N_HeadersCount },
            { "Source2ModHTTPHeaders.Clear", N_HeadersClear },
            { "Source2ModHTTPResponse.Success", N_ResponseSuccess },
            { "Source2ModHTTPResponse.StatusCode", N_ResponseStatusCode },
            { "Source2ModHTTPResponse.GetBody", N_ResponseGetBody },
            { "Source2ModHTTPResponse.BodyLength", N_ResponseBodyLength },
            { "Source2ModHTTPResponse.Json", N_ResponseJson },
            { "Source2ModHTTP.IsAvailable", N_HttpIsAvailable },
            { "Source2ModHTTP.PendingCount", N_HttpPendingCount },
            { "Source2ModHTTP.Request", N_HttpRequest },
            { "Source2ModHTTP.Get", N_HttpGet },
            { "Source2ModHTTP.Post", N_HttpPost },
            { "Source2ModHTTP.Put", N_HttpPut },
            { "Source2ModHTTP.Patch", N_HttpPatch },
            { "Source2ModHTTP.Delete", N_HttpDelete },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetHttpNatives()
    {
        return g_httpNatives;
    }

    bool HttpHeadersArg(IPluginContext* ctx, cell_t handle, const std::vector<Source2ModHTTPHeader>** out)
    {
        return HeadersArg(ctx, handle, out);
    }
}
