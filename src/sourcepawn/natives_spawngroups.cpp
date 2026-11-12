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
 * SourcePawn natives for source2mod/spawngroups.inc: Source2ModSpawnGroups
 * (core/game/spawngroups.cpp). The script's groups and callbacks are the
 * core's, owned by the script, so they go when it unloads.
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/game/spawngroups.h"

#include <sp_callable.h>

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

        bool ReadVector(IPluginContext* ctx, cell_t addr, float out[3])
        {
            cell_t* v;
            ctx->LocalToPhysAddr(addr, &v);
            if (v == ctx->GetNullRef(SP_NULL_VECTOR))
                return false;
            out[0] = sp_ctof(v[0]);
            out[1] = sp_ctof(v[1]);
            out[2] = sp_ctof(v[2]);
            return true;
        }

        // Load(worldName, origin, angles, flags, name, callback, data, localNameFixup)
        cell_t N_Load(IPluginContext* ctx, const cell_t* params)
        {
            SpawnGroupLoadInfo info;
            info.worldName = LocalString(ctx, params[1]);
            if (!info.worldName || !*info.worldName)
                return ctx->ThrowNativeError("The world name is empty");

            // A script built against the first version: Load(world, name, blockUntilLoaded).
            if (params[0] == 3)
            {
                info.name = LocalString(ctx, params[2]);
                info.flags = params[3] ? SPAWNGROUP_LOAD_BLOCK_UNTIL_LOADED : SPAWNGROUP_LOAD_NONE;
                return static_cast<cell_t>(spawngroups::spawnGroupsManager.Load(CallerId(ctx), info));
            }

            float v[3];
            if (ReadVector(ctx, params[2], v))
                info.origin = Vector(v[0], v[1], v[2]);
            if (ReadVector(ctx, params[3], v))
                info.angles = QAngle(v[0], v[1], v[2]);
            info.flags = static_cast<uint32_t>(params[4]);
            info.name = LocalString(ctx, params[5]);
            info.localNameFixup = LocalString(ctx, params[8]);

            SpawnGroupCallback callback;
            if (params[6] != -1)
            {
                IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[6]));
                if (!fn)
                    return ctx->ThrowNativeError("Invalid function id (%X)", params[6]);
                const cell_t data = params[7];
                callback = SpawnGroupCallback([fn, data](Source2ModSpawnGroup group, bool loaded)
                {
                    sp::CallArgs args;
                    args.PushCell(static_cast<cell_t>(group));
                    args.PushCell(loaded);
                    args.PushCell(data);
                    Invoke(fn, args);
                });
            }

            return static_cast<cell_t>(spawngroups::spawnGroupsManager.Load(CallerId(ctx), info, std::move(callback)));
        }

        cell_t N_Unload(IPluginContext*, const cell_t* params)
        {
            return spawngroups::spawnGroupsManager.Unload(static_cast<Source2ModSpawnGroup>(params[1]));
        }

        cell_t N_FindByName(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(spawngroups::spawnGroupsManager.FindByName(LocalString(ctx, params[1])));
        }

        cell_t N_IsLoaded(IPluginContext*, const cell_t* params)
        {
            return spawngroups::spawnGroupsManager.IsLoaded(static_cast<Source2ModSpawnGroup>(params[1]));
        }

        cell_t N_IsLoading(IPluginContext*, const cell_t* params)
        {
            return spawngroups::spawnGroupsManager.IsLoading(static_cast<Source2ModSpawnGroup>(params[1]));
        }

        cell_t N_MakeActive(IPluginContext*, const cell_t* params)
        {
            return spawngroups::spawnGroupsManager.MakeActive(static_cast<Source2ModSpawnGroup>(params[1]));
        }

        cell_t N_GetEntitySpawnGroup(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(spawngroups::spawnGroupsManager.GetEntitySpawnGroup(EntityArg(ctx, params[1])));
        }

        const sp_nativeinfo_t g_spawnGroupNatives[] = {
            { "Source2ModSpawnGroups.Load", N_Load },
            { "Source2ModSpawnGroups.Unload", N_Unload },
            { "Source2ModSpawnGroups.FindByName", N_FindByName },
            { "Source2ModSpawnGroups.IsLoaded", N_IsLoaded },
            { "Source2ModSpawnGroups.IsLoading", N_IsLoading },
            { "Source2ModSpawnGroups.MakeActive", N_MakeActive },
            { "Source2ModSpawnGroups.GetEntitySpawnGroup", N_GetEntitySpawnGroup },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetSpawnGroupNatives()
    {
        return g_spawnGroupNatives;
    }
}
