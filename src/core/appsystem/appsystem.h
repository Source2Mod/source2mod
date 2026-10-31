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
#include <ISmmPlugin.h>
#include <playerslot.h>

#include <cstddef>

class CEntityInstance;

// Every part of the core with a lifetime derives from this and is found through
// it, so Load(), Unload() and the hooks say what happened instead of naming who
// has to hear about it. The shape comes from the engine's IAppSystem (Init and
// Shutdown in a known order), the self-registration from SourceMod's
// SMGlobalClass. Unlike that one the order is not the link order, which C++
// leaves open between files: it is written down in appsystem.cpp.
//
// Core-internal: plugins never see this.
namespace appsystem
{
    enum class SystemId
    {
        Scheduler, CoreConfig, CrashHandler, GameConfig, Globals, Addresses, GameSystems, Sounds, Patches,
        ConVarRegistration, Commands, SlotInput, Permissions, GeoIP, Cookies, Scripts, Unlock, InlineHooks,
        VirtualHooks, Events, GameHooks, DynamicHooks, Configs, SourcePawn, ConVars, HTTP, WebSockets, Net,
        MySQL, Maps, EntityListener, CustomHud, NetworkMessages, Transmit, Menus, Hooks, Entities, Clients,
        Translations, Targeting, Functions, PluginKHook, Modules, Voice,
        Count
    };

    class ISource2ModAppSystem
    {
    public:
        ISource2ModAppSystem();
        virtual ~ISource2ModAppSystem() = default;

        virtual SystemId GetSystemId() const = 0;

        /// False stops the load; @p error says why. What a server can run
        /// without reports nothing and answers true.
        virtual bool Init(char* error, size_t maxlen) { return true; }
        virtual void Shutdown() {}

        virtual void OnGameFrame(bool simulating) {}
        /// After the plugins' GameFrame hooks.
        virtual void OnGameFramePost() {}
        virtual void OnPostSimulate() {}
        virtual void OnLevelInit(const char* mapName) {}
        virtual void OnLevelShutdown() {}
        virtual void OnSteamAPIActivated() {}
        virtual void OnSteamAPIDeactivated() {}
        /// Ahead of the plugins' hooks.
        virtual void OnClientPutInServer(CPlayerSlot slot, int type, uint64 xuid) {}
        /// Ahead of the plugins' hooks.
        virtual void OnClientDisconnect(CPlayerSlot slot) {}
        /// After them: theirs is the last look at who the player was.
        virtual void OnClientDisconnectPost(CPlayerSlot slot) {}
        virtual void OnEntityDeleted(CEntityInstance* pEntity) {}
        /// Whatever plugin @p id left here holds code in its library.
        virtual void RemoveAllForPlugin(PluginId id) {}

    private:
        friend struct Registry;
        ISource2ModAppSystem* m_pNext;
    };

    bool InitAll(char* error, size_t maxlen);
    void ShutdownAll();

    void GameFrame(bool simulating);
    void GameFramePost();
    void PostSimulate();
    void LevelInit(const char* mapName);
    void LevelShutdown();
    void SteamAPIActivated();
    void SteamAPIDeactivated();
    void ClientPutInServer(CPlayerSlot slot, int type, uint64 xuid);
    void ClientDisconnect(CPlayerSlot slot);
    void ClientDisconnectPost(CPlayerSlot slot);
    void EntityDeleted(CEntityInstance* pEntity);
    void RemoveAllForPlugin(PluginId id);
}
