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
 * SourcePawn natives for source2mod/json.inc on the core's JSON
 * (core/json.cpp). JSON values are lent handles kept by the document (or HTTP
 * response) they came from: the same value gives the same handle, and they go
 * with it.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"
#include "natives_json.h"

#include "core/json.h"
#include "core/sqlite.h"

#include <sp_callable.h>

#include <string>
#include <unordered_map>

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

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        void WriteInt64(IPluginContext* ctx, cell_t addr, int64_t value)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            out[0] = static_cast<cell_t>(static_cast<uint32_t>(value));
            out[1] = static_cast<cell_t>(static_cast<uint32_t>(static_cast<uint64_t>(value) >> 32));
        }

        // An optional int[2] argument (scripts built before it existed pass none): 0 when absent.
        int64_t ReadInt64Arg(IPluginContext* ctx, const cell_t* params, int index)
        {
            if (params[0] < index)
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[index], &in);
            return static_cast<int64_t>(static_cast<uint64_t>(static_cast<uint32_t>(in[0])) | (static_cast<uint64_t>(static_cast<uint32_t>(in[1])) << 32));
        }

        // ---- JSON ---------------------------------------------------------------------

        struct JsonVal : HandleObject
        {
            const ISource2ModJSONValue* value = nullptr;
            JsonValues* root = nullptr;
        };

        HandleType ValueType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModJSONValue", [](void* p) { delete static_cast<JsonVal*>(p); }, 0, false);
            return t;
        }

        struct JsonDoc : HandleObject
        {
            ISource2ModJSONDocument* doc = nullptr;
            JsonValues values;

            ~JsonDoc() override
            {
                values.FreeAll();
                if (doc)
                    doc->Release();
            }
        };

        HandleType DocType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModJSONDocument", [](void* p) { delete static_cast<JsonDoc*>(p); });
            return t;
        }

        JsonVal* ValOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<JsonVal*>(ReadHandle(ctx, handle, ValueType()));
        }

        JsonDoc* DocOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<JsonDoc*>(ReadHandle(ctx, handle, DocType()));
        }

#define S2M_JSON_TEST(name)                                                        \
        cell_t N_Json##name(IPluginContext* ctx, const cell_t* params)             \
        {                                                                          \
            JsonVal* v = ValOf(ctx, params[1]);                                    \
            return v && v->value->name();                                          \
        }
        S2M_JSON_TEST(IsNull)
        S2M_JSON_TEST(IsObject)
        S2M_JSON_TEST(IsArray)
        S2M_JSON_TEST(IsString)
        S2M_JSON_TEST(IsNumber)
        S2M_JSON_TEST(IsBool)
