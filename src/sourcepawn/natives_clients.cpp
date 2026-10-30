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
 * SourcePawn natives for source2mod/clients.inc: Source2ModClients on
 * ISource2ModClients.
 */
#include "natives.h"
#include "core/runtime/shared.h"

#include "scriptplugin.h"

#include "core/game/clients.h"

#include <engine2/serversideclient.h>
#include <interfaces/interfaces.h>
#include <sp_callable.h>

#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        clients::ClientsManager& Clients()
        {
            return clients::clientsManager;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        ESource2ModAuthId Form(cell_t form)
        {
            return (form >= SOURCE2MOD_AUTHID_ENGINE && form <= SOURCE2MOD_AUTHID_STEAMID64) ? static_cast<ESource2ModAuthId>(form)
                                                                                            : SOURCE2MOD_AUTHID_ENGINE;
        }

        cell_t N_IsConnected(IPluginContext*, const cell_t* params)
        {
            return Clients().IsConnected(CPlayerSlot(params[1]));
        }

        cell_t N_IsInGame(IPluginContext*, const cell_t* params)
        {
            return Clients().IsInGame(CPlayerSlot(params[1]));
        }

        cell_t N_IsFakeClient(IPluginContext*, const cell_t* params)
        {
            return Clients().IsFakeClient(CPlayerSlot(params[1]));
        }

        cell_t N_IsAuthorized(IPluginContext*, const cell_t* params)
        {
            return Clients().IsAuthorized(CPlayerSlot(params[1]));
        }

        // GetAuthId(slot, form, buffer, maxlen, validated)
        cell_t N_GetAuthId(IPluginContext* ctx, const cell_t* params)
        {
            const bool validated = params[0] < 5 || params[5];
            return ToBuffer(ctx, params[3], params[4], Clients().GetAuthId(CPlayerSlot(params[1]), Form(params[2]), validated).c_str());
        }

        cell_t N_GetLatency(IPluginContext*, const cell_t* params)
        {
            return sp_ftoc(Clients().GetLatency(CPlayerSlot(params[1])));
        }

        cell_t N_GetConnectionTime(IPluginContext*, const cell_t* params)
        {
            return sp_ftoc(Clients().GetConnectionTime(CPlayerSlot(params[1])));
        }

        // GetClientConVar(slot, name, buffer, maxlen)
        cell_t N_GetClientConVar(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[3], params[4], Clients().GetClientConVar(CPlayerSlot(params[1]), LocalString(ctx, params[2])).c_str());
        }

        // Kick(slot, format, ...)
        cell_t N_Kick(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<char> message(1024);
            if (!FormatParams(ctx, params, 2, message.data(), message.size()))
                return 0;
            Clients().Disconnect(CPlayerSlot(params[1]), NETWORK_DISCONNECT_KICKED, message.data());
            return 0;
        }

        cell_t N_GetClientCount(IPluginContext*, const cell_t* params)
        {
            return Clients().GetClientCount(params[0] < 1 || params[1]);
        }

        // ConvertSteamId(steamId, form, buffer, maxlen)
        cell_t N_ConvertSteamId(IPluginContext* ctx, const cell_t* params)
        {
            const uint64_t id = clients::ClientsManager::ParseSteamId(LocalString(ctx, params[1]));
            if (!id)
                return 0;
            ToBuffer(ctx, params[3], params[4], clients::ClientsManager::FormatSteamId(id, Form(params[2])).c_str());
            return 1;
        }

        cell_t N_GetUserId(IPluginContext*, const cell_t* params)
        {
            if (!g_pEngineServer || !Clients().IsConnected(CPlayerSlot(params[1])))
                return -1;
            return g_pEngineServer->GetPlayerUserId(CPlayerSlot(params[1])).Get();
        }

        cell_t N_FromUserId(IPluginContext*, const cell_t* params)
        {
            if (!g_pEngineServer)
                return -1;
            for (int slot = 0, n = shared::MaxClients(); slot < n; slot++)
            {
                if (Clients().IsConnected(CPlayerSlot(slot)) && g_pEngineServer->GetPlayerUserId(CPlayerSlot(slot)).Get() == params[1])
                    return slot;
            }
            return -1;
        }

        cell_t N_FromSteamId(IPluginContext* ctx, const cell_t* params)
        {
            const uint64_t id = clients::ClientsManager::ParseSteamId(LocalString(ctx, params[1]));
            if (!id)
                return -1;
            const std::string want = clients::ClientsManager::FormatSteamId(id, SOURCE2MOD_AUTHID_STEAMID64);
            for (int slot = 0, n = shared::MaxClients(); slot < n; slot++)
            {
                if (Clients().GetAuthId(CPlayerSlot(slot), SOURCE2MOD_AUTHID_STEAMID64, false) == want)
                    return slot;
            }
            return -1;
        }

        // ---- CServerSideClient (a slot; the client is looked up on every call) ----

        CServerSideClient* ServerClient(const cell_t* params)
        {
            return Clients().GetServerSideClient(CPlayerSlot(params[1]));
        }

        cell_t N_SscIsValid(IPluginContext*, const cell_t* params)
        {
            return ServerClient(params) != nullptr;
        }

        cell_t N_SscSignonState(IPluginContext*, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return client ? static_cast<cell_t>(client->GetSignonState()) : 0;
        }

        cell_t N_SscIsFakePlayer(IPluginContext*, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return client && client->IsFakePlayer();
        }

        cell_t N_SscIsHLTV(IPluginContext*, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return client && client->IsHLTV();
        }

        cell_t N_SscHasNetChannel(IPluginContext*, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return client && client->GetNetChannel() != nullptr;
        }

        cell_t N_SscIsFullyAuthenticated(IPluginContext*, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return client && client->IsFullyAuthenticated();
        }

        cell_t N_SscUserId(IPluginContext*, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return client ? client->GetUserID().Get() : -1;
        }

        // GetName(buffer, maxlen)
        cell_t N_SscGetName(IPluginContext* ctx, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return ToBuffer(ctx, params[2], params[3], client ? client->GetClientName() : "");
        }

        // GetNetworkIDString(buffer, maxlen)
        cell_t N_SscGetNetworkIDString(IPluginContext* ctx, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            return ToBuffer(ctx, params[2], params[3], client ? client->GetNetworkIDString() : "");
        }

        // GetSteamID(buffer, maxlen)
        cell_t N_SscGetSteamID(IPluginContext* ctx, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            const uint64_t id = client ? client->GetClientSteamID().ConvertToUint64() : 0;
            return ToBuffer(ctx, params[2], params[3], id ? std::to_string(id).c_str() : "");
        }

        // GetAddress(buffer, maxlen)
        cell_t N_SscGetAddress(IPluginContext* ctx, const cell_t* params)
        {
            CServerSideClient* client = ServerClient(params);
            char address[64] = "";
            if (client && client->GetNetChannel())
                client->GetRemoteAddress()->ToString(address, sizeof(address));
            return ToBuffer(ctx, params[2], params[3], address);
        }

        // QueryConVar(name, callback, value)
        cell_t N_SscQueryConVar(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[3]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[3]);

            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            const PluginId owner = plugin ? plugin->Id() : 0;
            const cell_t value = params[4];

            // Dropped with the script's other registrations when it unloads,
            // so fn never outlives it.
            ClientConVarQueryCallback callback([fn, value](CPlayerSlot slot, int cookie, ESource2ModConVarQueryResult result,
                                                           const char* name, const char* cvarValue)
            {
                sp::CallArgs args;
                args.PushCell(cookie);
                args.PushCell(slot.Get());
                args.PushCell(static_cast<cell_t>(result));
                args.PushString(name);
                args.PushString(cvarValue);
                args.PushCell(value);
                Invoke(fn, args);
            });
            return Clients().QueryClientConVar(owner, CPlayerSlot(params[1]), LocalString(ctx, params[2]), std::move(callback));
        }

        // ReplicateConVar(name, value)
        cell_t N_SscReplicateConVar(IPluginContext* ctx, const cell_t* params)
        {
            return Clients().ReplicateConVar(CPlayerSlot(params[1]), LocalString(ctx, params[2]), LocalString(ctx, params[3]));
        }

        // GetIpAddress(buffer, maxlen)
        cell_t N_SscGetIpAddress(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], Clients().GetIpAddress(CPlayerSlot(params[1])).c_str());
        }

        // PrintTo*(message)
        template <void (clients::ClientsManager::*Print)(CPlayerSlot, const char*)>
        cell_t N_SscPrint(IPluginContext* ctx, const cell_t* params)
        {
            (Clients().*Print)(CPlayerSlot(params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        // PrintToCenterHtml(message, duration, menu)
        cell_t N_SscPrintToCenterHtml(IPluginContext* ctx, const cell_t* params)
        {
            Clients().PrintToCenterHtml(CPlayerSlot(params[1]), LocalString(ctx, params[2]), params[3], params[4] != 0);
            return 0;
        }

        // ExecuteClientCommand(command)
        cell_t N_SscExecuteClientCommand(IPluginContext* ctx, const cell_t* params)
        {
            Clients().ExecuteClientCommand(CPlayerSlot(params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        // ExecuteClientCommandFromServer(command)
        cell_t N_SscExecuteClientCommandFromServer(IPluginContext* ctx, const cell_t* params)
        {
            Clients().ExecuteClientCommandFromServer(CPlayerSlot(params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        // FireEventToClient(event): the event stays the script's.
        cell_t N_SscFireEventToClient(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* event = ScriptEvent(ctx, params[2]))
                Clients().FireEventToClient(CPlayerSlot(params[1]), event);
            return 0;
        }

        // Disconnect(reason, internalReason): "" logs no reason, as the controller's does.
        cell_t N_SscDisconnect(IPluginContext* ctx, const cell_t* params)
        {
            const char* internalReason = LocalString(ctx, params[3]);
            Clients().Disconnect(CPlayerSlot(params[1]), static_cast<ENetworkDisconnectionReason>(params[2]),
                                 internalReason && *internalReason ? internalReason : nullptr);
            return 0;
        }

        const sp_nativeinfo_t g_clientNatives[] = {
            { "CServerSideClient.IsValid.get", N_SscIsValid },
            { "CServerSideClient.SignonState.get", N_SscSignonState },
            { "CServerSideClient.IsFakePlayer.get", N_SscIsFakePlayer },
            { "CServerSideClient.IsHLTV.get", N_SscIsHLTV },
            { "CServerSideClient.HasNetChannel.get", N_SscHasNetChannel },
            { "CServerSideClient.IsFullyAuthenticated.get", N_SscIsFullyAuthenticated },
            { "CServerSideClient.UserId.get", N_SscUserId },
            { "CServerSideClient.GetName", N_SscGetName },
            { "CServerSideClient.GetNetworkIDString", N_SscGetNetworkIDString },
            { "CServerSideClient.GetSteamID", N_SscGetSteamID },
            { "CServerSideClient.GetAddress", N_SscGetAddress },
            { "CServerSideClient.QueryConVar", N_SscQueryConVar },
            { "CServerSideClient.ReplicateConVar", N_SscReplicateConVar },
            { "CServerSideClient.GetIpAddress", N_SscGetIpAddress },
            { "CServerSideClient.PrintToConsole", N_SscPrint<&clients::ClientsManager::PrintToConsole> },
            { "CServerSideClient.PrintToChat", N_SscPrint<&clients::ClientsManager::PrintToChat> },
            { "CServerSideClient.PrintToCenter", N_SscPrint<&clients::ClientsManager::PrintToCenter> },
            { "CServerSideClient.PrintToCenterAlert", N_SscPrint<&clients::ClientsManager::PrintToCenterAlert> },
            { "CServerSideClient.PrintToCenterHtml", N_SscPrintToCenterHtml },
            { "CServerSideClient.ExecuteClientCommand", N_SscExecuteClientCommand },
            { "CServerSideClient.ExecuteClientCommandFromServer", N_SscExecuteClientCommandFromServer },
            { "CServerSideClient.FireEventToClient", N_SscFireEventToClient },
            { "CServerSideClient.Disconnect", N_SscDisconnect },
            // The same calls where ISource2ModEvents / ISource2ModConVars have them.
            { "Source2ModEvents.FireEventToClient", N_SscFireEventToClient },
            { "Source2ModConVars.QueryClientConVar", N_SscQueryConVar },
            { "Source2ModConVars.ReplicateConVar", N_SscReplicateConVar },
            { "Source2ModCommands.ExecuteClientCommand", N_SscExecuteClientCommand },
            { "Source2ModCommands.ExecuteClientCommandFromServer", N_SscExecuteClientCommandFromServer },
            { "Source2ModClients.IsConnected", N_IsConnected },
            { "Source2ModClients.IsInGame", N_IsInGame },
            { "Source2ModClients.IsFakeClient", N_IsFakeClient },
            { "Source2ModClients.IsAuthorized", N_IsAuthorized },
            { "Source2ModClients.GetAuthId", N_GetAuthId },
            { "Source2ModClients.GetLatency", N_GetLatency },
            { "Source2ModClients.GetConnectionTime", N_GetConnectionTime },
            { "Source2ModClients.GetClientConVar", N_GetClientConVar },
            { "Source2ModClients.Kick", N_Kick },
            { "Source2ModClients.GetClientCount", N_GetClientCount },
            { "Source2ModClients.ConvertSteamId", N_ConvertSteamId },
            { "Source2ModClients.GetUserId", N_GetUserId },
            { "Source2ModClients.FromUserId", N_FromUserId },
            { "Source2ModClients.FromSteamId", N_FromSteamId },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetClientNatives()
    {
        return g_clientNatives;
    }
}
