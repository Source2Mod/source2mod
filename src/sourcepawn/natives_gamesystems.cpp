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
 * SourcePawn natives for source2mod/gamesystems.inc: Source2ModGameSystems
 * on the core's game systems (core/gamesystems.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gamesystems.h"

#include <source2mod/schema/entity/classes/CBaseEntity.h>

#include <sp_callable.h>

#include <cstdlib>
#include <string>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
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
