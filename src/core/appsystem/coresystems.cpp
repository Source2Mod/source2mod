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
#include "core/appsystem/appsystem.h"

#include "core/game/addresses.h"
#include "core/game/commands.h"
#include "core/game/convars.h"
#include "core/hooks/inlinehooks.h"
#include "core/net/net.h"
#include "core/game/patches.h"
#include "core/plugins/pluginapi.h"
#include "core/ui/slotinput.h"
#include "sourcepawn/scriptplugin.h"
#include "utils/log.h"
#include "core/hooks/virtualhooks.h"

#include <cstdio>

// The parts of the core with a lifetime but no manager object of their own.
namespace appsystem
{
    namespace
    {
        class AddressesSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::Addresses; }
            bool Init(char* error, size_t maxlen) override
            {
                if (addresses::Initialize())
                    return true;
                std::snprintf(error, maxlen, "Could not initialize addresses.");
                return false;
            }
        } s_addresses;

        class PatchesSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::Patches; }
            bool Init(char* error, size_t maxlen) override
            {
                if (patches::Initialize())
                    return true;
                std::snprintf(error, maxlen, "Could not initialize patches.");
                return false;
            }
        } s_patches;

        class GlobalsSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::Globals; }
            bool Init(char*, size_t) override
            {
                FillSource2ModGlobals();
                return true;
            }
        } s_globals;

        class ConVarRegistrationSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::ConVarRegistration; }
            bool Init(char*, size_t) override
            {
                ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);
                return true;
            }
            void Shutdown() override { ConVar_Unregister(); }
        } s_conVarRegistration;

        class UnlockSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::Unlock; }
            bool Init(char*, size_t) override
            {
                commands::commandsManager.UnlockConCommands();
                convars::convarsManager.UnlockConVars();
                return true;
            }
        } s_unlock;

        class SlotInputSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::SlotInput; }
            bool Init(char*, size_t) override
            {
                slotinput::Init();
                return true;
            }
            void Shutdown() override { slotinput::Shutdown(); }
            void OnGameFrame(bool) override { slotinput::OnFrame(); }
            void OnClientPutInServer(CPlayerSlot slot, int, uint64) override { slotinput::ResetPlayer(slot); }
        } s_slotInput;

        class InlineHooksSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::InlineHooks; }
            bool Init(char*, size_t) override
            {
                inlinehooks::inlines.InitListeners();
                return true;
            }
            void Shutdown() override { inlinehooks::inlines.DestructListeners(); }
        } s_inlineHooks;

        class VirtualHooksSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::VirtualHooks; }
            bool Init(char*, size_t) override
            {
                virtualhooks::virtuals.InitListeners();
                return true;
            }
            void Shutdown() override { virtualhooks::virtuals.DestructListeners(); }
        } s_virtualHooks;

        class EntityListenerSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::EntityListener; }
            void Shutdown() override
            {
                if (CGameEntitySystem* pEntitySystem = pluginApi.GetEntitySystem())
                    pEntitySystem->RemoveListenerEntity(&virtualhooks::entityListener);
            }
        } s_entityListener;

        class NetSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::Net; }
            // Results of the core's own sockets (HTTP through Beast). Here and not
            // in PostSimulate: Steam's HTTP results arrive while the server
            // hibernates, and Beast's must not wait for a player.
            void OnGameFrame(bool) override { net::RunFrame(); }
            void Shutdown() override { net::Shutdown(); }
        } s_net;

        class SourcePawnSystem final : public ISource2ModAppSystem
        {
            SystemId GetSystemId() const override { return SystemId::SourcePawn; }
            void Shutdown() override { sourcepawn::Shutdown(); }
            void RemoveAllForPlugin(PluginId id) override { sourcepawn::RemoveAllForPlugin(id); }
        } s_sourcePawn;
    }
}
