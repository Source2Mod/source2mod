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
#pragma once
#include "utils/log.h"

#include <dynlibutils/memaddr.hpp>
#include <dynlibutils/module.hpp>
#include <entity2/variant.h>
#include <platform.h>
#include <playerslot.h>

/// Resolves a gamedata signature by its entry name: the pattern and the module
/// (the entry's "library") both come from the game config.
#define RESOLVE_SIG(name, variable)                                   \
{                                                                     \
    auto addr = FindSignature(name);                                  \
                                                                      \
    if (!addr)                                                        \
    {                                                                 \
        FP_DEBUG("Failed to find '{}'", name);                        \
        return false;                                                 \
    }                                                                 \
                                                                      \
    variable = addr;                                                  \
}

/// Same as RESOLVE_SIG, but a miss is not fatal: the address is left null and
/// initialization carries on. For functions we only have a signature for on
/// some platforms -- the caller is expected to null-check before using it.
#define RESOLVE_SIG_OPTIONAL(name, variable)                          \
{                                                                     \
    auto addr = FindSignature(name);                                  \
                                                                      \
    if (!addr)                                                        \
    {                                                                 \
        FP_DEBUG("Optional '{}' not found, leaving it unresolved", name); \
    }                                                                 \
    else                                                              \
    {                                                                 \
        variable = addr;                                              \
    }                                                                 \
}

class CEntityInstance;
class CEntityIOOutput;
class CEntitySystem;
class CEntityKeyValues;
class CBaseEntity;
class CBasePlayerWeapon;
class CBaseModelEntity;
class CBasePlayerPawn;
class CBasePlayerController;
class CGameRules;
class CCSPlayerController;
class IGameEventListener2;
class IGameEventManager2;
class IEntityFindFilter;

/// What the core calls itself, typed. Everything else in the gamedata is reached by
/// name through ISource2ModFunctions.
using CBaseEntity_CreateEntityByName_t = CBaseEntity* (FASTCALL*)(const char*, int);
using CEntityInstance_AcceptInput_t = bool (FASTCALL*)(CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, const variant_t&);
using CEntitySystem_AddEntityIOEvent_t = void (FASTCALL*)(CEntitySystem*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, const variant_t&, float, void*, void*);
using CGameEntitySystem_FindEntityByClassName_t = CBaseEntity* (FASTCALL*)(CEntitySystem*, CEntityInstance*, const char*);
using CGameEntitySystem_FindEntityByName_t = CBaseEntity* (FASTCALL*)(CEntitySystem*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, CEntityInstance*, IEntityFindFilter*);

namespace addresses
{
    /// Scans for the signatures the core cannot work without; a miss stops the core
    /// from starting instead of failing at some later call.
    bool Initialize();

    /// Scans the module named by a gamedata entry for its signature; null on a miss.
    DynLibUtils::CMemory FindSignature(const char* pszName);

    class Addresses
    {
    public:
        DynLibUtils::CModule& GetOrLoadModule(void* ptr);

        CBaseEntity_CreateEntityByName_t CBaseEntity_CreateEntityByName();
        CEntityInstance_AcceptInput_t CEntityInstance_AcceptInput();
        CEntitySystem_AddEntityIOEvent_t CEntitySystem_AddEntityIOEvent();
        CGameEntitySystem_FindEntityByClassName_t CGameEntitySystem_FindEntityByClassName();
        CGameEntitySystem_FindEntityByName_t CGameEntitySystem_FindEntityByName();

        // Raw addresses: a function pointer is not portably convertible to void*
        // and back, so a scan's result is kept as the address it is.
        DynLibUtils::CMemory CreateEntityByName;
        DynLibUtils::CMemory AcceptInput;
        DynLibUtils::CMemory FireOutputInternal;
        DynLibUtils::CMemory AddEntityIOEvent;
        DynLibUtils::CMemory FindEntityByClassName;
        DynLibUtils::CMemory FindEntityByName;
    };

    extern Addresses source2modAddresses;
}
