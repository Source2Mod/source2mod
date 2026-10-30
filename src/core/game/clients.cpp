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
 * Player helpers (ISource2ModClients). See clients.h.
 */
#include "core/game/clients.h"

#include "core/hooks/hookid.h"
#include "core/ui/menus.h"
#include "core/plugins/pluginapi.h"
#include "core/runtime/scheduler.h"
#include "core/runtime/shared.h"

#include <engine2/igameeventsystem.h>
#include <engine2/serversideclient.h>
#include <igameevents.h>
#include <interfaces/interfaces.h>
#include <inetchannelinfo.h>
#include <networksystem/inetworkmessages.h>
#include <recipientfilter.h>
#include <source2mod/ISource2ModFunctions.h>
#include <source2mod/sdk/game/shared/netmessages.h>
#include <source2mod/sdk/schema/classes/CCSPlayerController.h>
#include <usermessages.pb.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

SOURCE2MOD_GLOBALVARS();

namespace clients
{
    ClientsManager clientsManager;

    namespace
    {
        constexpr uint64_t kSteamId64Base = 76561197960265728ull;

        // TextMsg's dest (HUD_PRINT*).
        enum class HudDestination
        {
            Console = 2,
            Chat = 3,
            Center = 4,
            Alert = 6,
        };

        bool SlotInRange(CPlayerSlot slot)
        {
            return slot.Get() >= 0 && slot.Get() < shared::MaxClients();
        }

        void ClientPrint(CPlayerSlot slot, HudDestination destination, const char* message)
        {
            if (!SlotInRange(slot) || !message || !g_pNetworkMessages || !shared::g_pGameEventSystem)
                return;

            static INetworkMessageInternal* s_textMsg = g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
            if (!s_textMsg)
                return;

            auto* data = s_textMsg->AllocateMessage()->ToPB<CUserMessageTextMsg>();
            data->set_dest(static_cast<int>(destination));
            data->add_param(message);

            CPlayerBitVec recipients;
            recipients.Set(slot.Get());
            shared::g_pGameEventSystem->PostEventAbstract(CSplitScreenSlot(-1), false, ABSOLUTE_PLAYER_LIMIT,
                                                          reinterpret_cast<const uint64*>(recipients.Base()), s_textMsg, data, 0,
                                                          NetChannelBufType_t::BUF_RELIABLE);
            delete data;
        }
    }

    CCSPlayerController* ClientsManager::Connected(CPlayerSlot slot)
    {
        if (!SlotInRange(slot))
            return nullptr;
        CCSPlayerController* player = CCSPlayerController::FromSlot(slot);
        if (!player)
            return nullptr;
        const PlayerConnectedState state = player->m_iConnected();
        if (state != PlayerConnectedState::Connected && state != PlayerConnectedState::Connecting
            && state != PlayerConnectedState::Reconnecting)
            return nullptr;
        return player;
    }

    CServerSideClient* ClientsManager::GetServerSideClient(CPlayerSlot slot)
    {
        if (!SlotInRange(slot) || !g_pNetworkServerService)
            return nullptr;
        // Off the server's client list, not the controller: a client that is
        // still signing on, or one that lost its controller and kept its net
        // channel, has no controller to ask.
        CNetworkGameServerBase* server = g_pNetworkServerService->GetIGameServer();
        if (!server || slot.Get() >= server->m_Clients.Count())
            return nullptr;
        // The engine keeps one client per slot and hands it to whoever takes
        // the slot next; between the two it sits at SIGNONSTATE_NONE with the
        // last client's flags still in it.
        auto* client = static_cast<CServerSideClient*>(server->m_Clients.Element(slot.Get()));
        return client && client->GetSignonState() != SIGNONSTATE_NONE ? client : nullptr;
    }

    // Everything below decides from the engine's client, not the controller:
    // a slot can have a client and no controller (signing on, or the console
    // spam bots that keep their net channel after losing it).

    bool ClientsManager::IsConnected(CPlayerSlot slot)
    {
        CServerSideClient* client = GetServerSideClient(slot);
        return client && client->IsConnected();
    }

    bool ClientsManager::IsInGame(CPlayerSlot slot)
    {
        CServerSideClient* client = GetServerSideClient(slot);
        return client && client->IsActive();
    }

    bool ClientsManager::IsFakeClient(CPlayerSlot slot)
    {
        // The engine client's flag: a bot is one before it has a controller,
        // and a slot that has a controller but lost its client is not one.
        CServerSideClient* client = GetServerSideClient(slot);
        return client && client->IsFakePlayer();
    }

