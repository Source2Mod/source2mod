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
 * SourcePawn natives for source2mod/netmessages.inc on the core's network
 * messages manager (core/networkmessages.cpp).
 *
 * A NetMessage handle is either the script's own (allocated, freed with the
 * handle) or lent for a hook (valid inside it); nested messages are handles
 * that go with the one they were taken from.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/game/networkmessages.h"

#include <source2mod/sdk/schema/classes/CBaseEntity.h>

#include <sp_callable.h>

#include <algorithm>
#include <map>
#include <cstring>
#include <string>
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

        ISource2ModNetworkMessages& NM()
        {
            return networkmessages::networkMessagesManager;
        }

        // ---- NetMessage handles ---------------------------------------------------

        struct NetMsg : HandleObject
        {
            void* msg = nullptr;
            bool owned = false;
            PluginId owner = 0;
            std::vector<cell_t> children;   // nested-message handles taken from this one

            ~NetMsg() override
            {
                for (cell_t child : children)
                    Handles().Free(child, 0, true);
                if (owned && msg)
                    NM().DeallocateNetMessage(msg);
            }
        };

        HandleType NetMsgType()
        {
            static const HandleType t = Handles().CreateType("NetMessage", [](void* p) { delete static_cast<NetMsg*>(p); });
            return t;
        }

        // Lent ones: the same object, but a script cannot close them.
        HandleType LentNetMsgType()
        {
            static const HandleType t = Handles().CreateType("NetMessageLent", [](void* p) { delete static_cast<NetMsg*>(p); }, NetMsgType(), false);
            return t;
        }

        cell_t NewNetMsg(void* msg, bool owned, PluginId owner, bool lent)
        {
            auto* obj = new NetMsg();
            obj->msg = msg;
            obj->owned = owned;
            obj->owner = owner;
            const cell_t h = Handles().Create(lent ? LentNetMsgType() : NetMsgType(), obj, owner);
            if (!h)
            {
                obj->owned = false;
                delete obj;
                if (owned)
                    NM().DeallocateNetMessage(msg);
            }
            return h;
        }

        NetMsg* MsgOf(IPluginContext* ctx, cell_t handle)
        {
            auto* m = static_cast<NetMsg*>(ReadHandle(ctx, handle, NetMsgType()));
            if (m && !m->msg)
            {
                ctx->ThrowNativeError("NetMessage %x is empty", handle);
                return nullptr;
            }
            return m;
        }

        cell_t N_AllocateByID(IPluginContext* ctx, const cell_t* params)
        {
            void* msg = NM().AllocateNetMessageByID(params[1]);
            return msg ? NewNetMsg(msg, true, CallerId(ctx), false) : 0;
        }

        cell_t N_AllocateByName(IPluginContext* ctx, const cell_t* params)
        {
            void* msg = NM().AllocateNetMessageByPartialName(LocalString(ctx, params[1]));
            return msg ? NewNetMsg(msg, true, CallerId(ctx), false) : 0;
        }

        cell_t N_Deallocate(IPluginContext* ctx, const cell_t* params)
        {
            if (!ReadHandle(ctx, params[1], NetMsgType()))
                return 0;
            const HandleError err = Handles().Free(params[1], CallerId(ctx));
            if (err != HandleError::None)
                return ReportHandleError(ctx, params[1], err);
            return 0;
        }

        cell_t N_HasField(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m && NM().HasField(m->msg, LocalString(ctx, params[2]));
        }

        // Scalars: Get(msg, field), GetRepeated(msg, field, index), Set(msg, field, v),
        // SetRepeated(msg, field, index, v), Add(msg, field, v).
