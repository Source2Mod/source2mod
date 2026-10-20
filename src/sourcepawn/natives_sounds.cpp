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
 * SourcePawn natives for source2mod/sounds.inc on the core's sounds manager
 * (core/sounds.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/sounds.h"

#include <source2mod/schema/entity/classes/CBaseEntity.h>

#include <sp_callable.h>

#include <algorithm>
#include <map>
#include <cstring>
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

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        uint64_t ReadMask(IPluginContext* ctx, cell_t addr)
        {
            cell_t* m;
            ctx->LocalToPhysAddr(addr, &m);
            return static_cast<uint64_t>(static_cast<uint32_t>(m[0])) | (static_cast<uint64_t>(static_cast<uint32_t>(m[1])) << 32);
        }

        void WriteMask(cell_t* m, uint64_t mask)
        {
            m[0] = static_cast<cell_t>(static_cast<uint32_t>(mask));
            m[1] = static_cast<cell_t>(static_cast<uint32_t>(mask >> 32));
        }

        void ToFloats(IPluginContext* ctx, cell_t addr, const float* v, int n)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            for (int i = 0; i < n; i++)
                out[i] = sp_ftoc(v[i]);
        }

        void FromFloats(IPluginContext* ctx, cell_t addr, float* v, int n)
        {
            cell_t* in;
            ctx->LocalToPhysAddr(addr, &in);
            for (int i = 0; i < n; i++)
                v[i] = sp_ctof(in[i]);
        }

        Action ToAction(cell_t res)
        {
            return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
        }

        // The scripts' hook ids by kind: an Unhook* takes only an id of its own
        // kind that the calling script made.
        enum class HookKind
        {
            Sound,
        };

        struct ScriptHook
        {
            PluginId owner;
            HookKind kind;
        };

        std::map<Source2ModHookId, ScriptHook> g_hooks;

        cell_t Remember(Source2ModHookId id, PluginId owner, HookKind kind)
        {
            if (id)
                g_hooks[id] = { owner, kind };
            return static_cast<cell_t>(id);
        }

        /// Takes the id out of the script's hooks; false when it is not the caller's of that kind.
        bool Forget(IPluginContext* ctx, cell_t id, HookKind kind)
        {
            auto it = g_hooks.find(static_cast<Source2ModHookId>(id));
            if (it == g_hooks.end() || it->second.kind != kind || it->second.owner != CallerId(ctx))
                return false;
            g_hooks.erase(it);
            return true;
        }

        // ---- sounds -----------------------------------------------------------------

        ISource2ModSounds& SM()
        {
            return sounds::soundsManager;
        }

        HandleType SoundType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModSound", [](void* p)
            {
                sounds::soundsManager.DestroySound(static_cast<ISource2ModSound*>(p));
            });
            return t;
        }

        // The one a sound hook is handed: not the script's to destroy.
        HandleType LentSoundType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModSoundLent", nullptr, SoundType(), false);
            return t;
        }

        ISource2ModSound* SoundOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ISource2ModSound*>(ReadHandle(ctx, handle, SoundType()));
        }

        cell_t N_SoundNew(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            ISource2ModSound* sound = SM().CreateSound(0, *name ? name : nullptr);
            if (!sound)
                return 0;
            const cell_t h = Handles().Create(SoundType(), sound, CallerId(ctx));
            if (!h)
                SM().DestroySound(sound);
            return h;
        }

        cell_t N_SoundSetName(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetName(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundGetName(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            size_t written = 0;
            if (s)
                ctx->StringToLocalUTF8(params[2], params[3], s->GetName() ? s->GetName() : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SoundSetNameHash(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetNameHash(static_cast<uint32_t>(params[2]));
            return 0;
        }

        cell_t N_SoundGetNameHash(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetNameHash()) : 0;
        }

        cell_t N_SoundGetGuid(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetGuid()) : 0;
        }

        cell_t N_SoundSetSourceEntityIndex(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetSourceEntityIndex(params[2]);
            return 0;
        }

        cell_t N_SoundGetSourceEntityIndex(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? s->GetSourceEntityIndex() : 0;
        }

        cell_t N_SoundSetSourceEntity(IPluginContext* ctx, const cell_t* params)
        {
            // An entity is its index; SetSourceEntityIndex says the same.
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetSourceEntityIndex(params[2]);
            return 0;
        }

        cell_t N_SoundSetPosition(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[2], f, 3);
                s->SetPosition(Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_SoundSetVolume(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetVolume(sp_ctof(params[2]));
            return 0;
        }

        cell_t N_SoundGetVolume(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? sp_ftoc(s->GetVolume()) : 0;
        }

        cell_t N_SoundSetPitch(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetPitch(sp_ctof(params[2]));
            return 0;
        }

        cell_t N_SoundGetPitch(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? sp_ftoc(s->GetPitch()) : 0;
        }

        cell_t N_SoundSetChannel(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetChannel(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundGetChannel(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            size_t written = 0;
            if (s)
                ctx->StringToLocalUTF8(params[2], params[3], s->GetChannel() ? s->GetChannel() : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SoundHasParam(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s && s->HasParam(LocalString(ctx, params[2]));
        }

        cell_t N_SoundGetParamType(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetParamType(LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_SoundRemoveParam(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->RemoveParam(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundClearParams(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->ClearParams();
            return 0;
        }

        cell_t N_SoundSetBool(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetBool(LocalString(ctx, params[2]), params[3] != 0);
            return 0;
        }

        cell_t N_SoundSetInt32(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetInt32(LocalString(ctx, params[2]), params[3]);
            return 0;
        }

        cell_t N_SoundSetUInt32(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetUInt32(LocalString(ctx, params[2]), static_cast<uint32_t>(params[3]));
            return 0;
        }

        // SetUInt64(name, const int value[2]): two cells, [0] low bits.
        cell_t N_SoundSetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetUInt64(LocalString(ctx, params[2]), ReadMask(ctx, params[3]));
            return 0;
        }

        cell_t N_SoundSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetFloat(LocalString(ctx, params[2]), sp_ctof(params[3]));
            return 0;
        }

        cell_t N_SoundSetFloat3(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                s->SetFloat3(LocalString(ctx, params[2]), Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_SoundGetBool(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s && s->GetBool(LocalString(ctx, params[2]), params[3] != 0);
        }

        cell_t N_SoundGetInt32(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? s->GetInt32(LocalString(ctx, params[2]), params[3]) : 0;
        }

        cell_t N_SoundGetUInt32(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetUInt32(LocalString(ctx, params[2]), static_cast<uint32_t>(params[3]))) : 0;
        }

        cell_t N_SoundGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? sp_ftoc(s->GetFloat(LocalString(ctx, params[2]), sp_ctof(params[3]))) : 0;
        }

        // GetUInt64(name, int value[2], const int def[2])
        cell_t N_SoundGetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                cell_t* out;
                ctx->LocalToPhysAddr(params[3], &out);
                WriteMask(out, s->GetUInt64(LocalString(ctx, params[2]), ReadMask(ctx, params[4])));
            }
            return 0;
        }

        // GetFloat3(name, float value[3], const float def[3]); scripts built before def pass none.
        cell_t N_SoundGetFloat3(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                float d[3] = { 0.0f, 0.0f, 0.0f };
                if (params[0] >= 4)
                    FromFloats(ctx, params[4], d, 3);
                const Vector v = s->GetFloat3(LocalString(ctx, params[2]), Vector(d[0], d[1], d[2]));
                const float f[3] = { v.x, v.y, v.z };
                ToFloats(ctx, params[3], f, 3);
            }
            return 0;
        }

        cell_t N_SoundAddRecipient(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->AddRecipient(CPlayerSlot(params[2]));
            return 0;
        }

        cell_t N_SoundRemoveRecipient(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->RemoveRecipient(CPlayerSlot(params[2]));
            return 0;
        }

        cell_t N_SoundHasRecipient(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s && s->HasRecipient(CPlayerSlot(params[2]));
        }

        cell_t N_SoundAddAllRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->AddAllRecipients();
            return 0;
        }

        cell_t N_SoundClearRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->ClearRecipients();
            return 0;
        }

        cell_t N_SoundSetRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetRecipients(ReadMask(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundGetRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                cell_t* m;
                ctx->LocalToPhysAddr(params[2], &m);
                WriteMask(m, s->GetRecipients());
            }
            return 0;
        }

        cell_t N_SoundEmit(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->Emit()) : 0;
        }

        const char* Channel(IPluginContext* ctx, cell_t addr)
        {
            const char* c = LocalString(ctx, addr);
            return *c ? c : nullptr;
        }

        // EmitSoundToPlayer(slot, name, volume = 1.0, pitch = 1.0, channel = "")
        cell_t N_EmitToPlayer(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().EmitSoundToPlayer(CPlayerSlot(params[1]), LocalString(ctx, params[2]), sp_ctof(params[3]),
                                                             sp_ctof(params[4]), Channel(ctx, params[5])));
        }

        cell_t N_EmitToPlayers(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().EmitSoundToPlayers(ReadMask(ctx, params[1]), LocalString(ctx, params[2]), sp_ctof(params[3]),
                                                              sp_ctof(params[4]), Channel(ctx, params[5])));
        }

        cell_t N_EmitToAll(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().EmitSoundToAll(LocalString(ctx, params[1]), sp_ctof(params[2]), sp_ctof(params[3]), Channel(ctx, params[4])));
        }

        // EmitSoundFromEntity(entity, name, volume = 1.0, pitch = 1.0, players = {-1, -1}, channel = "")
        cell_t N_EmitFromEntity(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = EntityArg(ctx, params[1]);
            if (!ent)
                return 0;
            return static_cast<cell_t>(SM().EmitSoundFromEntity(ent, LocalString(ctx, params[2]), sp_ctof(params[3]), sp_ctof(params[4]),
                                                               ReadMask(ctx, params[5]), Channel(ctx, params[6])));
        }

        cell_t N_StopSound(IPluginContext*, const cell_t* params)
        {
            SM().StopSound(static_cast<SoundGuid>(params[1]));
            return 0;
        }

        cell_t N_StopSoundForPlayers(IPluginContext* ctx, const cell_t* params)
        {
            SM().StopSoundForPlayers(static_cast<SoundGuid>(params[1]), ReadMask(ctx, params[2]));
            return 0;
        }

        cell_t N_StopSoundByName(IPluginContext* ctx, const cell_t* params)
        {
            // players: appended later, scripts built before it pass none (= everyone).
            SM().StopSoundByName(LocalString(ctx, params[1]), params[2], params[0] >= 3 ? ReadMask(ctx, params[3]) : ~SoundRecipients(0));
            return 0;
        }

        cell_t N_SetSoundVolume(IPluginContext*, const cell_t* params)
        {
            SM().SetSoundVolume(static_cast<SoundGuid>(params[1]), sp_ctof(params[2]));
            return 0;
        }

        cell_t N_UpdateSoundParams(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[2]))
                SM().UpdateSoundParams(static_cast<SoundGuid>(params[1]), s, params[0] >= 3 ? ReadMask(ctx, params[3]) : SOUND_RECIPIENTS_NONE);
            return 0;
        }

        cell_t N_HookSound(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            SoundHook handler([fn, owner](ISource2ModSound* sound) -> Action
            {
                const cell_t h = Handles().Create(LentSoundType(), sound, owner);
                sp::CallArgs args;
                args.PushCell(h);
                cell_t res = 0;
                Invoke(fn, args, &res);
                Handles().Free(h, 0, true);
                return ToAction(res);
            });
            return Remember(sounds::soundsManager.HookSound(owner, std::move(handler)), owner, HookKind::Sound);
        }

        cell_t N_UnhookSound(IPluginContext* ctx, const cell_t* params)
        {
            if (!Forget(ctx, params[1], HookKind::Sound))
                return 0;
            return sounds::soundsManager.UnhookSoundId(static_cast<Source2ModHookId>(params[1]));
        }

        cell_t N_HashSoundName(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().HashSoundName(LocalString(ctx, params[1])));
        }

        // LookupSoundName(hash, buffer, maxlen): false when the hash was never seen.
        cell_t N_LookupSoundName(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = SM().LookupSoundName(static_cast<uint32_t>(params[1]));
            ctx->StringToLocalUTF8(params[2], params[3], name ? name : "", nullptr);
            return name != nullptr;
        }

        cell_t N_SetPlayerVolume(IPluginContext* ctx, const cell_t* params)
        {
            SM().SetPlayerVolume(CPlayerSlot(params[1]), LocalString(ctx, params[2]), sp_ctof(params[3]));
            return 0;
        }

        cell_t N_GetPlayerVolume(IPluginContext* ctx, const cell_t* params)
        {
            return sp_ftoc(SM().GetPlayerVolume(CPlayerSlot(params[1]), LocalString(ctx, params[2])));
        }

        cell_t N_ResetPlayerVolumes(IPluginContext*, const cell_t* params)
        {
            SM().ResetPlayerVolumes(CPlayerSlot(params[1]));
            return 0;
        }

        const sp_nativeinfo_t g_soundNatives[] = {
            { "ISource2ModSound.New", N_SoundNew },
            { "ISource2ModSound.SetName", N_SoundSetName },
            { "ISource2ModSound.GetName", N_SoundGetName },
            { "ISource2ModSound.SetNameHash", N_SoundSetNameHash },
            { "ISource2ModSound.GetNameHash", N_SoundGetNameHash },
            { "ISource2ModSound.GetGuid", N_SoundGetGuid },
            { "ISource2ModSound.SetSourceEntityIndex", N_SoundSetSourceEntityIndex },
            { "ISource2ModSound.GetSourceEntityIndex", N_SoundGetSourceEntityIndex },
            { "ISource2ModSound.SetSourceEntity", N_SoundSetSourceEntity },
            { "ISource2ModSound.SetPosition", N_SoundSetPosition },
            { "ISource2ModSound.SetVolume", N_SoundSetVolume },
            { "ISource2ModSound.GetVolume", N_SoundGetVolume },
            { "ISource2ModSound.SetPitch", N_SoundSetPitch },
            { "ISource2ModSound.GetPitch", N_SoundGetPitch },
            { "ISource2ModSound.SetChannel", N_SoundSetChannel },
            { "ISource2ModSound.GetChannel", N_SoundGetChannel },
            { "ISource2ModSound.HasParam", N_SoundHasParam },
            { "ISource2ModSound.GetParamType", N_SoundGetParamType },
            { "ISource2ModSound.RemoveParam", N_SoundRemoveParam },
            { "ISource2ModSound.ClearParams", N_SoundClearParams },
            { "ISource2ModSound.SetBool", N_SoundSetBool },
            { "ISource2ModSound.SetInt32", N_SoundSetInt32 },
            { "ISource2ModSound.SetUInt32", N_SoundSetUInt32 },
            { "ISource2ModSound.SetUInt64", N_SoundSetUInt64 },
            { "ISource2ModSound.SetFloat", N_SoundSetFloat },
            { "ISource2ModSound.SetFloat3", N_SoundSetFloat3 },
            { "ISource2ModSound.GetBool", N_SoundGetBool },
            { "ISource2ModSound.GetInt32", N_SoundGetInt32 },
            { "ISource2ModSound.GetUInt32", N_SoundGetUInt32 },
            { "ISource2ModSound.GetUInt64", N_SoundGetUInt64 },
            { "ISource2ModSound.GetFloat", N_SoundGetFloat },
            { "ISource2ModSound.GetFloat3", N_SoundGetFloat3 },
            { "ISource2ModSound.AddRecipient", N_SoundAddRecipient },
            { "ISource2ModSound.RemoveRecipient", N_SoundRemoveRecipient },
            { "ISource2ModSound.HasRecipient", N_SoundHasRecipient },
            { "ISource2ModSound.AddAllRecipients", N_SoundAddAllRecipients },
            { "ISource2ModSound.ClearRecipients", N_SoundClearRecipients },
            { "ISource2ModSound.SetRecipients", N_SoundSetRecipients },
            { "ISource2ModSound.GetRecipients", N_SoundGetRecipients },
            { "ISource2ModSound.Emit", N_SoundEmit },
            { "Source2ModSounds.EmitSoundToPlayer", N_EmitToPlayer },
            { "Source2ModSounds.EmitSoundToPlayers", N_EmitToPlayers },
            { "Source2ModSounds.EmitSoundToAll", N_EmitToAll },
            { "Source2ModSounds.EmitSoundFromEntity", N_EmitFromEntity },
            { "Source2ModSounds.StopSound", N_StopSound },
            { "Source2ModSounds.StopSoundForPlayers", N_StopSoundForPlayers },
            { "Source2ModSounds.StopSoundByName", N_StopSoundByName },
            { "Source2ModSounds.SetSoundVolume", N_SetSoundVolume },
            { "Source2ModSounds.UpdateSoundParams", N_UpdateSoundParams },
            { "Source2ModSounds.HookSound", N_HookSound },
            { "Source2ModSounds.UnhookSound", N_UnhookSound },
            { "Source2ModSounds.HashSoundName", N_HashSoundName },
            { "Source2ModSounds.LookupSoundName", N_LookupSoundName },
            { "Source2ModSounds.SetPlayerVolume", N_SetPlayerVolume },
            { "Source2ModSounds.GetPlayerVolume", N_GetPlayerVolume },
            { "Source2ModSounds.ResetPlayerVolumes", N_ResetPlayerVolumes },
            { nullptr, nullptr },
        };
    }

    void ForgetSoundHooks(PluginId id)
    {
        // The core has taken these down already (it unhooks by owner); only the
        // bookkeeping is left.
        for (auto it = g_hooks.begin(); it != g_hooks.end();)
            it = it->second.owner == id ? g_hooks.erase(it) : std::next(it);
    }

    const sp_nativeinfo_t* GetSoundNatives()
    {
        return g_soundNatives;
    }
}
