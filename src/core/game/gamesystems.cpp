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

#include "core/game/gamesystems.h"

#include "core/game/addresses.h"
#include "core/data/gameconfig.h"
#include "core/hooks/hookid.h"
#include "core/runtime/shared.h"
#include "utils/log.h"

#include <dynlibutils/module.hpp>
#include <igamesystemfactory.h>
#include <tier0/utlsymboltable.h>
#include <tier0/utlvector.h>

#include <cstring>

// The engine's factory list. The static is declared by the s2sdk header and
// defined by whichever binary uses it.
CBaseGameSystemFactory** CBaseGameSystemFactory::sm_pFirst = nullptr;

namespace gamesystems
{
    GameSystemsManager gameSystemsManager;

    namespace
    {
        constexpr const char* kHostName = "Source2Mod_GameSystem";

        // An entry of the engine's factory table, indexed by the factory name's
        // symbol: the factory, and whether its system is made (and active).
        struct FactoryEntry_t
        {
            IGameSystemFactory* m_pFactory;
            bool m_bCreated;
        };
        static_assert(sizeof(FactoryEntry_t) == 16);

        // An entry of the engine's active list: the system, its factory's
        // priority (the list is sorted by it at every map) and a field the
        // engine fills with 0x7FFFFFFF.
        struct ActiveSystem_t
        {
            IGameSystem* m_pSystem;
            int m_nPriority;
            int m_nUnknown;
        };
        static_assert(sizeof(ActiveSystem_t) == 16);

        using FactoryTable = CUtlVector<FactoryEntry_t>;
        using ActiveSystems = CUtlVector<ActiveSystem_t>;

        // A factory as the engine walks the factory list: CBaseGameSystemFactory's
        // layout, whose members s2sdk keeps private.
        struct FactoryLink
        {
            void* m_pVTable;
            FactoryLink* m_pNext;
            const char* m_pName;
        };

        // CGameSystemEventDispatcher: one listener list per event id (the
        // event's IGameSystem vtable slot).
        class IGameSystemEventDispatcher
        {
        public:
            virtual void RegisterListener(int id, IGameSystem* pSystem) = 0;
            virtual void UnregisterListener(IGameSystem* pSystem) = 0;

            CUtlVector<CUtlVector<IGameSystem*>>* m_funcListeners;
        };

        bool IsListening(IGameSystemEventDispatcher* dispatcher, int slot, IGameSystem* system)
        {
            auto* lists = dispatcher->m_funcListeners;
            if (!lists || slot >= lists->Count())
                return false;
            const auto& list = lists->Element(slot);
            for (int i = 0; i < list.Count(); ++i)
            {
                if (list.Element(i) == system)
                    return true;
            }
            return false;
        }

        // IGameSystem's slots: Init .. DoesGameSystemReallocate (0..62), the
        // destructor (two on Itanium) and YouForgot_DECLARE_GAME_SYSTEM.
#if defined(_WIN32)
        constexpr int kVTableSlots = 65;
#else
        constexpr int kVTableSlots = 66;
#endif
        constexpr int kFirstEventSlot = 3;
        constexpr int kLastEventSlot = 58;

        bool IsHookable(int slot)
        {
            switch (static_cast<GameSystemEvent>(slot))
            {
#define S2M_CASE(name) case GameSystemEvent::name:
                S2M_CASE(GameInit) S2M_CASE(GameShutdown) S2M_CASE(GamePostInit) S2M_CASE(GamePreShutdown)
                S2M_CASE(BuildGameSessionManifest) S2M_CASE(GameActivate) S2M_CASE(ClientFullySignedOn)
                S2M_CASE(ClientDisconnect) S2M_CASE(GameDeactivate) S2M_CASE(SpawnGroupPrecache)
                S2M_CASE(SpawnGroupUncache) S2M_CASE(PreSpawnGroupLoad) S2M_CASE(PostSpawnGroupLoad)
                S2M_CASE(PreSpawnGroupUnload) S2M_CASE(PostSpawnGroupUnload) S2M_CASE(ActiveSpawnGroupChanged)
                S2M_CASE(ServerPreEntityThink) S2M_CASE(ServerPostEntityThink) S2M_CASE(ServerPostPhysicsSimulate)
                S2M_CASE(ServerPreClientUpdate) S2M_CASE(ServerAdvanceTick) S2M_CASE(ServerGamePostSimulate)
                S2M_CASE(ServerPostAdvanceTick) S2M_CASE(ServerBeginAsyncPostTickWork)
                S2M_CASE(ServerGameBeginAsyncPostTickWork) S2M_CASE(ServerPreEndAsyncPostTickWork)
                S2M_CASE(ServerPostEndAsyncPostTickWork) S2M_CASE(GameFrameBoundary) S2M_CASE(SaveGame)
                S2M_CASE(RestoreGame) S2M_CASE(NewLevelPlayerConnect) S2M_CASE(WorldGroupCreated)
                S2M_CASE(WorldGroupDestroyed) S2M_CASE(ServerPrePackEntities)
#undef S2M_CASE
                    return true;
                default:
                    return false;
            }
        }

