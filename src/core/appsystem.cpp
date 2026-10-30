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
#include "appsystem.h"

#include "utils/log.h"

namespace appsystem
{
    namespace
    {
        // Each one before whatever reads it. Scheduler: nothing else needs it yet,
        // but its clocks are zero from here on.
        constexpr SystemId kInitOrder[] = {
            SystemId::Scheduler,
            SystemId::CoreConfig,
            // As soon as the config allows and before anything else: whatever
            // dies below, plugins included, is caught and reported next start.
            SystemId::CrashHandler,
            SystemId::GameConfig,
            // Before anything can reach SDK code that reads them.
            SystemId::Globals,
            SystemId::Addresses,
            SystemId::GameSystems,
            SystemId::Sounds,
            SystemId::Patches,
            // Before Commands: a ConCommand made before ConVar_Register goes on
            // tier1's registration list, and ConVar_Unregister walks that list.
            // The core's commands are heap objects deleted first, so the walk
            // would read freed memory (SIGSEGV on unload).
            SystemId::ConVarRegistration,
            SystemId::Commands,
            SystemId::SlotInput,
            // Before plugins load: their Load() may already grant and check.
            SystemId::Permissions,
            SystemId::GeoIP,
            SystemId::Cookies,
            SystemId::Scripts,
            SystemId::Unlock,
            SystemId::InlineHooks,
            SystemId::VirtualHooks,
        };

        // Not the reverse of the above: what matters here is who still needs whom
        // while going down, and plugins are gone by the time this runs.
        constexpr SystemId kShutdownOrder[] = {
            SystemId::Commands,
            SystemId::SlotInput,
            SystemId::Events,
            // None of the hooked game functions is on the stack here, so these
            // come out synchronously like the rest.
            SystemId::GameHooks,
            SystemId::DynamicHooks,
            SystemId::InlineHooks,
            SystemId::VirtualHooks,
            SystemId::Scripts,
            // Before the scheduler: it kills its poll timer there.
            SystemId::Configs,
            SystemId::Scheduler,
            // After the scheduler: a next-frame close still holding a script
            // runtime drops it there, and the runtimes have to go before the VM.
            SystemId::SourcePawn,
            SystemId::GameSystems,
            // Takes the engine-level change callback back out with it.
            SystemId::ConVars,
            SystemId::HTTP,
            SystemId::WebSockets,
            // Joins the IO thread: after HTTP and the WebSockets, whose
            // connections run on it.
            SystemId::Net,
            // Takes the Steam ticket callback back out.
            SystemId::Permissions,
            // Its last writes go out through the SQL layer, so before that.
            SystemId::Cookies,
            // Joins the worker threads. They run code from this library, so
            // leaving one running is a crash the moment Metamod unmaps it.
            SystemId::MySQL,
            SystemId::Maps,
            SystemId::GeoIP,
            SystemId::EntityListener,
            SystemId::ConVarRegistration,
            // Last: a crash anywhere in the teardown above is still worth a dump.
            SystemId::CrashHandler,
        };

        // Timers and next-frame tasks (Scheduler) run callbacks inside the library
        // about to be closed; so do most of the others. SourcePawn last.
        constexpr SystemId kPluginRemovalOrder[] = {
            SystemId::Events, SystemId::Commands, SystemId::Configs, SystemId::CustomHud, SystemId::ConVars,
            SystemId::NetworkMessages, SystemId::Sounds, SystemId::Transmit, SystemId::GameSystems,
            SystemId::Scripts, SystemId::Scheduler, SystemId::HTTP, SystemId::WebSockets, SystemId::MySQL,
            SystemId::Menus, SystemId::Hooks, SystemId::Entities, SystemId::Permissions, SystemId::Cookies,
            SystemId::Clients, SystemId::Translations, SystemId::Targeting, SystemId::Functions,
            SystemId::SourcePawn,
        };

