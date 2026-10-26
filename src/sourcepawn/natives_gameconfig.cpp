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
 * SourcePawn natives for source2mod/gameconfig.inc: Source2ModGameConfig on
 * the core's gamedata (core/gameconfig.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gameconfig.h"
#include "core/module.h"
#include "core/shared.h"

#include <source2mod/sdk/schema/classes/CBaseEntity.h>

#include <tier0/buffer_string.h>

#include <khook.hpp>
#include <sp_callable.h>

#include <cstring>
#include <string>
#include <type_traits>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {

        // enum Source2ModPlatform (gameconfig.inc)
        constexpr cell_t kPlatformLinux = 0;
        constexpr cell_t kPlatformWindows = 1;

        // An optional string argument at params[n]: "" when the script was
        // compiled against an include without it.
        const char* OptionalString(IPluginContext* ctx, const cell_t* params, int n)
        {
            return params[0] >= n ? LocalString(ctx, params[n]) : "";
        }

        // A signature in a module, optionally only in one section (".text").
        uintptr_t FindSignature(const char* moduleName, const char* signature, const char* section)
        {
            Source2ModModule module(moduleName);
            if (!section || !*section)
                return module.FindPattern(signature).GetAddr();
            if (!module.GetSectionByName(section).IsValid())
                return 0;
            return module.FindPatternInSection(signature, section).GetAddr();
        }

        // ---- Source2ModGameConfig ------------------------------------------------------

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            if (!value)
                return 0;
            ctx->StringToLocalUTF8(addr, maxlen, value, nullptr);
            return 1;
        }

        cell_t N_GCGetLibrary(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetLibrary(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetSignature(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetSignature(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetSymbol(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetSymbol(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetPatch(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetPatch(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetOffset(IPluginContext* ctx, const cell_t* params)
        {
            return gameconfig::gameConfig.GetOffset(LocalString(ctx, params[1]));
        }

        cell_t N_GCIsSymbol(IPluginContext* ctx, const cell_t* params)
        {
            return gameconfig::gameConfig.IsSymbol(LocalString(ctx, params[1]));
        }

        cell_t N_GCGetPlatform(IPluginContext*, const cell_t*)
        {
#if defined(_WIN32)
            return kPlatformWindows;
#else
            return kPlatformLinux;
#endif
        }

        // Resolves(const char[] name): the entry's signature or symbol is found.
        cell_t N_GCResolves(IPluginContext* ctx, const cell_t* params)
        {
            return gameconfig::gameConfig.ResolveSignature(LocalString(ctx, params[1])).GetAddr() != 0;
        }

        // SignatureMatches(const char[] module, const char[] signature, const char[] section = "")
        cell_t N_GCSignatureMatches(IPluginContext* ctx, const cell_t* params)
        {
            return FindSignature(LocalString(ctx, params[1]), LocalString(ctx, params[2]), OptionalString(ctx, params, 3)) != 0;
        }

        const sp_nativeinfo_t g_gameConfigNatives[] = {
            { "Source2ModGameConfig.GetLibrary", N_GCGetLibrary },
            { "Source2ModGameConfig.GetSignature", N_GCGetSignature },
            { "Source2ModGameConfig.GetSymbol", N_GCGetSymbol },
            { "Source2ModGameConfig.GetPatch", N_GCGetPatch },
            { "Source2ModGameConfig.GetOffset", N_GCGetOffset },
            { "Source2ModGameConfig.IsSymbol", N_GCIsSymbol },
            { "Source2ModGameConfig.GetPlatform", N_GCGetPlatform },
            { "Source2ModGameConfig.Resolves", N_GCResolves },
            { "Source2ModGameConfig.SignatureMatches", N_GCSignatureMatches },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetGameConfigNatives()
    {
        return g_gameConfigNatives;
    }
}
