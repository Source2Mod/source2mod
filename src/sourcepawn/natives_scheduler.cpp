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
 * SourcePawn natives for the Source2ModTimer fields (source2mod/timers.inc)
 * on the core's scheduler (core/scheduler.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

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
        // ---- Source2ModTimer: the scheduler's Timer fields -------------------------

        cell_t N_TimerInterval(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return sp_ftoc(t ? t->Interval : 0.0f);
        }

        cell_t N_TimerExecTime(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return sp_ftoc(t ? static_cast<float>(t->ExecTime) : 0.0f);
        }

        cell_t N_TimerFlags(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return t ? t->Flags : 0;
        }

        cell_t N_TimerInExec(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return t && t->InExec;
        }

        cell_t N_TimerKillMe(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return !t || t->KillMe;
        }

        const sp_nativeinfo_t g_schedulerNatives[] = {
            { "Source2ModTimer.Interval.get", N_TimerInterval },
            { "Source2ModTimer.ExecTime.get", N_TimerExecTime },
            { "Source2ModTimer.Flags.get", N_TimerFlags },
            { "Source2ModTimer.InExec.get", N_TimerInExec },
            { "Source2ModTimer.KillMe.get", N_TimerKillMe },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetSchedulerNatives()
    {
        return g_schedulerNatives;
    }
}
