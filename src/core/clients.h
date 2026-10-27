/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */

/*
 * Player helpers the controller classes do not have (ISource2ModClients).
 */
#pragma once

#include <source2mod/ISource2ModClients.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

class CCSPlayerController;
class CCLCMsg_RespondCvarValue;

namespace clients
{
    class ClientsManager final : public ISource2ModClients
    {
    public:
        CServerSideClient* GetServerSideClient(CPlayerSlot slot) override;
        bool IsConnected(CPlayerSlot slot) override;
        bool IsInGame(CPlayerSlot slot) override;
        bool IsFakeClient(CPlayerSlot slot) override;
        bool IsAuthorized(CPlayerSlot slot) override;
        std::string GetAuthId(CPlayerSlot slot, ESource2ModAuthId form, bool validated = true) override;
        float GetLatency(CPlayerSlot slot) override;
        float GetConnectionTime(CPlayerSlot slot) override;
        std::string GetClientConVar(CPlayerSlot slot, const char* name) override;
        int QueryClientConVar(CPlayerSlot slot, const char* name, ClientConVarQueryCallback callback) override;
        bool ReplicateConVar(CPlayerSlot slot, const char* name, const char* value) override;
        std::string GetIpAddress(CPlayerSlot slot) override;
        void PrintToConsole(CPlayerSlot slot, const char* message) override;
        void PrintToChat(CPlayerSlot slot, const char* message) override;
        void PrintToCenter(CPlayerSlot slot, const char* message) override;
        void PrintToCenterAlert(CPlayerSlot slot, const char* message) override;
        void PrintToCenterHtml(CPlayerSlot slot, const char* message, int duration = 5, bool menu = false) override;
        void ExecuteClientCommand(CPlayerSlot slot, const char* command) override;
        void ExecuteClientCommandFromServer(CPlayerSlot slot, const char* command) override;
        void FireEventToClient(CPlayerSlot slot, IGameEvent* event) override;
        void Disconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* internalReason = nullptr) override;
        int GetClientCount(bool inGameOnly = true) override;

        // ---- core side ----

        /// The controller of a connected (connecting or in game) slot; nullptr otherwise.
        static CCSPlayerController* Connected(CPlayerSlot slot);
        /// A SteamID64 in a form; empty for 0.
        static std::string FormatSteamId(uint64_t steamId, ESource2ModAuthId form);
        /// Any SteamID form back to a SteamID64; 0 when the text is none.
        static uint64_t ParseSteamId(const char* text);

        /// QueryClientConVar on behalf of @p owner (a script names its own).
        int QueryClientConVar(PluginId owner, CPlayerSlot slot, const char* name, ClientConVarQueryCallback callback);
        /// A CLC_RespondCvarValue off the wire (FilterMessage).
        void OnRespondCvarValue(CPlayerSlot slot, const CCLCMsg_RespondCvarValue& msg);
        /// The slot's open queries answer CANCELLED.
        void OnClientDisconnect(CPlayerSlot slot);
        /// Drops a plugin's open queries unanswered: its callbacks are about to go.
        void RemoveAllForPlugin(PluginId id);

    private:
        struct PendingQuery
        {
            int cookie;
            CPlayerSlot slot;
            PluginId owner;
            std::string name;
            ClientConVarQueryCallback callback;
        };

        std::vector<PendingQuery> m_queries;
        std::mt19937 m_random{ std::random_device{}() };
    };

    extern ClientsManager clientsManager;
}
