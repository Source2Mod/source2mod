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
 * SourcePawn natives for source2mod/transmit.inc on the core's transmit
 * manager (core/transmit.cpp). The CheckTransmit viewer list is a handle
 * made for the call and freed when the handler returns.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/transmit.h"

#include <source2mod/schema/movedata.h>
#include <source2mod/schema/takedamageinfo.h>
#include <source2mod/schema/takedamageresult.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CBasePlayerController.h>
#include <source2mod/schema/entity/classes/CBasePlayerWeapon.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawnBase.h>
#include <source2mod/schema/entity/classes/CCSPlayer_ItemServices.h>
#include <source2mod/schema/entity/classes/CCSPlayer_MovementServices.h>
#include <source2mod/schema/entity/classes/CCSPlayer_WeaponServices.h>
#include <entity2/entityidentity.h>

#include <sp_callable.h>

#include <algorithm>
#include <type_traits>
#include <vector>

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

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        CEntityInstance* EntityAt(IPluginContext* ctx, cell_t entity)
        {
            return EntityArg(ctx, entity);
        }

        HandleType FrameType(const char* name)
        {
            return Handles().CreateType(name, nullptr, 0, false);
        }

        // ---- transmit -----------------------------------------------------------------

        transmit::TransmitManager& TM()
        {
            return transmit::transmitManager;
        }

        cell_t N_THookEntity(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().HookEntity(CallerId(ctx), e, params[2] != 0);
        }

        cell_t N_TUnhookEntity(IPluginContext* ctx, const cell_t* params)
        {
            if (CEntityInstance* e = EntityAt(ctx, params[1]))
                TM().UnhookEntity(e);
            return 0;
        }

        cell_t N_TIsEntityHooked(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().IsEntityHooked(e);
        }

        cell_t N_TSetVisible(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().SetVisible(e, CPlayerSlot(params[2]), params[3] != 0, params[4]);
        }

        cell_t N_TSetVisibleToAll(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().SetVisibleToAll(e, params[2] != 0, params[3]);
        }

        cell_t N_TIsVisible(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().IsVisible(e, CPlayerSlot(params[2]));
        }

        cell_t N_TIsHiddenOnChannel(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().IsHiddenOnChannel(e, CPlayerSlot(params[2]), params[3]);
        }

        cell_t N_TSetBlockAll(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().SetBlockAll(e, params[2] != 0);
        }

        cell_t N_TGetBlockAll(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().GetBlockAll(e);
        }

        cell_t N_TSetOwner(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            CEntityInstance* owner = params[2] == -1 ? nullptr : EntityAt(ctx, params[2]);
            return e && (owner || params[2] == -1) && TM().SetOwner(e, owner);
        }

        cell_t N_TGetOwner(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e ? EntityCell(TM().GetOwner(e)) : -1;
        }

        cell_t N_TResetViewer(IPluginContext*, const cell_t* params)
        {
            TM().ResetViewer(CPlayerSlot(params[1]));
            return 0;
        }

        HandleType InfoListType()
        {
            static const HandleType t = FrameType("ISource2ModTransmitInfoList");
            return t;
        }

        HandleType TransmitInfoType()
        {
            static const HandleType t = FrameType("ISource2ModTransmitInfo");
            return t;
        }

        struct InfoList
        {
            ISource2ModTransmitInfo* const* infos;
            int count;
            PluginId owner;
            std::vector<cell_t> handles;    // one per viewer, made on first Get()
        };

        cell_t N_THookCheckTransmit(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            CheckTransmitHook handler([fn, owner](ISource2ModTransmitInfo* const* infos, int infoCount, const uint16_t* indices, int entityCount)
            {
                InfoList list{ infos, infoCount, owner, std::vector<cell_t>(static_cast<size_t>(std::max(infoCount, 0)), 0) };
                const cell_t listHandle = Handles().Create(InfoListType(), &list, owner);

                std::vector<cell_t> cells(indices, indices + std::max(entityCount, 0));
                sp::CallArgs args;
                args.PushCell(listHandle);
                args.PushCell(infoCount);
                args.PushArray(cells.data(), static_cast<unsigned>(cells.size()));
                args.PushCell(entityCount);
                Invoke(fn, args);

                for (cell_t h : list.handles)
                    if (h)
                        Handles().Free(h, 0, true);
                Handles().Free(listHandle, 0, true);
            });
            return static_cast<cell_t>(TM().HookCheckTransmit(owner, std::move(handler)));
        }

        cell_t N_TUnhookCheckTransmit(IPluginContext*, const cell_t* params)
        {
            return static_cast<ISource2ModTransmit&>(TM()).UnhookCheckTransmit(static_cast<Source2ModTransmitHookId>(params[1]));
        }

        cell_t N_ListGet(IPluginContext* ctx, const cell_t* params)
        {
            auto* list = static_cast<InfoList*>(ReadHandle(ctx, params[1], InfoListType()));
            if (!list)
                return 0;
            const cell_t i = params[2];
            if (i < 0 || i >= list->count)
                return ctx->ThrowNativeError("Viewer %d is out of range (%d viewers)", i, list->count);
            if (!list->handles[i])
                list->handles[i] = Handles().Create(TransmitInfoType(), list->infos[i], list->owner);
            return list->handles[i];
        }

        ISource2ModTransmitInfo* InfoOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ISource2ModTransmitInfo*>(ReadHandle(ctx, handle, TransmitInfoType()));
        }

        cell_t N_InfoGetPlayerSlot(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            return i ? i->GetPlayerSlot().Get() : -1;
        }

        cell_t N_InfoIsFullUpdate(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            return i && i->IsFullUpdate();
        }

        cell_t N_InfoIsTransmitting(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            return i && i->IsTransmitting(params[2]);
        }

        cell_t N_InfoBlockTransmit(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]))
                i->BlockTransmit(params[2]);
            return 0;
        }

        cell_t N_InfoAllowTransmit(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]))
                i->AllowTransmit(params[2]);
            return 0;
        }

        // Get/SetTransmitEntity, ...NonPlayers, ...OutOfPVS, ...Always: one bit of a set.
        template <CBitVec<MAX_EDICTS>* (ISource2ModTransmitInfo::*SET)()>
        CBitVec<MAX_EDICTS>* BitsOf(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            if (!i)
                return nullptr;
            if (params[2] < 0 || params[2] >= MAX_EDICTS)
            {
                ctx->ThrowNativeError("Entity index %d is out of range", params[2]);
                return nullptr;
            }
            return (i->*SET)();
        }

        template <CBitVec<MAX_EDICTS>* (ISource2ModTransmitInfo::*SET)()>
        cell_t N_InfoGetBit(IPluginContext* ctx, const cell_t* params)
        {
            CBitVec<MAX_EDICTS>* bits = BitsOf<SET>(ctx, params);
            return bits && bits->IsBitSet(params[2]);
        }

        template <CBitVec<MAX_EDICTS>* (ISource2ModTransmitInfo::*SET)()>
        cell_t N_InfoSetBit(IPluginContext* ctx, const cell_t* params)
        {
            if (CBitVec<MAX_EDICTS>* bits = BitsOf<SET>(ctx, params))
            {
                if (params[3])
                    bits->Set(params[2]);
                else
                    bits->Clear(params[2]);
            }
            return 0;
        }

        const sp_nativeinfo_t g_transmitNatives[] = {
            { "Source2ModTransmit.HookEntity", N_THookEntity },
            { "Source2ModTransmit.UnhookEntity", N_TUnhookEntity },
            { "Source2ModTransmit.IsEntityHooked", N_TIsEntityHooked },
            { "Source2ModTransmit.SetVisible", N_TSetVisible },
            { "Source2ModTransmit.SetVisibleToAll", N_TSetVisibleToAll },
            { "Source2ModTransmit.IsVisible", N_TIsVisible },
            { "Source2ModTransmit.IsHiddenOnChannel", N_TIsHiddenOnChannel },
            { "Source2ModTransmit.SetBlockAll", N_TSetBlockAll },
            { "Source2ModTransmit.GetBlockAll", N_TGetBlockAll },
            { "Source2ModTransmit.SetOwner", N_TSetOwner },
            { "Source2ModTransmit.GetOwner", N_TGetOwner },
            { "Source2ModTransmit.ResetViewer", N_TResetViewer },
            { "Source2ModTransmit.HookCheckTransmit", N_THookCheckTransmit },
            { "Source2ModTransmit.UnhookCheckTransmit", N_TUnhookCheckTransmit },
            { "ISource2ModTransmitInfoList.Get", N_ListGet },
            { "ISource2ModTransmitInfo.GetPlayerSlot", N_InfoGetPlayerSlot },
            { "ISource2ModTransmitInfo.IsFullUpdate", N_InfoIsFullUpdate },
            { "ISource2ModTransmitInfo.IsTransmitting", N_InfoIsTransmitting },
            { "ISource2ModTransmitInfo.BlockTransmit", N_InfoBlockTransmit },
            { "ISource2ModTransmitInfo.AllowTransmit", N_InfoAllowTransmit },
            { "ISource2ModTransmitInfo.GetTransmitEntity", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitEntity> },
            { "ISource2ModTransmitInfo.SetTransmitEntity", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitEntity> },
            { "ISource2ModTransmitInfo.GetTransmitNonPlayers", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitNonPlayers> },
            { "ISource2ModTransmitInfo.SetTransmitNonPlayers", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitNonPlayers> },
            { "ISource2ModTransmitInfo.GetTransmitOutOfPVS", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitOutOfPVS> },
            { "ISource2ModTransmitInfo.SetTransmitOutOfPVS", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitOutOfPVS> },
            { "ISource2ModTransmitInfo.GetTransmitAlways", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitAlways> },
            { "ISource2ModTransmitInfo.SetTransmitAlways", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitAlways> },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetTransmitNatives()
    {
        return g_transmitNatives;
    }
}