        // Who hears an event first. HTTP before the crash handler: the report
        // waiting for Steam goes out through it. The crash handler first of the
        // frame: a signal handler someone replaced goes back regardless. Menus
        // before the slot keys: the key mask of the page now on screen.
        constexpr SystemId kEventOrder[] = {
            SystemId::HTTP, SystemId::CrashHandler, SystemId::Scheduler, SystemId::CustomHud, SystemId::Sounds,
            SystemId::Transmit, SystemId::Menus, SystemId::SlotInput, SystemId::Scripts, SystemId::Maps,
            SystemId::Permissions, SystemId::Cookies, SystemId::Clients, SystemId::Net, SystemId::DynamicHooks,
            SystemId::MySQL,
        };
    }

    struct Registry
    {
        // Constant-initialized: systems register during static construction,
        // in whatever order the linker chose.
        static inline ISource2ModAppSystem* s_pHead = nullptr;
        static inline ISource2ModAppSystem* s_systems[static_cast<int>(SystemId::Count)] = {};
        static inline bool s_bResolved = false;

        static void Add(ISource2ModAppSystem* pSystem)
        {
            pSystem->m_pNext = s_pHead;
            s_pHead = pSystem;
        }

        static ISource2ModAppSystem* Get(SystemId id)
        {
            // Not from the constructor: the derived class is not there yet.
            if (!s_bResolved)
            {
                for (auto* p = s_pHead; p; p = p->m_pNext)
                    s_systems[static_cast<int>(p->GetSystemId())] = p;
                s_bResolved = true;
            }
            return s_systems[static_cast<int>(id)];
        }
    };

    namespace
    {
        template <size_t N, typename F>
        void Each(const SystemId (&order)[N], F&& call)
        {
            for (SystemId id : order)
            {
                if (auto* pSystem = Registry::Get(id))
                    call(*pSystem);
            }
        }
    }

    ISource2ModAppSystem::ISource2ModAppSystem() { Registry::Add(this); }

    bool InitAll(char* error, size_t maxlen)
    {
        for (SystemId id : kInitOrder)
        {
            auto* pSystem = Registry::Get(id);
            if (pSystem && !pSystem->Init(error, maxlen))
                return false;
        }
        return true;
    }

    void ShutdownAll() { Each(kShutdownOrder, [](auto& s) { s.Shutdown(); }); }

    void GameFrame(bool simulating) { Each(kEventOrder, [=](auto& s) { s.OnGameFrame(simulating); }); }
    void GameFramePost() { Each(kEventOrder, [](auto& s) { s.OnGameFramePost(); }); }
    void PostSimulate() { Each(kEventOrder, [](auto& s) { s.OnPostSimulate(); }); }
    void LevelInit(const char* mapName) { Each(kEventOrder, [=](auto& s) { s.OnLevelInit(mapName); }); }
    void LevelShutdown() { Each(kEventOrder, [](auto& s) { s.OnLevelShutdown(); }); }
    void SteamAPIActivated() { Each(kEventOrder, [](auto& s) { s.OnSteamAPIActivated(); }); }
    void SteamAPIDeactivated() { Each(kEventOrder, [](auto& s) { s.OnSteamAPIDeactivated(); }); }
    void ClientPutInServer(CPlayerSlot slot, int type, uint64 xuid) { Each(kEventOrder, [=](auto& s) { s.OnClientPutInServer(slot, type, xuid); }); }
    void ClientDisconnect(CPlayerSlot slot) { Each(kEventOrder, [=](auto& s) { s.OnClientDisconnect(slot); }); }
    void ClientDisconnectPost(CPlayerSlot slot) { Each(kEventOrder, [=](auto& s) { s.OnClientDisconnectPost(slot); }); }
    void EntityDeleted(CEntityInstance* pEntity) { Each(kEventOrder, [=](auto& s) { s.OnEntityDeleted(pEntity); }); }
    void RemoveAllForPlugin(PluginId id) { Each(kPluginRemovalOrder, [=](auto& s) { s.RemoveAllForPlugin(id); }); }
}
