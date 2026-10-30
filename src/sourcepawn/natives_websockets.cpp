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
 * SourcePawn natives for source2mod/websockets.inc on the core's WebSockets
 * (core/websockets.cpp). A Source2ModWebSocket handle is the socket: closing
 * it destroys the socket, and the script's unload closes its handles.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/net/websockets.h"

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

        /// An unset function argument: INVALID_FUNCTION compiles to 0 with this spcomp, -1 with others.
        IPluginFunction* OptionalFunction(IPluginContext* ctx, cell_t id)
        {
            if (id == -1 || id == 0)
                return nullptr;
            return ctx->GetFunctionById(static_cast<funcid_t>(id));
        }

        /// The script's side of a socket: the handle's object, and the
        /// listener that turns the socket's reports into the script's callbacks.
        struct ScriptSocket final : HandleObject, ISource2ModWebSocketListener
        {
            ~ScriptSocket() override
            {
                // Not found when the script's unload took the socket first.
                if (socket)
                    websockets::webSocketsManager.Destroy(socket);
            }

            void OnWebSocketOpen(ISource2ModWebSocket*, bool reconnected) override
            {
                if (!onOpen)
                    return;
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(reconnected);
                args.PushCell(data);
                Invoke(onOpen, args);
            }

            void OnWebSocketMessage(ISource2ModWebSocket*, const char* message, size_t length, bool binary) override
            {
                if (!onMessage)
                    return;
                // A copy the VM reads as binary: the data may hold zeros.
                std::string copy(message, length);
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushString(copy.data(), copy.size() + 1, SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY);
                args.PushCell(static_cast<cell_t>(length));
                args.PushCell(binary);
                args.PushCell(data);
                Invoke(onMessage, args);
            }

            void OnWebSocketClose(ISource2ModWebSocket*, int code, const char* reason) override
            {
                if (!onClose)
                    return;
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(code);
                args.PushString(reason);
                args.PushCell(data);
                Invoke(onClose, args);
            }

            void OnWebSocketError(ISource2ModWebSocket*, const char* error) override
            {
                if (!onError)
                    return;
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushString(error);
                args.PushCell(data);
                Invoke(onError, args);
            }

            ISource2ModWebSocket* socket = nullptr;
            cell_t handle = 0;
            cell_t data = 0;
            IPluginFunction* onMessage = nullptr;
            IPluginFunction* onOpen = nullptr;
            IPluginFunction* onClose = nullptr;
            IPluginFunction* onError = nullptr;
        };

        HandleType SocketType()
        {
            static const HandleType t = Handles().CreateType("Source2ModWebSocket", [](void* p) { delete static_cast<ScriptSocket*>(p); });
            return t;
        }

        ScriptSocket* SocketOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ScriptSocket*>(ReadHandle(ctx, handle, SocketType()));
        }

        // Source2ModWebSocket(url, onMessage, onOpen, onClose, onError, data, headers, autoReconnect, verifyPeer, pingInterval)
        cell_t N_WebSocketNew(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* onMessage = ctx->GetFunctionById(static_cast<funcid_t>(params[2]));
            if (!onMessage)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);

            const std::vector<Source2ModHTTPHeader>* headers;
            if (!HttpHeadersArg(ctx, params[7], &headers))
                return 0;

            Source2ModWebSocketOptions options;
            if (headers)
                options.m_Headers = *headers;
            options.m_bAutoReconnect = params[8] != 0;
            options.m_bVerifyPeer = params[9] != 0;
            options.m_nPingIntervalSec = params[10] > 0 ? params[10] : 0;

            const PluginId owner = CallerId(ctx);
            auto* s = new ScriptSocket();
            s->onMessage = onMessage;
            s->onOpen = OptionalFunction(ctx, params[3]);
            s->onClose = OptionalFunction(ctx, params[4]);
            s->onError = OptionalFunction(ctx, params[5]);
            s->data = params[6];

            s->socket = websockets::webSocketsManager.Connect(owner, LocalString(ctx, params[1]), s, options);
            if (!s->socket)
            {
                delete s;
                return 0;
            }

            // Before any callback needs it: they all run on a later frame.
            const cell_t handle = Handles().Create(SocketType(), s, owner);
            if (!handle)
            {
                delete s;
                return 0;
            }
            s->handle = handle;
            return handle;
        }

        cell_t N_WebSocketSend(IPluginContext* ctx, const cell_t* params)
        {
            ScriptSocket* s = SocketOf(ctx, params[1]);
            return s && s->socket->Send(LocalString(ctx, params[2]));
        }

        cell_t N_WebSocketSendBinary(IPluginContext* ctx, const cell_t* params)
        {
            ScriptSocket* s = SocketOf(ctx, params[1]);
            if (!s)
                return 0;
            if (params[3] < 0)
                return ctx->ThrowNativeError("Invalid length %d", params[3]);
            char* data;
            ctx->LocalToString(params[2], &data);
            return s->socket->SendBinary(data, static_cast<size_t>(params[3]));
        }

        cell_t N_WebSocketClose(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptSocket* s = SocketOf(ctx, params[1]))
                s->socket->Close(params[2], LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_WebSocketGetUrl(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptSocket* s = SocketOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[2], params[3], s->socket->GetUrl(), nullptr);
            return 0;
        }

        cell_t N_WebSocketState(IPluginContext* ctx, const cell_t* params)
        {
            ScriptSocket* s = SocketOf(ctx, params[1]);
            return static_cast<cell_t>(s ? s->socket->GetState() : ESource2ModWebSocketState::Closed);
        }

        const sp_nativeinfo_t g_webSocketNatives[] = {
            { "Source2ModWebSocket.Source2ModWebSocket", N_WebSocketNew },
            { "Source2ModWebSocket.Send", N_WebSocketSend },
            { "Source2ModWebSocket.SendBinary", N_WebSocketSendBinary },
            { "Source2ModWebSocket.Close", N_WebSocketClose },
            { "Source2ModWebSocket.GetUrl", N_WebSocketGetUrl },
            { "Source2ModWebSocket.State.get", N_WebSocketState },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetWebSocketNatives()
    {
        return g_webSocketNatives;
    }
}