#undef S2M_JSON_TEST

        cell_t N_JsonHas(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v && v->value->Has(LocalString(ctx, params[2]));
        }

        cell_t N_JsonGet(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->root->For(v->value->Get(LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_JsonCount(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->value->Count() : 0;
        }

        cell_t N_JsonAt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->root->For(v->value->At(params[2])) : 0;
        }

        cell_t N_JsonKeyAt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[3], params[4], v->value->KeyAt(params[2])) : 0;
        }

        // AsString(buffer, maxlen, def)
        cell_t N_JsonAsString(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[2], params[3], v->value->AsString(LocalString(ctx, params[4]))) : 0;
        }

        cell_t N_JsonAsInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (JsonVal* v = ValOf(ctx, params[1]))
                WriteInt64(ctx, params[2], v->value->AsInt64(ReadInt64Arg(ctx, params, 3)));
            return 0;
        }

        cell_t N_JsonAsInt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->value->AsInt(params[2]) : 0;
        }

        cell_t N_JsonAsDouble(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(static_cast<float>(v->value->AsDouble(sp_ctof(params[2])))) : 0;
        }

        cell_t N_JsonAsFloat(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(v->value->AsFloat(sp_ctof(params[2]))) : 0;
        }

        cell_t N_JsonAsBool(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v && v->value->AsBool(params[2] != 0);
        }

        // GetString(key, buffer, maxlen, def)
        cell_t N_JsonGetString(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[3], params[4], v->value->GetString(LocalString(ctx, params[2]), LocalString(ctx, params[5]))) : 0;
        }

        cell_t N_JsonGetInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (JsonVal* v = ValOf(ctx, params[1]))
                WriteInt64(ctx, params[3], v->value->GetInt64(LocalString(ctx, params[2]), ReadInt64Arg(ctx, params, 4)));
            return 0;
        }

        cell_t N_JsonGetInt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->value->GetInt(LocalString(ctx, params[2]), params[3]) : 0;
        }

        cell_t N_JsonGetDouble(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(static_cast<float>(v->value->GetDouble(LocalString(ctx, params[2]), sp_ctof(params[3])))) : 0;
        }

        cell_t N_JsonGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(v->value->GetFloat(LocalString(ctx, params[2]), sp_ctof(params[3]))) : 0;
        }

        cell_t N_JsonGetBool(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v && v->value->GetBool(LocalString(ctx, params[2]), params[3] != 0);
        }

        // Dump(buffer, maxlen, indent)
        cell_t N_JsonDump(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[2], params[3], v->value->Dump(params[4])) : 0;
        }

        cell_t N_DocIsValid(IPluginContext* ctx, const cell_t* params)
        {
            JsonDoc* d = DocOf(ctx, params[1]);
            return d && d->doc->IsValid();
        }

        cell_t N_DocGetError(IPluginContext* ctx, const cell_t* params)
        {
            JsonDoc* d = DocOf(ctx, params[1]);
            return d ? ToBuffer(ctx, params[2], params[3], d->doc->GetError()) : 0;
        }

        cell_t N_DocRoot(IPluginContext* ctx, const cell_t* params)
        {
            JsonDoc* d = DocOf(ctx, params[1]);
            return d && d->doc->IsValid() ? d->values.For(d->doc->Root()) : 0;
        }

        // Parse(text, length)
        cell_t N_JsonParse(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModJSONDocument* parsed = json::jsonManager.Parse(LocalString(ctx, params[1]), params[2]);
            if (!parsed)
                return 0;
            auto* d = new JsonDoc();
            d->doc = parsed;
            d->values.owner = CallerId(ctx);
            const cell_t h = Handles().Create(DocType(), d, d->values.owner);
            if (!h)
                delete d;
            return h;
        }

        const sp_nativeinfo_t g_jsonNatives[] = {
            { "ISource2ModJSONValue.IsNull", N_JsonIsNull },
            { "ISource2ModJSONValue.IsObject", N_JsonIsObject },
            { "ISource2ModJSONValue.IsArray", N_JsonIsArray },
            { "ISource2ModJSONValue.IsString", N_JsonIsString },
            { "ISource2ModJSONValue.IsNumber", N_JsonIsNumber },
            { "ISource2ModJSONValue.IsBool", N_JsonIsBool },
            { "ISource2ModJSONValue.Has", N_JsonHas },
            { "ISource2ModJSONValue.Get", N_JsonGet },
            { "ISource2ModJSONValue.Count", N_JsonCount },
            { "ISource2ModJSONValue.At", N_JsonAt },
            { "ISource2ModJSONValue.KeyAt", N_JsonKeyAt },
            { "ISource2ModJSONValue.AsString", N_JsonAsString },
            { "ISource2ModJSONValue.AsInt64", N_JsonAsInt64 },
            { "ISource2ModJSONValue.AsInt", N_JsonAsInt },
            { "ISource2ModJSONValue.AsDouble", N_JsonAsDouble },
            { "ISource2ModJSONValue.AsFloat", N_JsonAsFloat },
            { "ISource2ModJSONValue.AsBool", N_JsonAsBool },
            { "ISource2ModJSONValue.GetString", N_JsonGetString },
            { "ISource2ModJSONValue.GetInt64", N_JsonGetInt64 },
            { "ISource2ModJSONValue.GetInt", N_JsonGetInt },
            { "ISource2ModJSONValue.GetDouble", N_JsonGetDouble },
            { "ISource2ModJSONValue.GetFloat", N_JsonGetFloat },
            { "ISource2ModJSONValue.GetBool", N_JsonGetBool },
            { "ISource2ModJSONValue.Dump", N_JsonDump },
            { "ISource2ModJSONDocument.IsValid", N_DocIsValid },
            { "ISource2ModJSONDocument.GetError", N_DocGetError },
            { "ISource2ModJSONDocument.Root", N_DocRoot },
            { "Source2ModJSON.Parse", N_JsonParse },
            { nullptr, nullptr },
        };
    }

    cell_t JsonValues::For(const ISource2ModJSONValue* value)
    {
        if (!value)
            return 0;
        const void* key = static_cast<const json::CJSONValue*>(value)->Node();
        auto it = handles.find(key);
        if (it != handles.end())
            return it->second;
        auto* obj = new JsonVal();
        obj->value = value;
        obj->root = this;
        const cell_t h = Handles().Create(ValueType(), obj, owner);
        if (!h)
        {
            delete obj;
            return 0;
        }
        handles.emplace(key, h);
        return h;
    }

    void JsonValues::FreeAll()
    {
        for (auto& [value, h] : handles)
            Handles().Free(h, 0, true);
        handles.clear();
    }

    const sp_nativeinfo_t* GetJsonNatives()
    {
        return g_jsonNatives;
    }
}
