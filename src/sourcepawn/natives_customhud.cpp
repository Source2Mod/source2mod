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
 * SourcePawn natives for source2mod/customhud.inc (custom HUD clicks) on the
 * core's custom HUD (core/customhud.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/customhud.h"

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

        // ---- Source2ModCustomHud -------------------------------------------------------

        struct HudHook
        {
            PluginId owner;
            CEntityHandle layout;
        };
        std::map<Source2ModHookId, HudHook> g_hudHooks;

        // HookCustomHudClick(layout, handler)
        cell_t N_HudHookCustomHudClick(IPluginContext* ctx, const cell_t* params)
        {
            auto* layout = TypedEntityArg<CCSCustomHudLayout>(ctx, params[1]);
            IPluginFunction* fn = layout ? Function(ctx, params[2]) : nullptr;
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            CustomHudClickHandler handler([fn](CCSPlayerController* player, CCSCustomHudLayout* l, const char* buttonId)
            {
                sp::CallArgs args;
                args.PushCell(EntityCell(player));
                args.PushCell(EntityCell(l));
                args.PushString(buttonId ? buttonId : "");
                Invoke(fn, args);
            });
            const Source2ModCustomHudHookId id = customhud::customHudManager.HookCustomHudClick(owner, layout, std::move(handler));
            if (id)
                g_hudHooks[id] = { owner, layout->GetRefEHandle() };
            return static_cast<cell_t>(id);
        }

        cell_t N_HudUnhookCustomHudClick(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_hudHooks.find(params[1]);
            if (it == g_hudHooks.end() || it->second.owner != CallerId(ctx))
                return 0;
            g_hudHooks.erase(it);
            return customhud::customHudManager.UnhookCustomHudClickId(params[1]);
        }

        cell_t N_HudUnhookCustomHudClickLayout(IPluginContext* ctx, const cell_t* params)
        {
            auto* layout = TypedEntityArg<CCSCustomHudLayout>(ctx, params[1]);
            if (!layout)
                return 0;
            const PluginId owner = CallerId(ctx);
            const CEntityHandle handle = layout->GetRefEHandle();
            for (auto it = g_hudHooks.begin(); it != g_hudHooks.end();)
            {
                if (it->second.owner == owner && it->second.layout == handle)
                {
                    customhud::customHudManager.UnhookCustomHudClickId(it->first);
                    it = g_hudHooks.erase(it);
                }
                else
                    ++it;
            }
            return 0;
        }

        const sp_nativeinfo_t g_customHudNatives[] = {
            { "Source2ModCustomHud.HookCustomHudClick", N_HudHookCustomHudClick },
            { "Source2ModCustomHud.UnhookCustomHudClick", N_HudUnhookCustomHudClick },
            { "Source2ModCustomHud.UnhookCustomHudClickLayout", N_HudUnhookCustomHudClickLayout },
            { nullptr, nullptr },
        };
    }

    void ForgetCustomHudHooks(PluginId id)
    {
        // The core has taken these down already (it unhooks by owner); only the
        // bookkeeping is left.
        for (auto it = g_hudHooks.begin(); it != g_hudHooks.end();)
            it = it->second.owner == id ? g_hudHooks.erase(it) : std::next(it);
    }

    const sp_nativeinfo_t* GetCustomHudNatives()
    {
        return g_customHudNatives;
    }
}