        uint8* Rip(uint8* displacement)
        {
            return displacement + 4 + *reinterpret_cast<int32*>(displacement);
        }

        uint8* FindSignature(DynLibUtils::CModule& mod, const char* key)
        {
            const std::string signature = gameconfig::gameConfig.GetSignature(key);
            if (signature.empty())
                return nullptr;
            return mod.FindPattern(DynLibUtils::ParsePattern(signature)).RCast<uint8*>();
        }
    }

    // The one game system the engine sees: every hookable event to the
    // registered systems' handlers.
    class HostGameSystem final : public CAutoGameSystem
    {
    public:
        HostGameSystem() : CAutoGameSystem(kHostName) {}

        DECLARE_GAME_SYSTEM();

#define S2M_GAMESYSTEM_EVENT(name) \
        GS_EVENT(name) { gameSystemsManager.Fire(GameSystemEvent::name, msg); }
        S2M_GAMESYSTEM_EVENT(GameInit)
        S2M_GAMESYSTEM_EVENT(GameShutdown)
        S2M_GAMESYSTEM_EVENT(GamePostInit)
        S2M_GAMESYSTEM_EVENT(GamePreShutdown)
        S2M_GAMESYSTEM_EVENT(BuildGameSessionManifest)
        S2M_GAMESYSTEM_EVENT(GameActivate)
        S2M_GAMESYSTEM_EVENT(ClientFullySignedOn)
        S2M_GAMESYSTEM_EVENT(ClientDisconnect)
        S2M_GAMESYSTEM_EVENT(GameDeactivate)
        S2M_GAMESYSTEM_EVENT(SpawnGroupPrecache)
        S2M_GAMESYSTEM_EVENT(SpawnGroupUncache)
        S2M_GAMESYSTEM_EVENT(PreSpawnGroupLoad)
        S2M_GAMESYSTEM_EVENT(PostSpawnGroupLoad)
        S2M_GAMESYSTEM_EVENT(PreSpawnGroupUnload)
        S2M_GAMESYSTEM_EVENT(PostSpawnGroupUnload)
        S2M_GAMESYSTEM_EVENT(ActiveSpawnGroupChanged)
        S2M_GAMESYSTEM_EVENT(ServerPreEntityThink)
        S2M_GAMESYSTEM_EVENT(ServerPostEntityThink)
        S2M_GAMESYSTEM_EVENT(ServerPostPhysicsSimulate)
        S2M_GAMESYSTEM_EVENT(ServerPreClientUpdate)
        S2M_GAMESYSTEM_EVENT(ServerAdvanceTick)
        S2M_GAMESYSTEM_EVENT(ServerGamePostSimulate)
        S2M_GAMESYSTEM_EVENT(ServerPostAdvanceTick)
        S2M_GAMESYSTEM_EVENT(ServerBeginAsyncPostTickWork)
        S2M_GAMESYSTEM_EVENT(ServerGameBeginAsyncPostTickWork)
        S2M_GAMESYSTEM_EVENT(ServerPreEndAsyncPostTickWork)
        S2M_GAMESYSTEM_EVENT(ServerPostEndAsyncPostTickWork)
        S2M_GAMESYSTEM_EVENT(GameFrameBoundary)
        S2M_GAMESYSTEM_EVENT(SaveGame)
        S2M_GAMESYSTEM_EVENT(RestoreGame)
        S2M_GAMESYSTEM_EVENT(NewLevelPlayerConnect)
        S2M_GAMESYSTEM_EVENT(WorldGroupCreated)
        S2M_GAMESYSTEM_EVENT(WorldGroupDestroyed)
        S2M_GAMESYSTEM_EVENT(ServerPrePackEntities)
#undef S2M_GAMESYSTEM_EVENT
    };

    // The host's factory, laid out like CBaseGameSystemFactory (vtable, next,
    // name) since the engine reads those two off every factory in the list. A
    // static one: the host lives as long as the core, and destroying it at a
    // map's end only means it is made again for the next.
    class HostFactory final : public IGameSystemFactory
    {
    public:
        explicit HostFactory(HostGameSystem* host) : m_pHost(host) {}

