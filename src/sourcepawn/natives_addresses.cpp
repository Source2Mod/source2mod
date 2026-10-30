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
 * SourcePawn natives for source2mod/addresses.inc: Source2ModAddresses on
 * the core's resolved addresses (core/addresses.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"
#include "addresses_list.h"

#include "core/game/addresses.h"

#include <source2mod/sdk/schema/classes/CBaseEntity.h>

#include <sp_callable.h>

#include <cstdlib>
#include <string>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // ---- Source2ModAddresses ---------------------------------------------------

        cell_t N_AddressesIsResolved(IPluginContext* ctx, const cell_t* params)
        {
            return AddressOf(ctx, params[1]) != nullptr;
        }

        const sp_nativeinfo_t g_addressNatives[] = {
            { "Source2ModAddresses.IsResolved", N_AddressesIsResolved },
            { nullptr, nullptr },
        };
    }

    void* AddressOf(IPluginContext* ctx, cell_t address)
    {
        auto& a = addresses::source2modAddresses;
        cell_t index = 0;
#define S2M_ADDRESS(name)                                           \
        if (address == index++)                                     \
            return reinterpret_cast<void*>(a.name());
        SOURCE2MOD_SP_ADDRESSES(S2M_ADDRESS)
#undef S2M_ADDRESS
        ctx->ThrowNativeError("Invalid address %d", address);
        return nullptr;
    }

    const sp_nativeinfo_t* GetAddressNatives()
    {
        return g_addressNatives;
    }
}
