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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl gives
 * you permission to link the code of this program (as well as its derivative
 * works) to "Counter-Strike 2," "Source 2," "Steam," and any Game MODs or
 * server software running on software by Valve Corporation. You must obey the
 * GNU General Public License in all respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "core/hooks/virtualpoints.h"
#include "core/runtime/shared.h"

#include "source2mod/sdk/schema/classes/CCSPlayerController.h"
#include "source2mod/sdk/schema/classes/CCSPlayerController_InventoryServices.h"

#include "core/hooks/dynamichooks.h"
#include "core/game/functions.h"
#include "core/hooks/hookid.h"
#include "utils/log.h"

#include <cstdint>
#include <type_traits>

namespace virtualpoints
{
    namespace
    {
        struct Def
        {
            HookPointId point;
            const char* name;
            const char* gamedata;       // an offsets entry with a "function" object
            const char* defaultClass;   // what HookTarget::All() means; nullptr: refused
        };

        // A class whose objects all share one vtable may take All(); an entity
        // virtual has an override per class, so it needs Instance or Class.
        constexpr Def kDefs[] = {
            { HookPointId::Spawn, "Spawn", "CBaseEntity::Spawn", nullptr },
            { HookPointId::Use, "Use", "CBaseEntity::Use", nullptr },
            { HookPointId::StartTouch, "StartTouch", "CBaseEntity::StartTouch", nullptr },
            { HookPointId::EndTouch, "EndTouch", "CBaseEntity::EndTouch", nullptr },
            { HookPointId::Blocked, "Blocked", "CBaseEntity::Blocked", nullptr },
            { HookPointId::Think, "Think", "CBaseEntity::Think", nullptr },
            { HookPointId::Killed, "Killed", "CBaseEntity::Event_Killed", nullptr },
            { HookPointId::WeaponPickup, "WeaponPickup", "CCSPlayer_WeaponServices::BumpWeapon", "CCSPlayer_WeaponServices" },
            { HookPointId::WeaponSwitch, "WeaponSwitch", "CCSPlayer_WeaponServices::SelectItem", "CCSPlayer_WeaponServices" },
            { HookPointId::GetItemInLoadout, "GetItemInLoadout", "CCSPlayerInventory::GetItemInLoadout", "CCSPlayerInventory" },
        };

        const Def* DefOf(HookPointId point)
        {
            for (const Def& d : kDefs)
                if (d.point == point)
                    return &d;
            return nullptr;
        }

