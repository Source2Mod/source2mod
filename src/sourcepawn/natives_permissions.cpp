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
 * SourcePawn natives for source2mod/permissions.inc: Source2ModPermissions
 * on the core's permissions manager (core/permissions.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/permissions.h"

#include <source2mod/schema/entity/classes/CBaseEntity.h>

#include <sp_callable.h>

#include <cstdlib>
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

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        // ---- Source2ModPermissions -------------------------------------------------

        uint64 SteamId(IPluginContext* ctx, cell_t addr)
        {
            return std::strtoull(LocalString(ctx, addr), nullptr, 10);
        }

        CPlayerSlot Slot(cell_t value)
        {
            return CPlayerSlot(value);
        }

        auto& Perms()
        {
            return permissions::permissionsManager;
        }

        cell_t N_PermHasPermission(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().HasPermission(SteamId(ctx, params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_PermPlayerHasPermission(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().PlayerHasPermission(Slot(params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_PermCheckAccess(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().CheckAccess(Slot(params[1]), LocalString(ctx, params[2]), LocalString(ctx, params[3]));
        }

        cell_t N_PermInGroup(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().InGroup(SteamId(ctx, params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_PermGetImmunity(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().GetImmunity(SteamId(ctx, params[1]));
        }

        cell_t N_PermCanTarget(IPluginContext*, const cell_t* params)
        {
            return Perms().CanTarget(Slot(params[1]), Slot(params[2]));
        }

        cell_t N_PermGetPlayerSteamID(IPluginContext* ctx, const cell_t* params)
        {
            const std::string text = std::to_string(Perms().GetPlayerSteamID(Slot(params[1])));
            return ToBuffer(ctx, params[2], params[3], text.c_str());
        }

        cell_t N_PermIsPlayerAuthorized(IPluginContext*, const cell_t* params)
        {
            return Perms().IsPlayerAuthorized(Slot(params[1]));
        }

        cell_t N_PermGrantPermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().GrantPermission(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRevokePermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RevokePermission(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermAddToGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().AddToGroup(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRemoveFromGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RemoveFromGroup(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermSetImmunity(IPluginContext* ctx, const cell_t* params)
        {
            Perms().SetImmunity(CallerId(ctx), SteamId(ctx, params[1]), params[2]);
            return 0;
        }

        cell_t N_PermClearPlayer(IPluginContext* ctx, const cell_t* params)
        {
            Perms().ClearPlayer(CallerId(ctx), SteamId(ctx, params[1]));
            return 0;
        }

        cell_t N_PermCreateGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().CreateGroup(CallerId(ctx), LocalString(ctx, params[1]), params[2]);
            return 0;
        }

        cell_t N_PermDeleteGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().DeleteGroup(CallerId(ctx), LocalString(ctx, params[1]));
            return 0;
        }

        cell_t N_PermAddGroupPermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().AddGroupPermission(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRemoveGroupPermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RemoveGroupPermission(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermAddGroupParent(IPluginContext* ctx, const cell_t* params)
        {
            Perms().AddGroupParent(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRemoveGroupParent(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RemoveGroupParent(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        // Access-denied handler ids per plugin; cleared when it unloads.
        std::map<Source2ModHookId, PluginId> g_deniedHandlers;

        cell_t N_PermSetAccessDeniedHandler(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            AccessDeniedHandler handler([fn, owner](const Source2ModCommandContext& c, const Source2ModCommandArgs& a, const char* permission)
            {
                const cell_t context = LendCommandContext(&c, owner);
                const cell_t args = LendCommandArgs(&a, owner);
                sp::CallArgs call;
                call.PushCell(context);
                call.PushCell(args);
                call.PushString(permission ? permission : "");
                Invoke(fn, call);
                Handles().Free(args, 0, true);
                Handles().Free(context, 0, true);
            });
            const Source2ModAccessDeniedHandlerId id = Perms().SetAccessDeniedHandler(std::move(handler));
            if (id)
                g_deniedHandlers[id] = owner;
            return static_cast<cell_t>(id);
        }

        cell_t N_PermClearAccessDeniedHandler(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_deniedHandlers.find(params[1]);
            if (it == g_deniedHandlers.end() || it->second != CallerId(ctx))
                return 0;
            g_deniedHandlers.erase(it);
            return Perms().ClearAccessDeniedHandler(params[1]);
        }

        const sp_nativeinfo_t g_permissionNatives[] = {
            { "Source2ModPermissions.HasPermission", N_PermHasPermission },
            { "Source2ModPermissions.PlayerHasPermission", N_PermPlayerHasPermission },
            { "Source2ModPermissions.CheckAccess", N_PermCheckAccess },
            { "Source2ModPermissions.InGroup", N_PermInGroup },
            { "Source2ModPermissions.GetImmunity", N_PermGetImmunity },
            { "Source2ModPermissions.CanTarget", N_PermCanTarget },
            { "Source2ModPermissions.GetPlayerSteamID", N_PermGetPlayerSteamID },
            { "Source2ModPermissions.IsPlayerAuthorized", N_PermIsPlayerAuthorized },
            { "Source2ModPermissions.GrantPermission", N_PermGrantPermission },
            { "Source2ModPermissions.RevokePermission", N_PermRevokePermission },
            { "Source2ModPermissions.AddToGroup", N_PermAddToGroup },
            { "Source2ModPermissions.RemoveFromGroup", N_PermRemoveFromGroup },
            { "Source2ModPermissions.SetImmunity", N_PermSetImmunity },
            { "Source2ModPermissions.ClearPlayer", N_PermClearPlayer },
            { "Source2ModPermissions.CreateGroup", N_PermCreateGroup },
            { "Source2ModPermissions.DeleteGroup", N_PermDeleteGroup },
            { "Source2ModPermissions.AddGroupPermission", N_PermAddGroupPermission },
            { "Source2ModPermissions.RemoveGroupPermission", N_PermRemoveGroupPermission },
            { "Source2ModPermissions.AddGroupParent", N_PermAddGroupParent },
            { "Source2ModPermissions.RemoveGroupParent", N_PermRemoveGroupParent },
            { "Source2ModPermissions.SetAccessDeniedHandler", N_PermSetAccessDeniedHandler },
            { "Source2ModPermissions.ClearAccessDeniedHandler", N_PermClearAccessDeniedHandler },
            { nullptr, nullptr },
        };
    }

    void ForgetPermissionHooks(PluginId id)
    {
        for (auto it = g_deniedHandlers.begin(); it != g_deniedHandlers.end();)
        {
            if (it->second == id)
            {
                permissions::permissionsManager.ClearAccessDeniedHandler(it->first);
                it = g_deniedHandlers.erase(it);
            }
            else
                ++it;
        }
    }

    const sp_nativeinfo_t* GetPermissionNatives()
    {
        return g_permissionNatives;
    }
}