    bool ClientsManager::IsAuthorized(CPlayerSlot slot)
    {
        CServerSideClient* client = GetServerSideClient(slot);
        return client && !client->IsFakePlayer() && client->IsFullyAuthenticated();
    }

    std::string ClientsManager::FormatSteamId(uint64_t steamId, ESource2ModAuthId form)
    {
        if (!steamId)
            return {};
        const uint32_t account = static_cast<uint32_t>(steamId & 0xFFFFFFFFu);
        char buf[64];
        switch (form)
        {
        case SOURCE2MOD_AUTHID_STEAM3:
            std::snprintf(buf, sizeof(buf), "[U:1:%u]", account);
            break;
        case SOURCE2MOD_AUTHID_STEAMID64:
            std::snprintf(buf, sizeof(buf), "%" PRIu64, steamId);
            break;
        default: // the engine form is Steam2, as SourceMod has it on CS:GO
            std::snprintf(buf, sizeof(buf), "STEAM_1:%u:%u", account & 1u, account >> 1);
            break;
        }
        return buf;
    }

    uint64_t ClientsManager::ParseSteamId(const char* text)
    {
        if (!text || !*text)
            return 0;
        unsigned int universe, y, z;
        char tail;
        if (std::sscanf(text, "STEAM_%u:%u:%u%c", &universe, &y, &z, &tail) == 3 && y <= 1)
            return kSteamId64Base + (static_cast<uint64_t>(z) << 1) + y;
        unsigned int account;
        if (std::sscanf(text, "[U:1:%u]%c", &account, &tail) == 1)
            return kSteamId64Base + account;
        if (std::strlen(text) == 17 && std::strncmp(text, "7656", 4) == 0)
        {
            char* end = nullptr;
            const uint64_t id = std::strtoull(text, &end, 10);
            if (end && !*end && id > kSteamId64Base)
                return id;
        }
        return 0;
    }

    std::string ClientsManager::GetAuthId(CPlayerSlot slot, ESource2ModAuthId form, bool validated)
    {
        CServerSideClient* client = GetServerSideClient(slot);
        if (!client || client->IsFakePlayer())
            return {};
        if (validated && !client->IsFullyAuthenticated())
            return {};
        return FormatSteamId(client->GetClientSteamID().ConvertToUint64(), form);
    }

    float ClientsManager::GetLatency(CPlayerSlot slot)
    {
        CServerSideClient* client = GetServerSideClient(slot);
        if (!client || client->IsFakePlayer() || !g_pEngineServer)
            return 0.0f;
        INetChannelInfo* info = g_pEngineServer->GetPlayerNetInfo(slot);
        return info ? info->GetAvgLatency() : 0.0f;
    }

    float ClientsManager::GetConnectionTime(CPlayerSlot slot)
    {
        CServerSideClient* client = GetServerSideClient(slot);
        if (!client || client->IsFakePlayer() || !g_pEngineServer)
            return 0.0f;
        INetChannelInfo* info = g_pEngineServer->GetPlayerNetInfo(slot);
        return info ? info->GetTimeConnected() : 0.0f;
    }

    std::string ClientsManager::GetClientConVar(CPlayerSlot slot, const char* name)
    {
        if (!GetServerSideClient(slot) || !g_pEngineServer || !name)
            return {};
        const char* value = g_pEngineServer->GetClientConVarValue(slot, name);
        return value ? value : "";
    }

    int ClientsManager::QueryClientConVar(CPlayerSlot slot, const char* name, ClientConVarQueryCallback callback)
    {
        const PluginId owner = hookid::OwnerOfHandler(callback);
        return QueryClientConVar(owner, slot, name, std::move(callback));
    }

