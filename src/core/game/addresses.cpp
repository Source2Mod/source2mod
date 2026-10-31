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
#include "core/game/addresses.h"
#include "core/game/module.h"

#include "core/data/gameconfig.h"
#include "core/runtime/shared.h"

namespace addresses
{
    Addresses source2modAddresses;

    DynLibUtils::CMemory FindSignature(const char* pszName)
    {
        // The module comes from the entry's "library" field, so tier0 and
        // engine2 entries need no interface pointer to stand in for them.
        DynLibUtils::CModule* pModule = gameconfig::gameConfig.GetModule(pszName);
        const char* pszSignature = gameconfig::gameConfig.GetSignature(pszName);
        if (!pModule || !pszSignature)
            return {};

        // Into the gameconfig's cache too: an SDK helper's first call
        // (ISource2ModFunctions::Find) resolves the same name again, and a
        // scan of libserver takes over 10 ms.
        DynLibUtils::CMemory address = pModule->FindPattern(DynLibUtils::ParsePattern(pszSignature));
        gameconfig::gameConfig.RememberAddress(pszName, address.GetPtr());
        return address;
    }

    bool Initialize()
    {
        RESOLVE_SIG("CEntityInstance::AcceptInput", source2modAddresses.AcceptInput);
        RESOLVE_SIG("CEntityIOOutput::FireOutputInternal", source2modAddresses.FireOutputInternal);
        RESOLVE_SIG("CEntitySystem::AddEntityIOEvent", source2modAddresses.AddEntityIOEvent);
        RESOLVE_SIG("CGameEntitySystem::FindEntityByClassName", source2modAddresses.FindEntityByClassName);
        RESOLVE_SIG("CGameEntitySystem::FindEntityByName", source2modAddresses.FindEntityByName);
        RESOLVE_SIG("UTIL::CreateEntityByName", source2modAddresses.CreateEntityByName);

        // The SDK and the plugins reach these through ISource2ModFunctions, on the
        // first call. Scanned here so a game update that breaks one shows at start.
        for (const char* pszName : {
                 "CAttributeList::SetOrAddAttributeValueByName",
                 "CBaseEntity::DispatchSpawn",
                 "CBaseEntity::TakeDamageOld",
                 "CBaseModelEntity::SetModel",
                 "CBasePlayerController::SetPawn",
                 "CBasePlayerPawn::SnapViewAngles",
                 "CCSGameRules::TerminateRound",
                 "CCSPlayer_ItemServices::GiveNamedItem",
                 "CCSPlayer_WeaponServices::Destroy",
                 "CCSPlayerController::SwitchTeam",
                 "CCSPlayerPawn::PostThink",
                 "CCSPlayerPawnBase::CanMove",
                 "CDecoyProjectile::EmitGrenade",
                 "CFlashbangProjectile::EmitGrenade",
                 "CHEGrenadeProjectile::EmitGrenade",
                 "CMolotovProjectile::EmitGrenade",
                 "CSmokeGrenadeProjectile::EmitGrenade",
                 "CTakeDamageInfo::Constructor",
                 "DispatchParticleEffect",
                 "GetWeaponCSDataFromKey",
                 "LegacyGameEventListener",
                 "UTIL::Remove",
             })
        {
            if (!FindSignature(pszName))
            {
                FP_DEBUG("Failed to find '{}'", pszName);
                return false;
            }
        }

        return true;
    }

    CBaseEntity_CreateEntityByName_t Addresses::CBaseEntity_CreateEntityByName()
    {
        return CreateEntityByName.RCast<CBaseEntity_CreateEntityByName_t>();
    }

    CEntityInstance_AcceptInput_t Addresses::CEntityInstance_AcceptInput()
    {
        return AcceptInput.RCast<CEntityInstance_AcceptInput_t>();
    }

    CEntitySystem_AddEntityIOEvent_t Addresses::CEntitySystem_AddEntityIOEvent()
    {
        return AddEntityIOEvent.RCast<CEntitySystem_AddEntityIOEvent_t>();
    }

    CGameEntitySystem_FindEntityByClassName_t Addresses::CGameEntitySystem_FindEntityByClassName()
    {
        return FindEntityByClassName.RCast<CGameEntitySystem_FindEntityByClassName_t>();
    }

    CGameEntitySystem_FindEntityByName_t Addresses::CGameEntitySystem_FindEntityByName()
    {
        return FindEntityByName.RCast<CGameEntitySystem_FindEntityByName_t>();
    }

    DynLibUtils::CModule& Addresses::GetOrLoadModule(void* ptr)
    {
        return modules::modulesManager.GetFromAddress(ptr)->Raw();
    }
}