#define NETMSG_SCALAR(Name, ToCell, FromCell)                                                                                 \
        cell_t N_Get##Name(IPluginContext* ctx, const cell_t* params)                                                         \
        {                                                                                                                     \
            NetMsg* m = MsgOf(ctx, params[1]);                                                                                \
            return m ? ToCell(NM().Get##Name(m->msg, LocalString(ctx, params[2]))) : 0;                                       \
        }                                                                                                                     \
        cell_t N_GetRepeated##Name(IPluginContext* ctx, const cell_t* params)                                                 \
        {                                                                                                                     \
            NetMsg* m = MsgOf(ctx, params[1]);                                                                                \
            return m ? ToCell(NM().GetRepeated##Name(m->msg, LocalString(ctx, params[2]), params[3])) : 0;                    \
        }                                                                                                                     \
        cell_t N_Set##Name(IPluginContext* ctx, const cell_t* params)                                                         \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                NM().Set##Name(m->msg, LocalString(ctx, params[2]), FromCell(params[3]));                                     \
            return 0;                                                                                                         \
        }                                                                                                                     \
        cell_t N_SetRepeated##Name(IPluginContext* ctx, const cell_t* params)                                                 \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                NM().SetRepeated##Name(m->msg, LocalString(ctx, params[2]), params[3], FromCell(params[4]));                  \
            return 0;                                                                                                         \
        }                                                                                                                     \
        cell_t N_Add##Name(IPluginContext* ctx, const cell_t* params)                                                         \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                NM().Add##Name(m->msg, LocalString(ctx, params[2]), FromCell(params[3]));                                     \
            return 0;                                                                                                         \
        }

        cell_t IntToCell(int v) { return static_cast<cell_t>(v); }
        cell_t UIntToCell(uint32_t v) { return static_cast<cell_t>(v); }
        cell_t BoolToCell(bool v) { return v ? 1 : 0; }
        cell_t FloatToCell(float v) { return sp_ftoc(v); }
        cell_t DoubleToCell(double v) { return sp_ftoc(static_cast<float>(v)); }
        int CellToInt(cell_t v) { return v; }
        uint32_t CellToUInt(cell_t v) { return static_cast<uint32_t>(v); }
        bool CellToBool(cell_t v) { return v != 0; }
        float CellToFloat(cell_t v) { return sp_ctof(v); }
        double CellToDouble(cell_t v) { return sp_ctof(v); }

        NETMSG_SCALAR(Int32, IntToCell, CellToInt)
        NETMSG_SCALAR(UInt32, UIntToCell, CellToUInt)
        NETMSG_SCALAR(Bool, BoolToCell, CellToBool)
        NETMSG_SCALAR(Float, FloatToCell, CellToFloat)
        NETMSG_SCALAR(Double, DoubleToCell, CellToDouble)