        bool Init() override { return true; }
        void PostInit() override {}
        void Shutdown() override {}
        IGameSystem* CreateGameSystem() override { return m_pHost; }
        void DestroyGameSystem(IGameSystem*) override {}
        bool ShouldAutoAdd() override { return true; }
        int GetPriority() override { return 0; }
        void SetGlobalPtr(void*) override {}
        bool IsReallocating() override { return false; }
        IGameSystem* GetStaticGameSystem() override { return m_pHost; }

        FactoryLink* m_pNext = nullptr;
        const char* m_pName = kHostName;
        HostGameSystem* m_pHost;
    };

    GameSystemsManager::GameSystemsManager() = default;
    GameSystemsManager::~GameSystemsManager() = default;

    bool GameSystemsManager::Init()
    {
        if (IsAvailable())
            return true;

        auto& mod = addresses::source2modAddresses.GetOrLoadModule(g_pSource2Server);

        // IGameSystem::InitAllSystems: the factory list, the name -> index
        // symbol table, the factory table.
        uint8* pFirst = FindSignature(mod, "IGameSystem::InitAllSystems->pFirst");
        uint8* pSymbols = FindSignature(mod, "IGameSystem::InitAllSystems->SymbolTable");
        uint8* pFactories = FindSignature(mod, "IGameSystem::InitAllSystems->Factories");
        // LoopInitAllSystems: the active list, the event dispatcher.
        uint8* pActive = FindSignature(mod, "IGameSystem::LoopInitAllSystems->ActiveSystems");
        uint8* pDispatcher = FindSignature(mod, "IGameSystem::LoopInitAllSystems->sm_pEventDispatcher");

        DynLibUtils::CModule libserver(g_pSource2Server);
        void** pDefault = libserver.GetVirtualTableByName("CDontUseThisGameSystem").RCast<void**>();

        if (pFirst)
        {
            m_ppFirst = reinterpret_cast<CBaseGameSystemFactory**>(Rip(pFirst + 3));
            CBaseGameSystemFactory::sm_pFirst = m_ppFirst;
        }

        if (!pFirst || !pSymbols || !pFactories || !pActive || !pDispatcher || !pDefault)
        {
            FP_ERROR("Game systems unavailable, not found:{}{}{}{}{}{}", pFirst ? "" : " factory list", pSymbols ? "" : " symbol table",
                     pFactories ? "" : " factory table", pActive ? "" : " active list", pDispatcher ? "" : " event dispatcher",
                     pDefault ? "" : " CDontUseThisGameSystem");
            return false;
        }

        auto* factories = reinterpret_cast<FactoryTable*>(Rip(pFactories + 2));
        auto* active = reinterpret_cast<ActiveSystems*>(Rip(pActive + 3));
        if (factories->Count() < 0 || factories->Count() > 4096 || active->Count() < 0 || active->Count() > 4096)
        {
            FP_ERROR("Game systems unavailable: the engine's lists do not look like lists ({} factories, {} active).",
                     factories->Count(), active->Count());
            return false;
        }

        m_pSymbols = Rip(pSymbols + 3);
        m_pFactories = factories;
        m_pActive = active;
        m_ppDispatcher = reinterpret_cast<void**>(Rip(pDispatcher + 13));
        m_pDefaultVTable = pDefault;

        // The host, with its own copy of its vtable: UpdateHost() points the
        // slots nobody hooks at CDontUseThisGameSystem's.
        m_host = std::make_unique<HostGameSystem>();
        m_factory = std::make_unique<HostFactory>(m_host.get());
        m_hostOwnVTable = *reinterpret_cast<void***>(m_host.get());
        m_hostVTable = std::make_unique<void*[]>(kVTableSlots);
        std::memcpy(m_hostVTable.get(), m_hostOwnVTable, sizeof(void*) * kVTableSlots);
        *reinterpret_cast<void***>(m_host.get()) = m_hostVTable.get();
        UpdateHost();

        m_hostAttached = AttachHost();
        FP_DEBUG("Game systems {} ({} factories, {} active).", m_hostAttached ? "ready" : "unavailable", factories->Count(), active->Count());
        return m_hostAttached;
    }

    void* GameSystemsManager::FactoryEntry()
    {
        auto* symbols = static_cast<CUtlSymbolTable*>(m_pSymbols);
        auto* factories = static_cast<FactoryTable*>(m_pFactories);
        const CUtlSymbol symbol = symbols->Find(kHostName);
        if (!symbol.IsValid() || static_cast<int>(static_cast<UtlSymId_t>(symbol)) >= factories->Count())
            return nullptr;
        return &factories->Element(static_cast<UtlSymId_t>(symbol));
    }

