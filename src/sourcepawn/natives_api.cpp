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
 * SourcePawn natives for CGlobalVars (ISource2ModAPI::GetGlobalVars,
 * source2mod/api.inc).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/pluginapi.h"
#include "core/shared.h"

#include <source2mod/ISource2ModScheduler.h>

#include <sp_callable.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

        // ---- CGlobalVars (ISource2ModAPI::GetGlobalVars) ------------------------------

        CGlobalVars* Globals(IPluginContext* ctx)
        {
            CGlobalVars* globals = pluginApi.GetGlobalVars();
            if (!globals)
                ctx->ThrowNativeError("The global variables are not available yet");
            return globals;
        }

#define S2M_GLOBAL(field, conv)                                                    \
        cell_t N_Globals_##field(IPluginContext* ctx, const cell_t*)               \
        {                                                                          \
            CGlobalVars* g = Globals(ctx);                                         \
            return g ? conv(g->field) : 0;                                         \
        }
#define S2M_CELL(v) static_cast<cell_t>(v)
        S2M_GLOBAL(realtime, sp_ftoc)
        S2M_GLOBAL(framecount, S2M_CELL)
        S2M_GLOBAL(absoluteframetime, sp_ftoc)
        S2M_GLOBAL(absoluteframetime_unbounded, sp_ftoc)
        S2M_GLOBAL(maxClients, S2M_CELL)
        S2M_GLOBAL(m_nCurrentTickThisFrame, S2M_CELL)
        S2M_GLOBAL(m_nTotalTicksThisFrame, S2M_CELL)
        S2M_GLOBAL(curtime, sp_ftoc)
        S2M_GLOBAL(frametime, sp_ftoc)
        S2M_GLOBAL(interpolation_amount, sp_ftoc)
        S2M_GLOBAL(m_bInSimulation, S2M_CELL)
        S2M_GLOBAL(tickcount, S2M_CELL)
        S2M_GLOBAL(m_flSubtickFraction, sp_ftoc)
        S2M_GLOBAL(eLoadType, S2M_CELL)
        S2M_GLOBAL(mp_teamplay, S2M_CELL)
        S2M_GLOBAL(maxEntities, S2M_CELL)
        S2M_GLOBAL(serverCount, S2M_CELL)
#undef S2M_CELL
#undef S2M_GLOBAL

        template <string_t CGlobalVars::*FIELD>
        cell_t N_GlobalsString(IPluginContext* ctx, const cell_t* params)
        {
            CGlobalVars* g = Globals(ctx);
            return ToBuffer(ctx, params[2], params[3], g ? (g->*FIELD).ToCStr() : "");
        }

        const sp_nativeinfo_t g_apiNatives[] = {
            { "CGlobalVars.realtime.get", N_Globals_realtime },
            { "CGlobalVars.framecount.get", N_Globals_framecount },
            { "CGlobalVars.absoluteframetime.get", N_Globals_absoluteframetime },
            { "CGlobalVars.absoluteframetime_unbounded.get", N_Globals_absoluteframetime_unbounded },
            { "CGlobalVars.maxClients.get", N_Globals_maxClients },
            { "CGlobalVars.m_nCurrentTickThisFrame.get", N_Globals_m_nCurrentTickThisFrame },
            { "CGlobalVars.m_nTotalTicksThisFrame.get", N_Globals_m_nTotalTicksThisFrame },
            { "CGlobalVars.curtime.get", N_Globals_curtime },
            { "CGlobalVars.frametime.get", N_Globals_frametime },
            { "CGlobalVars.interpolation_amount.get", N_Globals_interpolation_amount },
            { "CGlobalVars.m_bInSimulation.get", N_Globals_m_bInSimulation },
            { "CGlobalVars.tickcount.get", N_Globals_tickcount },
            { "CGlobalVars.m_flSubtickFraction.get", N_Globals_m_flSubtickFraction },
            { "CGlobalVars.eLoadType.get", N_Globals_eLoadType },
            { "CGlobalVars.mp_teamplay.get", N_Globals_mp_teamplay },
            { "CGlobalVars.maxEntities.get", N_Globals_maxEntities },
            { "CGlobalVars.serverCount.get", N_Globals_serverCount },
            { "CGlobalVars.GetMapName", N_GlobalsString<&CGlobalVars::mapname> },
            { "CGlobalVars.GetStartSpot", N_GlobalsString<&CGlobalVars::startspot> },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetApiNatives()
    {
        return g_apiNatives;
    }
}
