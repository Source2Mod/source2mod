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
 * SourcePawn natives for source2mod/snapshots.inc: Source2ModSnapshots
 * (core/game/snapshots.cpp). The callbacks run inside the native, so nothing
 * outlives the call.
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/game/snapshots.h"

#include <sp_callable.h>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        cell_t N_IsAvailable(IPluginContext*, const cell_t*)
        {
            return snapshots::snapshotsManager.IsAvailable();
        }

        cell_t N_GetNetworkClassName(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* entity = EntityArg(ctx, params[1]);
            const char* name = entity ? snapshots::snapshotsManager.GetNetworkClassName(entity) : nullptr;
            ToBuffer(ctx, params[2], params[3], name);
            return name != nullptr;
        }

        // GetFields(entity, callback, data)
        cell_t N_GetFields(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* entity = EntityArg(ctx, params[1]);
            IPluginFunction* fn = entity ? Function(ctx, params[2]) : nullptr;
            if (!fn)
                return -1;
            const cell_t data = params[3];
            return snapshots::snapshotsManager.GetFields(entity, [fn, data](uint32_t path, const char* name, const char* type, const char* value)
            {
                sp::CallArgs args;
                args.PushCell(static_cast<cell_t>(path));
                args.PushString(name);
                args.PushString(type);
                args.PushString(value);
                args.PushCell(data);
                Invoke(fn, args);
            });
        }

        cell_t N_GetField(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* entity = EntityArg(ctx, params[1]);
            if (!entity)
                return 0;
            std::string value(static_cast<size_t>(params[4] > 0 ? params[4] : 1), '\0');
            const bool found = snapshots::snapshotsManager.GetField(entity, LocalString(ctx, params[2]), value.data(), value.size());
            ToBuffer(ctx, params[3], params[4], found ? value.c_str() : "");
            return found;
        }

        // Dump(entity, callback, data)
        cell_t N_Dump(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* entity = EntityArg(ctx, params[1]);
            IPluginFunction* fn = entity ? Function(ctx, params[2]) : nullptr;
            if (!fn)
                return -1;
            const cell_t data = params[3];
            return snapshots::snapshotsManager.Dump(entity, [fn, data](const char* line)
            {
                sp::CallArgs args;
                args.PushString(line);
                args.PushCell(data);
                Invoke(fn, args);
            });
        }

        const sp_nativeinfo_t g_snapshotNatives[] = {
            { "Source2ModSnapshots.IsAvailable", N_IsAvailable },
            { "Source2ModSnapshots.GetNetworkClassName", N_GetNetworkClassName },
            { "Source2ModSnapshots.GetFields", N_GetFields },
            { "Source2ModSnapshots.GetField", N_GetField },
            { "Source2ModSnapshots.Dump", N_Dump },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetSnapshotNatives()
    {
        return g_snapshotNatives;
    }
}
