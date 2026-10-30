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
#include "core/appsystem/appsystem.h"
#include "source2mod/ISource2ModGameSystems.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class CBaseGameSystemFactory;

namespace gamesystems {
    class HostGameSystem;
    class HostFactory;

    /**
     * The game systems plugins make (ISource2ModGameSystems).
     *
     * The engine only ever sees one game system of Source2Mod's, the host
     * ("Source2Mod_GameSystem"), with a factory of the core's that goes into the
     * engine's factory list when the core loads with the server -- so the engine
     * creates, destroys and re-creates it with every map like its own. A plugin's
     * system is the core's bookkeeping: its handlers, run by the host. The host
     * overrides (has its own function in the vtable slot of) exactly the events
     * some registered system hooks; the rest are CDontUseThisGameSystem's, which
     * is how the engine tells that a system has no handler for an event.
     *
     * What the engine keeps (IDA 2026-10-25, IGameSystem::InitAllSystems /
     * LoopInitAllSystems / the map's destroy loop / ShutdownAllSystems):
     * a factory table built once from the factory list ({factory, created}, by
     * name through a symbol table); per map, an active list of the systems the
     * factories made, a dispatcher list per event; at the map's end every active
     * system goes back through its factory (found by name) and all the lists
     * are emptied; at exit every factory's Shutdown() runs.
     *
     * A plugin unloading takes nothing out of the engine. The core unloading
     * takes the host out of the active and dispatcher lists, and leaves its
     * table entry marked created on one of the engine's own reallocating
     * factories (whose Shutdown does nothing), which the engine then never
     * touches; a core loaded again takes the entry back by name.
     */
    class GameSystemsManager final : public ISource2ModGameSystems, public appsystem::ISource2ModAppSystem {
    public:
        appsystem::SystemId GetSystemId() const override { return appsystem::SystemId::GameSystems; }
        // Not fatal: a failed scan only means plugins cannot register game
        // systems, which most do not. ISource2ModGameSystems::IsAvailable() says so.
        bool Init(char*, size_t) override { Init(); return true; }

        GameSystemsManager();
        ~GameSystemsManager();

        bool IsAvailable() const override;
        IGameSystem* FindByName(const char* name) override;
        Source2ModGameSystemId Create(const char* name) override { return Create(PluginId(0), name); }
        Source2ModGameSystemId Create(PluginId owner, const char* name);
        Source2ModHookId Hook(Source2ModGameSystemId system, GameSystemEvent event, GameSystemHandler handler) override;
        bool Unhook(Source2ModHookId hook) override;
        bool Register(Source2ModGameSystemId system) override;
        bool Unregister(Source2ModGameSystemId system) override;
        bool IsRegistered(Source2ModGameSystemId system) override;
        bool Destroy(Source2ModGameSystemId system) override;

    public:
        /// Hook() for a handler the core makes for someone else (SourcePawn).
        Source2ModHookId Hook(PluginId owner, Source2ModGameSystemId system, GameSystemEvent event, GameSystemHandler handler);

        /// Resolves the engine's lists and puts the host in. Called once, after
        /// the game config is loaded.
        bool Init();

        /// Takes the host out of the engine (the core unloads).
        void Shutdown() override;

        /// The systems and handlers of a plugin that unloads.
        void RemoveAllForPlugin(PluginId owner) override;

        /// From the host: one event, to the registered systems' handlers.
        void Fire(GameSystemEvent event, const void* msg);

    private:
        struct HandlerEntry
        {
            Source2ModHookId id;
            PluginId owner;
            GameSystemHandler handler;
        };

        struct System
        {
            Source2ModGameSystemId id = 0;
            PluginId owner = 0;
            std::string name;
            std::map<int, std::vector<HandlerEntry>> handlers;  // by event slot
            bool registered = false;
        };

        System* Find(Source2ModGameSystemId id);
        bool IsHooked(int slot) const;
        void UpdateHost();
        bool AttachHost();
        void* FactoryEntry();

        CBaseGameSystemFactory** m_ppFirst = nullptr;
        void* m_pSymbols = nullptr;              // CUtlSymbolTable: factory name -> table index
        void* m_pFactories = nullptr;            // CUtlVector<FactoryEntry_t>
        void* m_pActive = nullptr;               // CUtlVector<ActiveSystem_t>
        void** m_ppDispatcher = nullptr;         // IGameSystem::sm_pEventDispatcher
        void** m_pDefaultVTable = nullptr;       // CDontUseThisGameSystem's

        std::unique_ptr<HostGameSystem> m_host;
        std::unique_ptr<HostFactory> m_factory;
        void** m_hostOwnVTable = nullptr;
        std::unique_ptr<void*[]> m_hostVTable;
        bool m_hostAttached = false;

        std::map<Source2ModGameSystemId, std::unique_ptr<System>> m_systems;
        std::map<Source2ModHookId, Source2ModGameSystemId> m_hooks;
        Source2ModGameSystemId m_nextId = 0;
    };

    extern GameSystemsManager gameSystemsManager;
}