    bool GameSystemsManager::AttachHost()
    {
        auto* factories = static_cast<FactoryTable*>(m_pFactories);
        auto* entry = static_cast<FactoryEntry_t*>(FactoryEntry());

        if (!entry && factories->Count() > 0)
        {
            // The engine built its factory table already, without the host.
            FP_WARN("Source2Mod was loaded after the server started: plugin game systems work from the next server start.");
            return false;
        }

        // Into the factory list: the engine builds its table from it (when the
        // core loads with the server), and FindByName walks it.
        auto* link = reinterpret_cast<FactoryLink*>(m_factory.get());
        link->m_pNext = reinterpret_cast<FactoryLink*>(*m_ppFirst);
        *m_ppFirst = reinterpret_cast<CBaseGameSystemFactory*>(m_factory.get());

        if (entry)
        {
            // Loaded again: the table entry is the host's from the last time.
            entry->m_pFactory = m_factory.get();
            entry->m_bCreated = false;
            if (m_ppDispatcher && *m_ppDispatcher)
            {
                // A map runs: what LoopInitAllSystems would do for it.
                entry->m_bCreated = true;
                static_cast<ActiveSystems*>(m_pActive)->AddToTail({ m_host.get(), 0, 0x7FFFFFFF });
            }
        }
        return true;
    }

    bool GameSystemsManager::IsAvailable() const
    {
        return m_hostAttached;
    }

    IGameSystem* GameSystemsManager::FindByName(const char* name)
    {
        if (!m_ppFirst || !name)
            return nullptr;
        return CBaseGameSystemFactory::GetGlobalPtrByName(name);
    }

    GameSystemsManager::System* GameSystemsManager::Find(Source2ModGameSystemId id)
    {
        auto it = m_systems.find(id);
        return it == m_systems.end() ? nullptr : it->second.get();
    }

    bool GameSystemsManager::IsHooked(int slot) const
    {
        for (const auto& [id, sys] : m_systems)
        {
            if (!sys->registered)
                continue;
            auto it = sys->handlers.find(slot);
            if (it != sys->handlers.end() && !it->second.empty())
                return true;
        }
        return false;
    }

    void GameSystemsManager::UpdateHost()
    {
        if (!m_host)
            return;

        auto* dispatcher = m_hostAttached && m_ppDispatcher ? static_cast<IGameSystemEventDispatcher*>(*m_ppDispatcher) : nullptr;
        for (int slot = kFirstEventSlot; slot <= kLastEventSlot; ++slot)
        {
            const bool hooked = IsHooked(slot);
            m_hostVTable[slot] = hooked ? m_hostOwnVTable[slot] : m_pDefaultVTable[slot];

            // The running map's lists were made already: a newly hooked event
            // goes in now (appending is safe while the engine walks one). One
            // nobody hooks any more stays until the map ends -- a no-op.
            if (hooked && dispatcher && !IsListening(dispatcher, slot, m_host.get()))
                dispatcher->RegisterListener(slot, m_host.get());
        }
    }

    Source2ModGameSystemId GameSystemsManager::Create(PluginId owner, const char* name)
    {
        auto sys = std::make_unique<System>();
        sys->id = ++m_nextId;
        sys->owner = owner;
        sys->name = name && *name ? name : "unnamed";
        const Source2ModGameSystemId id = sys->id;
        m_systems.emplace(id, std::move(sys));
        return id;
    }

    Source2ModHookId GameSystemsManager::Hook(Source2ModGameSystemId system, GameSystemEvent event, GameSystemHandler handler)
    {
        return Hook(hookid::OwnerOfHandler(handler), system, event, std::move(handler));
    }

    Source2ModHookId GameSystemsManager::Hook(PluginId owner, Source2ModGameSystemId system, GameSystemEvent event, GameSystemHandler handler)
    {
        System* sys = Find(system);
        const int slot = static_cast<int>(event);
        if (!sys || !handler || !IsHookable(slot))
            return 0;

        const Source2ModHookId id = hookid::Next();
        sys->handlers[slot].push_back({ id, owner, std::move(handler) });
        m_hooks[id] = system;
        if (sys->registered)
            UpdateHost();
        return id;
    }

    bool GameSystemsManager::Unhook(Source2ModHookId hook)
    {
        auto it = m_hooks.find(hook);
        if (it == m_hooks.end())
            return false;

        System* sys = Find(it->second);
        m_hooks.erase(it);
        if (!sys)
            return false;

        for (auto& [slot, list] : sys->handlers)
        {
            for (auto entry = list.begin(); entry != list.end(); ++entry)
            {
                if (entry->id != hook)
                    continue;
                list.erase(entry);
                UpdateHost();
                return true;
            }
        }
        return false;
    }