        // The context of each point, read off the call (`this` is argument 0).
        SpawnContext Make(FunctionContext& f, SpawnContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<const CEntityKeyValues*>(1) };
        }
        UseContext Make(FunctionContext& f, UseContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<EntityUseInfo*>(1) };
        }
        EntityOtherContext Make(FunctionContext& f, EntityOtherContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<CBaseEntity*>(1) };
        }
        ThinkContext Make(FunctionContext& f, ThinkContext*)
        {
            return { f.GetArg<CBaseEntity*>(0) };
        }
        KilledContext Make(FunctionContext& f, KilledContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<const CTakeDamageResult*>(1), false };
        }
        WeaponPickupContext Make(FunctionContext& f, WeaponPickupContext*)
        {
            return { f.GetArg<CCSPlayer_WeaponServices*>(0), f.GetArg<CBasePlayerWeapon*>(1), 0 };
        }
        WeaponSwitchContext Make(FunctionContext& f, WeaponSwitchContext*)
        {
            return { f.GetArg<CCSPlayer_WeaponServices*>(0), f.GetArg<CBasePlayerWeapon*>(1), f.GetArg<std::int32_t>(2), false };
        }

        // A player's CCSPlayerInventory is embedded in their
        // CCSPlayerController_InventoryServices, right after the schema field
        // m_nPersonaDataXpTrailLevel (an int32; the inventory starts with a
        // vtable, so 8-aligned). SwiftlyS2 finds the owner the same way.
        std::ptrdiff_t InventoryOffset(CCSPlayerController_InventoryServices* services)
        {
            static std::ptrdiff_t offset = -1;
            if (offset < 0 && services)
            {
                const auto field = reinterpret_cast<std::uintptr_t>(&services->m_nPersonaDataXpTrailLevel.Get());
                const auto base = reinterpret_cast<std::uintptr_t>(services);
                if (field > base && field - base < 0x10000)
                    offset = static_cast<std::ptrdiff_t>((field - base + sizeof(std::int32_t) + 7) & ~std::uintptr_t(7));
            }
            return offset;
        }

        CCSPlayerController* InventoryOwner(const CCSPlayerInventory* inventory)
        {
            if (!inventory)
                return nullptr;
            for (int slot = 0, n = shared::MaxClients(); slot < n; slot++)
            {
                CCSPlayerController* controller = CCSPlayerController::FromSlot(slot);
                CCSPlayerController_InventoryServices* services = controller ? controller->m_pInventoryServices() : nullptr;
                const std::ptrdiff_t offset = InventoryOffset(services);
                if (services && offset > 0
                    && reinterpret_cast<const std::uint8_t*>(services) + offset == reinterpret_cast<const std::uint8_t*>(inventory))
                    return controller;
            }
            return nullptr;
        }

        GetItemInLoadoutContext Make(FunctionContext& f, GetItemInLoadoutContext*)
        {
            auto* inventory = f.GetArg<CCSPlayerInventory*>(0);
            return { inventory, InventoryOwner(inventory), f.GetArg<std::int32_t>(1), f.GetArg<std::int32_t>(2), nullptr };
        }

        template <typename R>
        R CallOriginalOf(const void* data)
        {
            return static_cast<FunctionContext*>(const_cast<void*>(data))->CallOriginal<R>();
        }

        // The typed handler as a Function-point handler: the context from the
        // call, the answer's value (or ctx.result) as the return value.
        template <typename CONTEXT>
        HookHandler<FunctionContext> Adapt(const void* handler)
        {
            HookHandler<CONTEXT> typed = *static_cast<const HookHandler<CONTEXT>*>(handler);
            return HookHandler<FunctionContext>([typed](FunctionContext& f, bool post) -> HookReturn<void>
            {
                using R = typename CONTEXT::Return;
                CONTEXT ctx = Make(f, static_cast<CONTEXT*>(nullptr));
                ctx.callOriginal_ = &CallOriginalOf<R>;
                ctx.callOriginalData_ = &f;
                if constexpr (!std::is_void_v<R>)
                {
                    if (post)
                        ctx.result = f.GetReturn<R>();
                }

                const HookReturn<R> answer = typed(ctx, post);
                if constexpr (!std::is_void_v<R>)
                {
                    if (answer.action != Action::Ignore)
                        f.SetReturn<R>(answer.hasValue ? answer.value : ctx.result);
                }
                return HookReturn<void>(answer.action);
            });
        }


        // Calls visit(Context*) with the point's context type.
        template <typename VISIT>
        auto WithContext(HookPointId point, VISIT&& visit)
        {
            switch (point)
            {
            case HookPointId::Spawn: return visit(static_cast<SpawnContext*>(nullptr));
            case HookPointId::Use: return visit(static_cast<UseContext*>(nullptr));
            case HookPointId::StartTouch:
            case HookPointId::EndTouch:
            case HookPointId::Blocked: return visit(static_cast<EntityOtherContext*>(nullptr));
            case HookPointId::Think: return visit(static_cast<ThinkContext*>(nullptr));
            case HookPointId::Killed: return visit(static_cast<KilledContext*>(nullptr));
            case HookPointId::WeaponPickup: return visit(static_cast<WeaponPickupContext*>(nullptr));
            case HookPointId::GetItemInLoadout: return visit(static_cast<GetItemInLoadoutContext*>(nullptr));
            default: return visit(static_cast<WeaponSwitchContext*>(nullptr));
            }
        }

        ISource2ModFunction* FunctionOf(const Def& def)
        {
            // Find keeps the described function for good: one per entry.
            return functions::functionsManager.Find(def.gamedata);
        }
    }

    bool Is(HookPointId point)
    {
        return DefOf(point) != nullptr;
    }

    bool IsAvailable(HookPointId point)
    {
        const Def* def = DefOf(point);
        return def && FunctionOf(*def);
    }

    const char* Name(HookPointId point)
    {
        const Def* def = DefOf(point);
        return def ? def->name : "?";
    }


    Source2ModHookId Add(HookPointId point, PluginId owner, const void* handler, bool post, const HookTarget& target,
                         const HookFilter& filter)
    {
        const Def* def = DefOf(point);
        if (!def || !handler)
            return 0;

        ISource2ModFunction* function = FunctionOf(*def);
        if (!function)
        {
            FP_WARN("Hook on {}: gamedata entry '{}' is missing or does not resolve on this platform", def->name, def->gamedata);
            return 0;
        }

        HookTarget where = target;
        if (where.kind == HookTarget::Kind::All)
        {
            if (!def->defaultClass)
            {
                FP_WARN("Hook on {}: every entity class has its own {} -- hook it on HookTarget::Instance(entity) "
                        "or HookTarget::Class(\"CCSPlayerPawn\"), not All()", def->name, def->name);
                return 0;
            }
            where = HookTarget::Class(def->defaultClass);
        }

        HookHandler<FunctionContext> adapted =
            WithContext(point, [handler](auto* tag) { return Adapt<std::remove_pointer_t<decltype(tag)>>(handler); });
        return dynamichooks::dynamicHooksManager.Add(function, owner, std::move(adapted), post, where, filter);
    }
}