    int ClientsManager::QueryClientConVar(PluginId owner, CPlayerSlot slot, const char* name, ClientConVarQueryCallback callback)
    {
        if (!name || !*name || !callback || !shared::g_pGameEventSystem || !g_pNetworkMessages)
            return -1;

        // Asked off the engine's client, not the controller: the question is
        // whether anything on the other end of the wire answers, which is also
        // worth asking of a client that has no controller (yet).
        CServerSideClient* client = GetServerSideClient(slot);
        if (!client || client->IsFakePlayer() || !client->GetNetChannel() || !client->IsConnected())
            return -1;

        static INetworkMessageInternal* s_getCvarValue = g_pNetworkMessages->FindNetworkMessageById(CSVCMsg_GetCvarValue_t::kMsgId);
        if (!s_getCvarValue)
            return -1;

        // Random, not a counter: a client that never got the question must not
        // be able to answer it by guessing the next number.
        std::uniform_int_distribution<int> cookies(1, 0x7FFFFFFE);
        int cookie;
        do
            cookie = cookies(m_random);
        while (std::any_of(m_queries.begin(), m_queries.end(), [cookie](const PendingQuery& q) { return q.cookie == cookie; }));

        auto* msg = s_getCvarValue->AllocateMessage()->ToPB<CSVCMsg_GetCvarValue>();
        msg->set_cookie(cookie);
        msg->set_cvar_name(name);

        CReliableSingleUserRecipientFilter filter(slot);
        shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, s_getCvarValue, msg, 0);
        delete msg;

