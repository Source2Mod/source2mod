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
 * SourcePawn natives for source2mod/paths.inc: Source2ModPaths on the
 * core's paths manager (core/paths.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/data/paths.h"

#include <source2mod/sdk/schema/classes/CBaseEntity.h>

#include <sp_callable.h>

#include <cstdlib>
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

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        // ---- Source2ModPaths -------------------------------------------------------

#define S2M_PATH_NATIVE(name)                                                       \
        cell_t N_Paths##name(IPluginContext* ctx, const cell_t* params)             \
        {                                                                           \
            return ToBuffer(ctx, params[1], params[2], paths::pathsManager.name()); \
        }
        S2M_PATH_NATIVE(GameDirectory)
        S2M_PATH_NATIVE(RootDirectory)
        S2M_PATH_NATIVE(ConfigsDirectory)
        S2M_PATH_NATIVE(GamedataDirectory)
        S2M_PATH_NATIVE(PluginsDirectory)
        S2M_PATH_NATIVE(SharedDirectory)
#undef S2M_PATH_NATIVE

        cell_t N_PathsPluginConfigDirectory(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[1], params[2], paths::pathsManager.PluginConfigDirectory(CallerId(ctx)));
        }

        const sp_nativeinfo_t g_pathNatives[] = {
            { "Source2ModPaths.GameDirectory", N_PathsGameDirectory },
            { "Source2ModPaths.RootDirectory", N_PathsRootDirectory },
            { "Source2ModPaths.ConfigsDirectory", N_PathsConfigsDirectory },
            { "Source2ModPaths.GamedataDirectory", N_PathsGamedataDirectory },
            { "Source2ModPaths.PluginsDirectory", N_PathsPluginsDirectory },
            { "Source2ModPaths.SharedDirectory", N_PathsSharedDirectory },
            { "Source2ModPaths.PluginConfigDirectory", N_PathsPluginConfigDirectory },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetPathNatives()
    {
        return g_pathNatives;
    }
}
