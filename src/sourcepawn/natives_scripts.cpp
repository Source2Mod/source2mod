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
 * SourcePawn natives for source2mod/scripts.inc (cs_script) on the core's
 * scripts (core/scripts.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/scripts.h"

#include <source2mod/sdk/schema/classes/CBaseEntity.h>
#include <source2mod/sdk/schema/classes/CCSCustomHudLayout.h>
#include <source2mod/sdk/schema/classes/CCSPlayerController.h>

#include <sp_callable.h>

#include <map>
#include <string>

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

        template <typename T>
        T* TypedEntityArg(IPluginContext* ctx, cell_t entity)
        {
            return static_cast<T*>(EntityArg(ctx, entity));
        }

        // ---- Source2ModScripts ---------------------------------------------------------

        std::map<Source2ModHookId, PluginId> g_scriptHooks;

        cell_t N_ScriptsCanRunSource(IPluginContext*, const cell_t*)
        {
            return scripts::scriptsManager.CanRunSource();
        }

        // LoadAsset(name, asset, persistent)
        cell_t N_ScriptsLoadAsset(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.LoadAsset(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]), params[3] != 0));
        }

        // RunSource(name, virtualPath, source, persistent)
        cell_t N_ScriptsRunSource(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.RunSource(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]),
                                                             LocalString(ctx, params[3]), params[4] != 0));
        }

        // RunFile(name, filePath, persistent)
        cell_t N_ScriptsRunFile(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.RunFile(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]), params[3] != 0));
        }

        cell_t N_ScriptsReload(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.Reload(LocalString(ctx, params[1])));
        }

        cell_t N_ScriptsRemove(IPluginContext* ctx, const cell_t* params)
        {
            scripts::scriptsManager.Remove(LocalString(ctx, params[1]));
            return 0;
        }

        cell_t N_ScriptsFind(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.Find(LocalString(ctx, params[1])));
        }

        cell_t N_ScriptsFireInput(IPluginContext* ctx, const cell_t* params)
        {
            return scripts::scriptsManager.FireInput(LocalString(ctx, params[1]), LocalString(ctx, params[2]));
        }

        // HookScriptMessage(channel, handler)
        cell_t N_ScriptsHookScriptMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            ScriptMessageHandler handler([fn](const char* channel, const char* payload)
            {
                sp::CallArgs args;
                args.PushString(channel ? channel : "");
                args.PushString(payload ? payload : "");
                Invoke(fn, args);
            });
            const Source2ModScriptMessageHookId id = scripts::scriptsManager.HookScriptMessage(owner, LocalString(ctx, params[1]), std::move(handler));
            if (id)
                g_scriptHooks[id] = owner;
            return static_cast<cell_t>(id);
        }

        cell_t N_ScriptsUnhookScriptMessage(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_scriptHooks.find(params[1]);
            if (it == g_scriptHooks.end() || it->second != CallerId(ctx))
                return 0;
            g_scriptHooks.erase(it);
            return scripts::scriptsManager.UnhookScriptMessageId(params[1]);
        }

        const sp_nativeinfo_t g_scriptNatives[] = {
            { "Source2ModScripts.CanRunSource", N_ScriptsCanRunSource },
            { "Source2ModScripts.LoadAsset", N_ScriptsLoadAsset },
            { "Source2ModScripts.RunSource", N_ScriptsRunSource },
            { "Source2ModScripts.RunFile", N_ScriptsRunFile },
            { "Source2ModScripts.Reload", N_ScriptsReload },
            { "Source2ModScripts.Remove", N_ScriptsRemove },
            { "Source2ModScripts.Find", N_ScriptsFind },
            { "Source2ModScripts.FireInput", N_ScriptsFireInput },
            { "Source2ModScripts.HookScriptMessage", N_ScriptsHookScriptMessage },
            { "Source2ModScripts.UnhookScriptMessage", N_ScriptsUnhookScriptMessage },
            { nullptr, nullptr },
        };
    }

    void ForgetScriptHooks(PluginId id)
    {
        // The core has taken these down already (it unhooks by owner); only the
        // bookkeeping is left.
        for (auto it = g_scriptHooks.begin(); it != g_scriptHooks.end();)
            it = it->second == id ? g_scriptHooks.erase(it) : std::next(it);
    }

    const sp_nativeinfo_t* GetScriptNatives()
    {
        return g_scriptNatives;
    }
}