        m_queries.push_back({ cookie, slot, owner, name, std::move(callback) });
        return cookie;
    }

    void ClientsManager::OnRespondCvarValue(CPlayerSlot slot, const CCLCMsg_RespondCvarValue& msg)
    {
        if (!msg.has_cookie())
            return;

        auto it = std::find_if(m_queries.begin(), m_queries.end(), [&](const PendingQuery& q)
        {
            return q.cookie == msg.cookie() && q.slot == slot;
        });
        if (it == m_queries.end())
            return;

        // The answer has to be about what was asked; anything else leaves the
        // query open rather than answering it with a name the client made up.
        if (!msg.has_name() || msg.name() != it->name)
            return;

        const int status = msg.has_status_code() ? msg.status_code() : -1;
        if (status < SOURCE2MOD_CVARQUERY_OKAY || status > SOURCE2MOD_CVARQUERY_PROTECTED)
            return;

        // Out of the list before the call: the callback is free to query again.
        PendingQuery query = std::move(*it);
        m_queries.erase(it);

        const auto result = static_cast<ESource2ModConVarQueryResult>(status);
        const std::string value = result == SOURCE2MOD_CVARQUERY_OKAY && msg.has_value() ? msg.value() : std::string();
        query.callback(slot, query.cookie, result, query.name.c_str(), value.c_str());
    }

    void ClientsManager::OnClientDisconnectPost(CPlayerSlot slot)
    {
        std::vector<PendingQuery> cancelled;
        for (auto it = m_queries.begin(); it != m_queries.end();)
        {
            if (it->slot == slot)
            {
                cancelled.push_back(std::move(*it));
                it = m_queries.erase(it);
            }
            else
                ++it;
        }

        for (PendingQuery& query : cancelled)
            query.callback(slot, query.cookie, SOURCE2MOD_CVARQUERY_CANCELLED, query.name.c_str(), "");
    }

    void ClientsManager::RemoveAllForPlugin(PluginId id)
    {
        std::erase_if(m_queries, [id](const PendingQuery& q) { return q.owner == id; });
    }

    bool ClientsManager::ReplicateConVar(CPlayerSlot slot, const char* name, const char* value)
    {
        if (!name || !*name || !g_pNetworkMessages)
            return false;

        // The engine client's own channel: there need not be a controller.
        CServerSideClient* client = GetServerSideClient(slot);
        INetChannel* channel = client ? client->GetNetChannel() : nullptr;
        if (!channel)
            return false;

        static INetworkMessageInternal* s_setConVar = g_pNetworkMessages->FindNetworkMessageById(CNETMsg_SetConVar_t::kMsgId);
        if (!s_setConVar)
            return false;

        // One message, refilled: SendNetMessage is done with it when it
        // returns, and a fresh one per call was about half of what this cost.
        static CNetMessagePB<CNETMsg_SetConVar>* s_msg = s_setConVar->AllocateMessage()->ToPB<CNETMsg_SetConVar>();
        s_msg->Clear();
        auto* cvar = s_msg->mutable_convars()->add_cvars();
        cvar->set_name(name);
        cvar->set_value(value ? value : "");

        channel->SendNetMessage(s_msg, BUF_DEFAULT);
        return true;
    }

    std::string ClientsManager::GetIpAddress(CPlayerSlot slot)
    {
        if (!SlotInRange(slot) || !g_pEngineServer)
            return {};
        const INetChannelInfo* info = g_pEngineServer->GetPlayerNetInfo(slot);
        if (!info)
            return {};

        // GetIP() is in network order.
        const uint32_t ip = info->GetRemoteAddress().GetIP();
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
        return buffer;
    }

    void ClientsManager::PrintToConsole(CPlayerSlot slot, const char* message)
    {
        if (!message)
            return;
        std::string line;
        line.reserve(std::strlen(message) + 1);
        line = message;
        line += '\n';
        ClientPrint(slot, HudDestination::Console, line.c_str());
    }

    void ClientsManager::PrintToChat(CPlayerSlot slot, const char* message)
    {
        ClientPrint(slot, HudDestination::Chat, message);
    }

    void ClientsManager::PrintToCenter(CPlayerSlot slot, const char* message)
    {
        ClientPrint(slot, HudDestination::Center, message);
    }

    void ClientsManager::PrintToCenterAlert(CPlayerSlot slot, const char* message)
    {
        ClientPrint(slot, HudDestination::Alert, message);
    }

    void ClientsManager::PrintToCenterHtml(CPlayerSlot slot, const char* message, int duration, bool menu)
    {
        if (!SlotInRange(slot) || !message || !shared::g_pGameEventManager)
            return;

        if (!menu)
        {
            CCSPlayerController* player = CCSPlayerController::FromSlot(slot);
            if (player && menus::menuManager.GetActiveMenu(player))
                return;
        }

        IGameEvent* event = shared::g_pGameEventManager->CreateEvent("show_survival_respawn_status", true);
        if (!event)
            return;

        event->SetString("loc_token", message);
        event->SetInt("duration", duration);
        event->SetPlayer("userid", slot);

        FireEventToClient(slot, event);
        shared::g_pGameEventManager->FreeEvent(event);
    }

    void ClientsManager::ExecuteClientCommand(CPlayerSlot slot, const char* command)
    {
        if (!SlotInRange(slot) || !command || !g_pEngineServer)
            return;
        g_pEngineServer->ClientCommand(slot, "%s", command);
    }

    void ClientsManager::ExecuteClientCommandFromServer(CPlayerSlot slot, const char* command)
    {
        if (!SlotInRange(slot) || !command || !g_pCVar)
            return;

        CCommand args;
        args.Tokenize(command);
        if (args.ArgC() < 1)
            return;

        auto handle = g_pCVar->FindConCommand(args.Arg(0));
        if (!handle.IsValidRef())
            return;

        CCommandContext context(CommandTarget_t::CT_NO_TARGET, slot);
        g_pCVar->DispatchConCommand(handle, context, args);
    }

    void ClientsManager::FireEventToClient(CPlayerSlot slot, IGameEvent* event)
    {
        if (!SlotInRange(slot) || !event)
            return;

        // CPlayerSlot is one int, passed like one.
        IGameEventListener2* listener = SOURCE2MOD_CALL_FUNCTION(IGameEventListener2*(int), "LegacyGameEventListener", HookChain::Run,
                                                                 slot.Get());
        if (listener)
            listener->FireGameEvent(event);
    }

    void ClientsManager::Disconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* internalReason)
    {
        CServerSideClient* client = GetServerSideClient(slot);
        if (!client)
            return;

        if (CCSPlayerController* player = CCSPlayerController::FromSlot(slot))
            menus::menuManager.CloseActiveMenu(player);

        // Next frame: the caller may be inside this client's own hooks. By then
        // a reason built on the spot -- fmt::format(...).c_str() -- is gone, so
        // the task keeps a copy; and the slot may hold somebody else, so it
        // goes only if the same user id is still there.
        const int userId = client->GetUserID().Get();
        const bool hasReason = internalReason != nullptr;
        std::string reasonText = hasReason ? internalReason : "";
        scheduler::schedulerManager.NextFrame(0, [this, slot, userId, reason, hasReason, reasonText = std::move(reasonText)]
        {
            CServerSideClient* now = GetServerSideClient(slot);
            if (!now || now->GetUserID().Get() != userId || !g_pEngineServer)
                return;
            g_pEngineServer->DisconnectClient(slot, reason, hasReason ? reasonText.c_str() : nullptr);
        });
    }

    int ClientsManager::GetClientCount(bool inGameOnly)
    {
        const int maxClients = shared::MaxClients();
        int count = 0;
        for (int i = 0; i < maxClients; i++)
        {
            if (inGameOnly ? IsInGame(CPlayerSlot(i)) : IsConnected(CPlayerSlot(i)))
                count++;
        }
        return count;
    }
}