    bool GameSystemsManager::Register(Source2ModGameSystemId system)
    {
        System* sys = Find(system);
        if (!sys || !IsAvailable())
            return false;
        if (!sys->registered)
        {
            sys->registered = true;
            UpdateHost();
        }
        return true;
    }

    bool GameSystemsManager::Unregister(Source2ModGameSystemId system)
    {
        System* sys = Find(system);
        if (!sys)
            return false;
        if (sys->registered)
        {
            sys->registered = false;
            UpdateHost();
        }
        return true;
    }

    bool GameSystemsManager::IsRegistered(Source2ModGameSystemId system)
    {
        System* sys = Find(system);
        return sys && sys->registered;
    }

    bool GameSystemsManager::Destroy(Source2ModGameSystemId system)
    {
        auto it = m_systems.find(system);
        if (it == m_systems.end())
            return false;

        for (auto& [slot, list] : it->second->handlers)
        {
            for (auto& entry : list)
                m_hooks.erase(entry.id);
        }
        m_systems.erase(it);
        UpdateHost();
        return true;
    }

    void GameSystemsManager::Fire(GameSystemEvent event, const void* msg)
    {
        const int slot = static_cast<int>(event);

        // A handler may hook, unhook or destroy while these run: copies, and
        // each one checked to still be hooked.
        std::vector<std::pair<Source2ModHookId, GameSystemHandler>> handlers;
        for (const auto& [id, sys] : m_systems)
        {
            if (!sys->registered)
                continue;
            auto it = sys->handlers.find(slot);
            if (it == sys->handlers.end())
                continue;
            for (const HandlerEntry& entry : it->second)
                handlers.emplace_back(entry.id, entry.handler);
        }

        for (const auto& [id, handler] : handlers)
        {
            if (m_hooks.count(id))
                handler(msg);
        }
    }

    void GameSystemsManager::RemoveAllForPlugin(PluginId owner)
    {
        for (auto it = m_systems.begin(); it != m_systems.end();)
        {
            System& sys = *it->second;
            for (auto& [slot, list] : sys.handlers)
            {
                for (auto entry = list.begin(); entry != list.end();)
                {
                    if (sys.owner == owner || entry->owner == owner)
                    {
                        m_hooks.erase(entry->id);
                        entry = list.erase(entry);
                    }
                    else
                        ++entry;
                }
            }
            it = sys.owner == owner ? m_systems.erase(it) : std::next(it);
        }
        UpdateHost();
    }

    void GameSystemsManager::Shutdown()
    {
        m_systems.clear();
        m_hooks.clear();

        if (!m_hostAttached)
            return;
        m_hostAttached = false;

        // Out of the running map: no event runs while the core unloads.
        auto* active = static_cast<ActiveSystems*>(m_pActive);
        for (int i = active->Count() - 1; i >= 0; --i)
        {
            if (active->Element(i).m_pSystem == m_host.get())
                active->Remove(i);
        }
        if (m_ppDispatcher && *m_ppDispatcher)
            static_cast<IGameSystemEventDispatcher*>(*m_ppDispatcher)->UnregisterListener(m_host.get());

        // Out of the factory list.
        for (auto** pp = reinterpret_cast<FactoryLink**>(m_ppFirst); *pp; pp = &(*pp)->m_pNext)
        {
            if (*pp == reinterpret_cast<FactoryLink*>(m_factory.get()))
            {
                *pp = (*pp)->m_pNext;
                break;
            }
        }

        // The table entry stays (the engine has no way to take one out). Marked
        // created, no map adds or destroys it; on one of the engine's own
        // reallocating factories, whose Init/Shutdown do nothing, the exit's
        // ShutdownAllSystems calls nothing of the core's.
        if (auto* entry = static_cast<FactoryEntry_t*>(FactoryEntry()))
        {
            IGameSystemFactory* stand_in = nullptr;
            for (auto* f = reinterpret_cast<FactoryLink*>(*m_ppFirst); f && !stand_in; f = f->m_pNext)
            {
                auto* factory = reinterpret_cast<IGameSystemFactory*>(f);
                if (factory->IsReallocating())
                    stand_in = factory;
            }
            if (stand_in)
                entry->m_pFactory = stand_in;
            else
                FP_ERROR("No reallocating game system factory to leave in Source2Mod's place; the server may crash on exit.");
            entry->m_bCreated = true;
        }

        *reinterpret_cast<void***>(m_host.get()) = m_hostOwnVTable;
        m_factory.reset();
        m_host.reset();
    }
}