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
 * SourcePawn natives for source2mod/gamesystems.inc: IGameSystem (a game
 * system of the script's, on the core's game systems -- core/gamesystems.cpp),
 * GameSystemMessage and Source2ModGameSystems.
 *
 * An event's message is never handed to a script as a pointer: a
 * GameSystemMessage is how deep in the running handlers it is, and its natives
 * read the message off the core's stack of the ones being handled -- so it is
 * dead the moment its handler returns.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gamesystems.h"

#include <entity2/entitysystem.h>

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

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        // ---- IGameSystem -------------------------------------------------------------

        struct ScriptGameSystem : HandleObject
        {
            Source2ModGameSystemId id = 0;
            cell_t handle = 0;
        };

        HandleType SystemType()
        {
            static const HandleType t = Handles().CreateType("IGameSystem", [](void* p)
            {
                auto* s = static_cast<ScriptGameSystem*>(p);
                // Gone already when the script unloaded first.
                gamesystems::gameSystemsManager.Destroy(s->id);
                delete s;
            });
            return t;
        }

        ScriptGameSystem* SystemOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ScriptGameSystem*>(ReadHandle(ctx, handle, SystemType()));
        }

        // The messages of the events being handled, innermost last; a
        // GameSystemMessage is an index into it + 1.
        struct Message
        {
            GameSystemEvent event;
            const void* msg;
        };
        std::vector<Message> g_messages;

        const Message* MessageOf(IPluginContext* ctx, cell_t token)
        {
            if (token < 1 || token > static_cast<cell_t>(g_messages.size()))
            {
                ctx->ThrowNativeError("GameSystemMessage %d is not valid here (only inside its handler)", token);
                return nullptr;
            }
            return &g_messages[token - 1];
        }

        cell_t N_SystemNew(IPluginContext* ctx, const cell_t* params)
        {
            const PluginId owner = CallerId(ctx);
            auto* s = new ScriptGameSystem();
            s->id = gamesystems::gameSystemsManager.Create(owner, LocalString(ctx, params[1]));
            s->handle = s->id ? Handles().Create(SystemType(), s, owner) : 0;
            if (!s->handle)
            {
                gamesystems::gameSystemsManager.Destroy(s->id);
                delete s;
                return ctx->ThrowNativeError("Could not create the game system");
            }
            return s->handle;
        }

        cell_t N_SystemHook(IPluginContext* ctx, const cell_t* params)
        {
            ScriptGameSystem* s = SystemOf(ctx, params[1]);
            IPluginFunction* fn = s ? Function(ctx, params[3]) : nullptr;
            if (!fn)
                return 0;

            const auto event = static_cast<GameSystemEvent>(params[2]);
            const cell_t handle = s->handle;
            GameSystemHandler handler([fn, handle, event](const void* msg)
            {
                g_messages.push_back({ event, msg });
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(static_cast<cell_t>(event));
                args.PushCell(static_cast<cell_t>(g_messages.size()));
                Invoke(fn, args);
                g_messages.pop_back();
            });

            const Source2ModHookId id = gamesystems::gameSystemsManager.Hook(CallerId(ctx), s->id, event, std::move(handler));
            if (!id)
                return ctx->ThrowNativeError("Event %d cannot be hooked", params[2]);
            return id;
        }

        cell_t N_SystemUnhook(IPluginContext* ctx, const cell_t* params)
        {
            if (!SystemOf(ctx, params[1]))
                return 0;
            return gamesystems::gameSystemsManager.Unhook(params[2]);
        }

        cell_t N_SystemRegister(IPluginContext* ctx, const cell_t* params)
        {
            ScriptGameSystem* s = SystemOf(ctx, params[1]);
            return s && gamesystems::gameSystemsManager.Register(s->id);
        }

        cell_t N_SystemUnregister(IPluginContext* ctx, const cell_t* params)
        {
            ScriptGameSystem* s = SystemOf(ctx, params[1]);
            return s && gamesystems::gameSystemsManager.Unregister(s->id);
        }

        cell_t N_SystemIsRegistered(IPluginContext* ctx, const cell_t* params)
        {
            ScriptGameSystem* s = SystemOf(ctx, params[1]);
            return s && gamesystems::gameSystemsManager.IsRegistered(s->id);
        }

        // ---- GameSystemMessage -------------------------------------------------------

        cell_t N_MessageAddResource(IPluginContext* ctx, const cell_t* params)
        {
            const Message* m = MessageOf(ctx, params[1]);
            if (!m)
                return 0;

            IEntityResourceManifest* manifest = nullptr;
            if (m->event == GameSystemEvent::BuildGameSessionManifest)
                manifest = static_cast<const EventBuildGameSessionManifest_t*>(m->msg)->m_pResourceManifest;
            else if (m->event == GameSystemEvent::SpawnGroupPrecache)
                manifest = static_cast<const EventSpawnGroupPrecache_t*>(m->msg)->m_pManifest;
            else
                return ctx->ThrowNativeError("AddResource only works in BuildGameSessionManifest and SpawnGroupPrecache");

            if (manifest)
                manifest->AddResource(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_MessageSpawnGroupHandle(IPluginContext* ctx, const cell_t* params)
        {
            const Message* m = MessageOf(ctx, params[1]);
            if (!m)
                return 0;
            if (m->event == GameSystemEvent::SpawnGroupPrecache)
                return static_cast<cell_t>(static_cast<const EventSpawnGroupPrecache_t*>(m->msg)->m_SpawnGroupHandle);
            if (m->event == GameSystemEvent::SpawnGroupUncache)
                return static_cast<cell_t>(static_cast<const EventSpawnGroupUncache_t*>(m->msg)->m_SpawnGroupHandle);
            return ctx->ThrowNativeError("SpawnGroupHandle only exists in SpawnGroupPrecache and SpawnGroupUncache");
        }

        cell_t N_MessageGetSpawnGroupName(IPluginContext* ctx, const cell_t* params)
        {
            const Message* m = MessageOf(ctx, params[1]);
            if (!m)
                return 0;
            const char* name = nullptr;
            if (m->event == GameSystemEvent::SpawnGroupPrecache)
                name = static_cast<const EventSpawnGroupPrecache_t*>(m->msg)->m_SpawnGroupName.Get();
            else if (m->event == GameSystemEvent::SpawnGroupUncache)
                name = static_cast<const EventSpawnGroupUncache_t*>(m->msg)->m_SpawnGroupName.Get();
            else
                return ctx->ThrowNativeError("GetSpawnGroupName only exists in SpawnGroupPrecache and SpawnGroupUncache");
            return ToBuffer(ctx, params[2], params[3], name);
        }

        // ---- Source2ModGameSystems -------------------------------------------------

        cell_t N_GameSystemsIsAvailable(IPluginContext*, const cell_t*)
        {
            return gamesystems::gameSystemsManager.IsAvailable();
        }

        cell_t N_GameSystemsFindByName(IPluginContext* ctx, const cell_t* params)
        {
            return SchemaObjectForPointer(gamesystems::gameSystemsManager.FindByName(LocalString(ctx, params[1])));
        }

        const sp_nativeinfo_t g_gameSystemNatives[] = {
            { "IGameSystem.IGameSystem", N_SystemNew },
            { "IGameSystem.Hook", N_SystemHook },
            { "IGameSystem.Unhook", N_SystemUnhook },
            { "IGameSystem.Register", N_SystemRegister },
            { "IGameSystem.Unregister", N_SystemUnregister },
            { "IGameSystem.IsRegistered.get", N_SystemIsRegistered },
            { "GameSystemMessage.AddResource", N_MessageAddResource },
            { "GameSystemMessage.SpawnGroupHandle.get", N_MessageSpawnGroupHandle },
            { "GameSystemMessage.GetSpawnGroupName", N_MessageGetSpawnGroupName },
            { "Source2ModGameSystems.IsAvailable", N_GameSystemsIsAvailable },
            { "Source2ModGameSystems.FindByName", N_GameSystemsFindByName },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetGameSystemNatives()
    {
        return g_gameSystemNatives;
    }
}