#undef NETMSG_SCALAR

        // 64-bit fields as decimal text.
        cell_t N_GetInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[3], params[4], std::to_string(NM().GetInt64(m->msg, LocalString(ctx, params[2]))).c_str(), nullptr);
            return 0;
        }

        cell_t N_SetInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetInt64(m->msg, LocalString(ctx, params[2]), strtoll(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_AddInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddInt64(m->msg, LocalString(ctx, params[2]), strtoll(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_GetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[3], params[4], std::to_string(NM().GetUInt64(m->msg, LocalString(ctx, params[2]))).c_str(), nullptr);
            return 0;
        }

        cell_t N_SetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetUInt64(m->msg, LocalString(ctx, params[2]), strtoull(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_AddUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddUInt64(m->msg, LocalString(ctx, params[2]), strtoull(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        // GetRepeatedInt64(msg, field, index, buffer, maxlen)
        cell_t N_GetRepeatedInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[4], params[5], std::to_string(NM().GetRepeatedInt64(m->msg, LocalString(ctx, params[2]), params[3])).c_str(), nullptr);
            return 0;
        }

        // SetRepeatedInt64(msg, field, index, value)
        cell_t N_SetRepeatedInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetRepeatedInt64(m->msg, LocalString(ctx, params[2]), params[3], strtoll(LocalString(ctx, params[4]), nullptr, 10));
            return 0;
        }

        cell_t N_GetRepeatedUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[4], params[5], std::to_string(NM().GetRepeatedUInt64(m->msg, LocalString(ctx, params[2]), params[3])).c_str(), nullptr);
            return 0;
        }

        cell_t N_SetRepeatedUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetRepeatedUInt64(m->msg, LocalString(ctx, params[2]), params[3], strtoull(LocalString(ctx, params[4]), nullptr, 10));
            return 0;
        }

        cell_t N_GetString(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], NM().GetString(m->msg, LocalString(ctx, params[2])).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_GetRepeatedString(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[4], params[5], NM().GetRepeatedString(m->msg, LocalString(ctx, params[2]), params[3]).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SetString(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetString(m->msg, LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_SetRepeatedString(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetRepeatedString(m->msg, LocalString(ctx, params[2]), params[3], LocalString(ctx, params[4]));
            return 0;
        }

        cell_t N_AddString(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddString(m->msg, LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
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

        cell_t N_GetVector(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const Vector v = NM().GetVector(m->msg, LocalString(ctx, params[2]));
                const float f[3] = { v.x, v.y, v.z };
                ToFloats(ctx, params[3], f, 3);
            }
            return 0;
        }

        cell_t N_SetVector(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                NM().SetVector(m->msg, LocalString(ctx, params[2]), Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_AddVector(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                NM().AddVector(m->msg, LocalString(ctx, params[2]), Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_GetQAngle(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const QAngle a = NM().GetQAngle(m->msg, LocalString(ctx, params[2]));
                const float f[3] = { a.x, a.y, a.z };
                ToFloats(ctx, params[3], f, 3);
            }
            return 0;
        }

        cell_t N_SetQAngle(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                NM().SetQAngle(m->msg, LocalString(ctx, params[2]), QAngle(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_GetVector2D(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const Vector2D v = NM().GetVector2D(m->msg, LocalString(ctx, params[2]));
                const float f[2] = { v.x, v.y };
                ToFloats(ctx, params[3], f, 2);
            }
            return 0;
        }

        cell_t N_SetVector2D(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[2];
                FromFloats(ctx, params[3], f, 2);
                NM().SetVector2D(m->msg, LocalString(ctx, params[2]), Vector2D(f[0], f[1]));
            }
            return 0;
        }

        cell_t N_GetColor(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const Color c = NM().GetColor(m->msg, LocalString(ctx, params[2]));
                cell_t* out;
                ctx->LocalToPhysAddr(params[3], &out);
                out[0] = c.r();
                out[1] = c.g();
                out[2] = c.b();
                out[3] = c.a();
            }
            return 0;
        }

        cell_t N_SetColor(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                cell_t* in;
                ctx->LocalToPhysAddr(params[3], &in);
                NM().SetColor(m->msg, LocalString(ctx, params[2]), Color(in[0], in[1], in[2], in[3]));
            }
            return 0;
        }

        // Repeated elements and Add for the vector types: GetRepeated(msg, field, index, out),
        // SetRepeated(msg, field, index, in), Add(msg, field, in).
        void ToCells(IPluginContext* ctx, cell_t addr, const Vector2D& v)
        {
            const float f[2] = { v.x, v.y };
            ToFloats(ctx, addr, f, 2);
        }

        void ToCells(IPluginContext* ctx, cell_t addr, const Vector& v)
        {
            const float f[3] = { v.x, v.y, v.z };
            ToFloats(ctx, addr, f, 3);
        }

        void ToCells(IPluginContext* ctx, cell_t addr, const QAngle& a)
        {
            const float f[3] = { a.x, a.y, a.z };
            ToFloats(ctx, addr, f, 3);
        }

        void ToCells(IPluginContext* ctx, cell_t addr, const Color& c)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            out[0] = c.r();
            out[1] = c.g();
            out[2] = c.b();
            out[3] = c.a();
        }

        template <typename T>
        T FromCells(IPluginContext* ctx, cell_t addr);

        template <>
        Vector2D FromCells<Vector2D>(IPluginContext* ctx, cell_t addr)
        {
            float f[2];
            FromFloats(ctx, addr, f, 2);
            return Vector2D(f[0], f[1]);
        }

        template <>
        Vector FromCells<Vector>(IPluginContext* ctx, cell_t addr)
        {
            float f[3];
            FromFloats(ctx, addr, f, 3);
            return Vector(f[0], f[1], f[2]);
        }

        template <>
        QAngle FromCells<QAngle>(IPluginContext* ctx, cell_t addr)
        {
            float f[3];
            FromFloats(ctx, addr, f, 3);
            return QAngle(f[0], f[1], f[2]);
        }

        template <>
        Color FromCells<Color>(IPluginContext* ctx, cell_t addr)
        {
            cell_t* in;
            ctx->LocalToPhysAddr(addr, &in);
            return Color(in[0], in[1], in[2], in[3]);
        }

#define NETMSG_VECTOR_REPEATED(Name, Type)                                                                                   \
        cell_t N_GetRepeated##Name(IPluginContext* ctx, const cell_t* params)                                                 \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                ToCells(ctx, params[4], NM().GetRepeated##Name(m->msg, LocalString(ctx, params[2]), params[3]));              \
            return 0;                                                                                                         \
        }                                                                                                                     \
        cell_t N_SetRepeated##Name(IPluginContext* ctx, const cell_t* params)                                                 \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                NM().SetRepeated##Name(m->msg, LocalString(ctx, params[2]), params[3], FromCells<Type>(ctx, params[4]));      \
            return 0;                                                                                                         \
        }

        NETMSG_VECTOR_REPEATED(Vector2D, Vector2D)
        NETMSG_VECTOR_REPEATED(Vector, Vector)
        NETMSG_VECTOR_REPEATED(QAngle, QAngle)
        NETMSG_VECTOR_REPEATED(Color, Color)
#undef NETMSG_VECTOR_REPEATED

        cell_t N_AddVector2D(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddVector2D(m->msg, LocalString(ctx, params[2]), FromCells<Vector2D>(ctx, params[3]));
            return 0;
        }

        cell_t N_AddQAngle(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddQAngle(m->msg, LocalString(ctx, params[2]), FromCells<QAngle>(ctx, params[3]));
            return 0;
        }

        cell_t N_AddColor(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddColor(m->msg, LocalString(ctx, params[2]), FromCells<Color>(ctx, params[3]));
            return 0;
        }

        // Bytes into a script buffer: the interface writes the whole field, so it
        // is sized first (a null out only measures). index -1 = not repeated.
        cell_t CopyBytes(IPluginContext* ctx, void* msg, const char* field, int index, cell_t addr, cell_t maxlen)
        {
            if (maxlen <= 0)
                return 0;
            const int size = index < 0 ? NM().GetBytes(nullptr, msg, field) : NM().GetRepeatedBytes(nullptr, msg, field, index);
            if (size <= 0)
                return 0;
            std::vector<uint8_t> tmp(static_cast<size_t>(size));
            const int n = index < 0 ? NM().GetBytes(tmp.data(), msg, field) : NM().GetRepeatedBytes(tmp.data(), msg, field, index);
            char* dest;
            ctx->LocalToString(addr, &dest);
            const int copy = std::min({ n, size, maxlen });
            memcpy(dest, tmp.data(), static_cast<size_t>(std::max(copy, 0)));
            return copy;
        }

        // GetBytes(msg, field, char[] buffer, maxlen): raw bytes
        cell_t N_GetBytes(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? CopyBytes(ctx, m->msg, LocalString(ctx, params[2]), -1, params[3], params[4]) : 0;
        }

        // GetRepeatedBytes(msg, field, index, char[] buffer, maxlen)
        cell_t N_GetRepeatedBytes(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m && params[3] >= 0 ? CopyBytes(ctx, m->msg, LocalString(ctx, params[2]), params[3], params[4], params[5]) : 0;
        }

        cell_t N_SetBytes(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            char* src;
            ctx->LocalToString(params[3], &src);
            NM().SetBytes(m->msg, LocalString(ctx, params[2]), src, params[4]);
            return 0;
        }

        // SetRepeatedBytes(msg, field, index, value, length)
        cell_t N_SetRepeatedBytes(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            char* src;
            ctx->LocalToString(params[4], &src);
            NM().SetRepeatedBytes(m->msg, LocalString(ctx, params[2]), params[3], src, params[5]);
            return 0;
        }

        // AddBytes(msg, field, value, length)
        cell_t N_AddBytes(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            char* src;
            ctx->LocalToString(params[3], &src);
            NM().AddBytes(m->msg, LocalString(ctx, params[2]), src, params[4]);
            return 0;
        }

        cell_t Nested(IPluginContext* ctx, cell_t parentHandle, void* nested)
        {
            auto* parent = static_cast<NetMsg*>(Handles().Read(parentHandle, NetMsgType()));
            if (!parent || !nested)
                return 0;
            const cell_t h = NewNetMsg(nested, false, parent->owner, true);
            if (h)
                parent->children.push_back(h);
            return h;
        }

        cell_t N_GetNested(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? Nested(ctx, params[1], NM().GetNestedMessage(m->msg, LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_GetRepeatedNested(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? Nested(ctx, params[1], NM().GetRepeatedNestedMessage(m->msg, LocalString(ctx, params[2]), params[3])) : 0;
        }

        cell_t N_AddNested(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? Nested(ctx, params[1], NM().AddNestedMessage(m->msg, LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_GetRepeatedFieldSize(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? NM().GetRepeatedFieldSize(m->msg, LocalString(ctx, params[2])) : 0;
        }

        cell_t N_ClearRepeatedField(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().ClearRepeatedField(m->msg, LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_Clear(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().Clear(m->msg);
            return 0;
        }

        cell_t N_SendMessage(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SendMessage(m->msg, params[2], CPlayerSlot(params[3]));
            return 0;
        }

        cell_t N_SendMessageToPlayers(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SendMessageToPlayers(m->msg, params[2], ReadMask(ctx, params[3]));
            return 0;
        }

        // Hook handlers: the message lent as a handle for the call.
        struct LentMsg
        {
            cell_t handle;
            LentMsg(void* msg, PluginId owner) : handle(NewNetMsg(msg, false, owner, true)) {}
            ~LentMsg() { Handles().Free(handle, 0, true); }
        };

        Action ToAction(cell_t res)
        {
            return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
        }

        // The scripts' hook ids by kind: an Unhook* takes only an id of its own
        // kind that the calling script made.
        enum class HookKind
        {
            ServerMessage,
            ClientMessage,
            ServerInternalMessage,
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

        // The optional messageId of the Hook* natives; -1 (every message) for a
        // script compiled before it existed.
        int OnlyMessage(const cell_t* params, int index)
        {
            return params[0] >= index ? static_cast<int>(params[index]) : -1;
        }

        cell_t N_HookServerMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            const int only = OnlyMessage(params, 2);
            NetMessageServerHook handler([fn, owner, only](uint64_t* clients, int messageId, void* msg) -> Action
            {
                // Ahead of the handle and the call: an unwanted message costs
                // a compare, not a trip through the VM.
                if (only != -1 && messageId != only)
                    return Action::Ignore;
                LentMsg lent(msg, owner);
                cell_t mask[2];
                WriteMask(mask, *clients);
                sp::CallArgs args;
                args.PushArray(mask, 2, SM_PARAM_COPYBACK);
                args.PushCell(messageId);
                args.PushCell(lent.handle);
                cell_t res = 0;
                Invoke(fn, args, &res);
                *clients = static_cast<uint64_t>(static_cast<uint32_t>(mask[0])) | (static_cast<uint64_t>(static_cast<uint32_t>(mask[1])) << 32);
                return ToAction(res);
            });
            return Remember(networkmessages::networkMessagesManager.HookServerMessage(owner, std::move(handler)), owner, HookKind::ServerMessage);
        }

        NetMessageClientHook ClientHook(IPluginFunction* fn, PluginId owner, int only)
        {
            return NetMessageClientHook([fn, owner, only](CPlayerSlot slot, int messageId, void* msg) -> Action
            {
                if (only != -1 && messageId != only)
                    return Action::Ignore;
                LentMsg lent(msg, owner);
                sp::CallArgs args;
                args.PushCell(slot.Get());
                args.PushCell(messageId);
                args.PushCell(lent.handle);
                cell_t res = 0;
                Invoke(fn, args, &res);
                return ToAction(res);
            });
        }

        cell_t N_HookClientMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return Remember(networkmessages::networkMessagesManager.HookClientMessage(owner, ClientHook(fn, owner, OnlyMessage(params, 2))), owner,
                            HookKind::ClientMessage);
        }

        cell_t N_HookServerInternalMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return Remember(networkmessages::networkMessagesManager.HookServerInternalMessage(owner, ClientHook(fn, owner, OnlyMessage(params, 2))), owner,
                            HookKind::ServerInternalMessage);
        }

        template <HookKind KIND>
        cell_t N_UnhookNetMessage(IPluginContext* ctx, const cell_t* params)
        {
            if (!Forget(ctx, params[1], KIND))
                return 0;
            return networkmessages::networkMessagesManager.UnhookNetMessage(static_cast<Source2ModHookId>(params[1]));
        }

        const sp_nativeinfo_t g_networkMessageNatives[] = {
            { "Source2ModNetworkMessages.AllocateNetMessageByID", N_AllocateByID },
            { "Source2ModNetworkMessages.AllocateNetMessageByPartialName", N_AllocateByName },
            { "Source2ModNetworkMessages.DeallocateNetMessage", N_Deallocate },
            { "Source2ModNetworkMessages.HasField", N_HasField },
            { "Source2ModNetworkMessages.GetInt32", N_GetInt32 },
            { "Source2ModNetworkMessages.GetRepeatedInt32", N_GetRepeatedInt32 },
            { "Source2ModNetworkMessages.SetInt32", N_SetInt32 },
            { "Source2ModNetworkMessages.SetRepeatedInt32", N_SetRepeatedInt32 },
            { "Source2ModNetworkMessages.AddInt32", N_AddInt32 },
            { "Source2ModNetworkMessages.GetUInt32", N_GetUInt32 },
            { "Source2ModNetworkMessages.GetRepeatedUInt32", N_GetRepeatedUInt32 },
            { "Source2ModNetworkMessages.SetUInt32", N_SetUInt32 },
            { "Source2ModNetworkMessages.SetRepeatedUInt32", N_SetRepeatedUInt32 },
            { "Source2ModNetworkMessages.AddUInt32", N_AddUInt32 },
            { "Source2ModNetworkMessages.GetInt64", N_GetInt64 },
            { "Source2ModNetworkMessages.SetInt64", N_SetInt64 },
            { "Source2ModNetworkMessages.AddInt64", N_AddInt64 },
            { "Source2ModNetworkMessages.GetRepeatedInt64", N_GetRepeatedInt64 },
            { "Source2ModNetworkMessages.SetRepeatedInt64", N_SetRepeatedInt64 },
            { "Source2ModNetworkMessages.GetUInt64", N_GetUInt64 },
            { "Source2ModNetworkMessages.SetUInt64", N_SetUInt64 },
            { "Source2ModNetworkMessages.AddUInt64", N_AddUInt64 },
            { "Source2ModNetworkMessages.GetRepeatedUInt64", N_GetRepeatedUInt64 },
            { "Source2ModNetworkMessages.SetRepeatedUInt64", N_SetRepeatedUInt64 },
            { "Source2ModNetworkMessages.GetBool", N_GetBool },
            { "Source2ModNetworkMessages.GetRepeatedBool", N_GetRepeatedBool },
            { "Source2ModNetworkMessages.SetBool", N_SetBool },
            { "Source2ModNetworkMessages.SetRepeatedBool", N_SetRepeatedBool },
            { "Source2ModNetworkMessages.AddBool", N_AddBool },
            { "Source2ModNetworkMessages.GetFloat", N_GetFloat },
            { "Source2ModNetworkMessages.GetRepeatedFloat", N_GetRepeatedFloat },
            { "Source2ModNetworkMessages.SetFloat", N_SetFloat },
            { "Source2ModNetworkMessages.SetRepeatedFloat", N_SetRepeatedFloat },
            { "Source2ModNetworkMessages.AddFloat", N_AddFloat },
            { "Source2ModNetworkMessages.GetDouble", N_GetDouble },
            { "Source2ModNetworkMessages.GetRepeatedDouble", N_GetRepeatedDouble },
            { "Source2ModNetworkMessages.SetDouble", N_SetDouble },
            { "Source2ModNetworkMessages.SetRepeatedDouble", N_SetRepeatedDouble },
            { "Source2ModNetworkMessages.AddDouble", N_AddDouble },
            { "Source2ModNetworkMessages.GetString", N_GetString },
            { "Source2ModNetworkMessages.GetRepeatedString", N_GetRepeatedString },
            { "Source2ModNetworkMessages.SetString", N_SetString },
            { "Source2ModNetworkMessages.SetRepeatedString", N_SetRepeatedString },
            { "Source2ModNetworkMessages.AddString", N_AddString },
            { "Source2ModNetworkMessages.GetVector", N_GetVector },
            { "Source2ModNetworkMessages.GetRepeatedVector", N_GetRepeatedVector },
            { "Source2ModNetworkMessages.SetVector", N_SetVector },
            { "Source2ModNetworkMessages.SetRepeatedVector", N_SetRepeatedVector },
            { "Source2ModNetworkMessages.AddVector", N_AddVector },
            { "Source2ModNetworkMessages.GetQAngle", N_GetQAngle },
            { "Source2ModNetworkMessages.GetRepeatedQAngle", N_GetRepeatedQAngle },
            { "Source2ModNetworkMessages.SetQAngle", N_SetQAngle },
            { "Source2ModNetworkMessages.SetRepeatedQAngle", N_SetRepeatedQAngle },
            { "Source2ModNetworkMessages.AddQAngle", N_AddQAngle },
            { "Source2ModNetworkMessages.GetVector2D", N_GetVector2D },
            { "Source2ModNetworkMessages.GetRepeatedVector2D", N_GetRepeatedVector2D },
            { "Source2ModNetworkMessages.SetVector2D", N_SetVector2D },
            { "Source2ModNetworkMessages.SetRepeatedVector2D", N_SetRepeatedVector2D },
            { "Source2ModNetworkMessages.AddVector2D", N_AddVector2D },
            { "Source2ModNetworkMessages.GetColor", N_GetColor },
            { "Source2ModNetworkMessages.GetRepeatedColor", N_GetRepeatedColor },
            { "Source2ModNetworkMessages.SetColor", N_SetColor },
            { "Source2ModNetworkMessages.SetRepeatedColor", N_SetRepeatedColor },
            { "Source2ModNetworkMessages.AddColor", N_AddColor },
            { "Source2ModNetworkMessages.GetBytes", N_GetBytes },
            { "Source2ModNetworkMessages.GetRepeatedBytes", N_GetRepeatedBytes },
            { "Source2ModNetworkMessages.SetBytes", N_SetBytes },
            { "Source2ModNetworkMessages.SetRepeatedBytes", N_SetRepeatedBytes },
            { "Source2ModNetworkMessages.AddBytes", N_AddBytes },
            { "Source2ModNetworkMessages.GetNestedMessage", N_GetNested },
            { "Source2ModNetworkMessages.GetRepeatedNestedMessage", N_GetRepeatedNested },
            { "Source2ModNetworkMessages.AddNestedMessage", N_AddNested },
            { "Source2ModNetworkMessages.GetRepeatedFieldSize", N_GetRepeatedFieldSize },
            { "Source2ModNetworkMessages.ClearRepeatedField", N_ClearRepeatedField },
            { "Source2ModNetworkMessages.Clear", N_Clear },
            { "Source2ModNetworkMessages.SendMessage", N_SendMessage },
            { "Source2ModNetworkMessages.SendMessageToPlayers", N_SendMessageToPlayers },
            { "Source2ModNetworkMessages.HookServerMessage", N_HookServerMessage },
            { "Source2ModNetworkMessages.UnhookServerMessage", N_UnhookNetMessage<HookKind::ServerMessage> },
            { "Source2ModNetworkMessages.HookClientMessage", N_HookClientMessage },
            { "Source2ModNetworkMessages.UnhookClientMessage", N_UnhookNetMessage<HookKind::ClientMessage> },
            { "Source2ModNetworkMessages.HookServerInternalMessage", N_HookServerInternalMessage },
            { "Source2ModNetworkMessages.UnhookServerInternalMessage", N_UnhookNetMessage<HookKind::ServerInternalMessage> },
            { nullptr, nullptr },
        };
    }

    void ForgetNetworkMessageHooks(PluginId id)
    {
        // The core has taken these down already (it unhooks by owner); only the
        // bookkeeping is left.
        for (auto it = g_hooks.begin(); it != g_hooks.end();)
            it = it->second.owner == id ? g_hooks.erase(it) : std::next(it);
    }

    void* NetMessagePointer(IPluginContext* ctx, cell_t handle)
    {
        NetMsg* m = MsgOf(ctx, handle);
        return m ? m->msg : nullptr;
    }

    const sp_nativeinfo_t* GetNetworkMessageNatives()
    {
        return g_networkMessageNatives;
    }
}
