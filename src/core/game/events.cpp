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
#include "core/game/events.h"
#include "core/ui/slotinput.h"
#include "core/game/clients.h"
#include "core/game/gamesystems.h"

#include "core/hooks/hookid.h"
#include "core/runtime/shared.h"
#include "core/runtime/slowguard.h"
#include "utils/log.h"
#include "core/hooks/virtualhooks.h"
#include "sdk/game/shared/gameeventmanager.h"
#include "sdk/game/shared/source1legacygameeventgamesystem.h"

namespace events {
    // The engine's manager is a CGameEventManager (sdk/game/shared/gameeventmanager.h).
    static CGameEventManager* GameEventManager()
    {
        return static_cast<CGameEventManager*>(shared::g_pGameEventManager);
    }

    // The game system that keeps one listener proxy per client. Its factory
    // reallocates it, so it is looked up each time rather than kept.
    static CSource1LegacyGameEventGameSystem* LegacyGameEventSystem()
    {
        return static_cast<CSource1LegacyGameEventGameSystem*>(
            gamesystems::gameSystemsManager.FindByName("CSource1LegacyGameEventGameSystem"));
    }

    static CGameEventDescriptor* FindDescriptor(const char* name)
    {
        CGameEventManager* manager = GameEventManager();
        return manager ? manager->GetEventDescriptor(name) : nullptr;
    }

    static std::unordered_map<std::string, std::vector<EventEntry> > gameEvents;
    static EventListener eventListener;

    EventManager eventManager;

    static void RegisterListenerIfNeeded(const std::string& name)
    {
        if (!shared::g_pGameEventManager)
            return;

        if (!shared::g_pGameEventManager->FindListener(&eventListener, name.c_str()))
        {
            shared::g_pGameEventManager->AddListener(&eventListener, name.c_str(), true);
        }
    }

    Source2ModEventHookId EventManager::HookGameEvent(PluginId owner, const char* name, GameEventHandler handler, bool post)
    {
        // 0 for an event the game does not have: a listener on a name that
        // never fires would only sit there.
        if (!name || !*name || (shared::g_pGameEventManager && !shared::g_pGameEventManager->HasEventDescriptor(name)))
            return 0;

        const Source2ModHookId id = hookid::Next();
        gameEvents[name].push_back({ owner, std::move(handler), post, id });
        RegisterListenerIfNeeded(name);
        return id;
    }

    bool EventManager::UnhookGameEvent(PluginId owner, const char* name, const GameEventHandler& handler, bool post)
    {
        if (!handler.HasIdentity())
            return false;

        auto it = gameEvents.find(name);
        if (it == gameEvents.end())
            return false;

        const bool found = std::erase_if(it->second, [owner, post, &handler](const EventEntry& e)
        {
            return e.owner == owner && e.post == post && handler.SameAs(e.handler);
        }) > 0;

        if (it->second.empty())
            gameEvents.erase(it);
        return found;
    }

    bool EventManager::Unhook(Source2ModHookId id)
    {
        for (auto it = gameEvents.begin(); it != gameEvents.end(); ++it)
        {
            if (std::erase_if(it->second, [id](const EventEntry& e) { return e.id == id; }) > 0)
            {
                if (it->second.empty())
                    gameEvents.erase(it);
                return true;
            }
        }
        return false;
    }

    void EventManager::FireEventToClient(CPlayerSlot slot, IGameEvent* event)
    {
        clients::clientsManager.FireEventToClient(slot, event);
    }

    int EventManager::GetEventCount()
    {
        CGameEventManager* manager = GameEventManager();
        return manager ? manager->GetEventCount() : 0;
    }

    const char* EventManager::GetEventName(int eventId)
    {
        CGameEventManager* manager = GameEventManager();
        return manager ? manager->GetEventName(manager->GetEventDescriptor(eventId)) : nullptr;
    }

    int EventManager::GetEventID(const char* pchName)
    {
        CGameEventDescriptor* descriptor = FindDescriptor(pchName);
        return descriptor ? descriptor->m_nEventId : -1;
    }

    int EventManager::GetEventListenerCount(const char* pchName, Source2ModEventListenerType type)
    {
        CGameEventDescriptor* descriptor = FindDescriptor(pchName);
        if (!descriptor)
            return -1;

        if (type == Source2ModEventListenerType::Any)
            return descriptor->m_Listeners.Count();

        int count = 0;
        for (const CGameEventCallback* callback : descriptor->m_Listeners)
        {
            if (callback && callback->m_nListenerType == static_cast<int>(type))
                count++;
        }
        return count;
    }

    bool EventManager::IsClientListening(CPlayerSlot slot, const char* pchName)
    {
        CSource1LegacyGameEventGameSystem* legacySystem = LegacyGameEventSystem();
        auto* proxy = legacySystem ? legacySystem->GetClientProxy(slot) : nullptr;
        CGameEventDescriptor* descriptor = proxy ? FindDescriptor(pchName) : nullptr;
        if (!descriptor)
            return false;

        // The client's listen bits make its proxy a ClientStub listener of each event they name
        for (const CGameEventCallback* callback : descriptor->m_Listeners)
        {
            if (callback && callback->m_nListenerType == CGameEventManager::CLIENTSTUB && callback->m_pCallback == proxy)
                return true;
        }
        return false;
    }

    const char* EventManager::GetEventFile(const char* pchName)
    {
        CGameEventDescriptor* descriptor = FindDescriptor(pchName);
        return descriptor ? descriptor->m_pszFilename : nullptr;
    }

    bool EventManager::IsEventLocal(const char* pchName)
    {
        CGameEventDescriptor* descriptor = FindDescriptor(pchName);
        return descriptor && descriptor->m_bLocal;
    }

    bool EventManager::IsEventReliable(const char* pchName)
    {
        CGameEventDescriptor* descriptor = FindDescriptor(pchName);
        return descriptor && descriptor->m_bReliable;
    }

    void EventManager::Shutdown() { DestructEvents(); }

    void EventManager::RemoveAllForPlugin(PluginId id)
    {
        for (auto it = gameEvents.begin(); it != gameEvents.end(); )
        {
            auto& vec = it->second;

            std::erase_if(vec, [id](const EventEntry& e) {
                return e.owner == id;
            });

            if (vec.empty())
                it = gameEvents.erase(it);
            else
                ++it;
        }
    }

    void EventListener::FireGameEvent(IGameEvent* pEvent) {}

    void InitEvents()
    {
        if (!shared::g_pGameEventManager)
            return;

        for (const auto& [name, _] : gameEvents)
        {
            RegisterListenerIfNeeded(name);
        }
    }

    void DestructEvents()
    {
        if (shared::g_pGameEventManager)
            shared::g_pGameEventManager->RemoveListener(&eventListener);
        gameEvents.clear();
    }

    bool DispatchGameEvent(IGameEvent *event, bool post, bool &dontBroadcast) {
        const char *name = event->GetName();
        auto it = gameEvents.find(name);
        if (it == gameEvents.end())
            return true;

        // The menus' fakes stay out of m_hMyWeapons for the plugins (slotinput.h)
        slotinput::HiddenFromPlugins hidden;

        for (const auto &hook: it->second) {
            if (hook.post != post)
                continue;

            Action result;
            {
                ::slow::Guard slowGuard("game event handler", name, hook.owner);
                result = hook.handler(event, post, dontBroadcast);
            }

            if (result == Action::Supersede)
                return false;
        }

        return true;
    }
}
