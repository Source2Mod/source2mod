/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * ConVar natives (convars.inc, AutoExecConfig from source2mod.inc) on the
 * core's ConVars manager. SourceMod's smn_console.cpp / ConVarManager
 * (AlliedModders LLC) are the reference: one handle per ConVar, owned by the
 * core and never closed, the same handle from CreateConVar and FindConVar,
 * change hooks per plugin and callback.
 *
 * CS2 ConVars are typed where Source 1's were strings. A script's ConVar is
 * made an int when its default and bounds are whole numbers, a float when
 * they are numbers, a string otherwise; the getters and setters convert
 * either way.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/game/convars.h"
#include "utils/log.h"

#include <sp_callable.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // Through the interface, whose overloads have the slot default.
        ISource2ModConVars& CV()
        {
            return convars::convarsManager;
        }

        std::unordered_map<uint16, cell_t> g_conVarHandles;

        // ConVar.AddChangeHook / HookConVarChange: by plugin, ConVar and callback.
        std::map<std::tuple<PluginId, uint16, funcid_t>, Source2ModHookId> g_changeHooks;

        // A ConVar handle holds its access index + 1, so it is never null.
        uint16 IndexOf(IPluginContext* ctx, cell_t handle, bool* ok)
        {
            void* object = ReadHandle(ctx, handle, Types().conVar);
            *ok = object != nullptr;
            return object ? static_cast<uint16>(reinterpret_cast<uintptr_t>(object) - 1) : SOURCE2MOD_INVALID_CONVAR_INDEX;
        }

#define CONVAR_OR_RETURN(idx, handle, ret)          \
        bool idx##_ok;                              \
        const uint16 idx = IndexOf(ctx, handle, &idx##_ok); \
        if (!idx##_ok)                              \
            return ret

        bool IsWholeNumber(const char* s)
        {
            if (!*s)
                return false;
            char* end;
            strtol(s, &end, 10);
            return *end == '\0';
        }

        bool IsNumber(const char* s)
        {
            if (!*s)
                return false;
            char* end;
            strtof(s, &end);
            return *end == '\0';
        }

        bool IsWhole(float v)
        {
            return std::floor(v) == v;
        }

        cell_t N_GetBool(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            // A string ConVar reads as a number, as Source 1's did.
            if (CV().GetType(idx) == EConVarType_String)
                return atoi(CV().GetString(idx)) != 0;
            return CV().GetBool(idx);
        }

        cell_t N_GetInt(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const EConVarType type = CV().GetType(idx);
            if (type == EConVarType_String)
                return atoi(CV().GetString(idx));
            if (type == EConVarType_Float32 || type == EConVarType_Float64)
                return static_cast<cell_t>(CV().GetFloat(idx));
            return CV().GetInt(idx);
        }

        cell_t N_GetFloat(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const EConVarType type = CV().GetType(idx);
            if (type == EConVarType_String)
                return sp_ftoc(static_cast<float>(atof(CV().GetString(idx))));
            if (type == EConVarType_Float32 || type == EConVarType_Float64)
                return sp_ftoc(CV().GetFloat(idx));
            return sp_ftoc(static_cast<float>(CV().GetInt(idx)));
        }

        void SetValueString(uint16 idx, const char* value)
        {
            CV().SetString(idx, value);
        }

        // A number into whatever type the ConVar is; a string ConVar gets
        // the text Source 1 would have stored.
        void SetNumber(uint16 idx, double value, const char* text)
        {
            switch (CV().GetType(idx))
            {
            case EConVarType_Float32:
                CV().SetFloat(idx, static_cast<float>(value));
                break;
            case EConVarType_Float64:
                CV().SetDouble(idx, value);
                break;
            case EConVarType_Bool:
                CV().SetBool(idx, value != 0.0);
                break;
            case EConVarType_Int16:
            case EConVarType_UInt16:
            case EConVarType_Int32:
            case EConVarType_UInt32:
            case EConVarType_Int64:
            case EConVarType_UInt64:
                CV().SetInt(idx, static_cast<int32>(value));
                break;
            default:
                CV().SetString(idx, text);
                break;
            }
        }

        cell_t N_SetBool(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetNumber(idx, params[2] ? 1.0 : 0.0, params[2] ? "1" : "0");
            return 0;
        }

        cell_t N_SetInt(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetNumber(idx, params[2], std::to_string(params[2]).c_str());
            return 0;
        }

        cell_t N_SetFloat(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            char buf[64];
            snprintf(buf, sizeof(buf), "%f", static_cast<double>(sp_ctof(params[2])));
            SetNumber(idx, sp_ctof(params[2]), buf);
            return 0;
        }

        cell_t N_SetString(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetValueString(idx, LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_GetString(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            ctx->StringToLocalUTF8(params[2], params[3], CV().GetString(idx), nullptr);
            return 0;
        }

        std::string DefaultOf(uint16 idx)
        {
            ConVarRefAbstract ref(idx);
            if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
                return {};
            CBufferString buf;
            ref.GetDefaultAsString(buf);
            return buf.Get();
        }

        cell_t N_RestoreDefault(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetValueString(idx, DefaultOf(idx).c_str());
            return 0;
        }

        cell_t N_GetDefault(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], DefaultOf(idx).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_GetFlags(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            return static_cast<cell_t>(CV().GetFlags(idx) & 0xFFFFFFFFu);
        }

        // The upper 32 flag bits are CS2's own; a script only sees and sets the lower.
        cell_t N_SetFlags(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const uint64 high = CV().GetFlags(idx) & ~0xFFFFFFFFull;
            CV().SetFlags(idx, high | static_cast<uint32_t>(params[2]));
            return 0;
        }

        // GetBounds(ConVarBounds type, float &value): ConVarBound_Upper = 0, ConVarBound_Lower.
        cell_t N_GetBounds(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            ConVarRefAbstract ref(idx);
            if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
                return 0;

            const bool upper = params[2] == 0;
            if (upper ? !ref.HasMax() : !ref.HasMin())
                return 0;

            CBufferString buf;
            if (upper)
                ref.GetMaxAsString(buf);
            else
                ref.GetMinAsString(buf);

            cell_t* out;
            ctx->LocalToPhysAddr(params[3], &out);
            *out = sp_ftoc(static_cast<float>(atof(buf.Get())));
            return 1;
        }

        cell_t N_GetName(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            ctx->StringToLocalUTF8(params[2], params[3], CV().GetName(idx), nullptr);
            return 0;
        }

        cell_t N_GetDescription(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const char* help = CV().GetHelpText(idx);
            ctx->StringToLocalUTF8(params[2], params[3], help ? help : "", nullptr);
            return 0;
        }

        // void (ConVar convar, const char[] oldValue, const char[] newValue)
        cell_t HookChange(IPluginContext* ctx, cell_t handle, cell_t funcId)
        {
            CONVAR_OR_RETURN(idx, handle, 0);
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(funcId);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", funcId);

            const auto key = std::make_tuple(plugin->Id(), idx, static_cast<funcid_t>(funcId));
            if (g_changeHooks.count(key))
                return 0;

            ConVarChangeHandler handler([idx, handle, fn](ConVarRefAbstract* ref, CSplitScreenSlot, const char* newValue, const char* oldValue)
            {
                if (!ref || ref->GetAccessIndex() != idx)
                    return;
                // The engine reports every change, also a set to the same value.
                if (newValue && oldValue && strcmp(newValue, oldValue) == 0)
                    return;

                sp::CallArgs args;
                args.PushCell(handle);
                args.PushString(oldValue ? oldValue : "");
                args.PushString(newValue ? newValue : "");
                Invoke(fn, args);
            });

            g_changeHooks[key] = convars::convarsManager.HookConVarChange(plugin->Id(), std::move(handler));
            return 0;
        }

        cell_t UnhookChange(IPluginContext* ctx, cell_t handle, cell_t funcId)
        {
            CONVAR_OR_RETURN(idx, handle, 0);
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            auto it = g_changeHooks.find(std::make_tuple(plugin ? plugin->Id() : 0, idx, static_cast<funcid_t>(funcId)));
            if (it == g_changeHooks.end())
                return ctx->ThrowNativeError("No active hook on convar \"%s\" with callback %x", CV().GetName(idx), funcId);
            convars::convarsManager.UnhookConVarChangeId(it->second);
            g_changeHooks.erase(it);
            return 0;
        }

        cell_t N_AddChangeHook(IPluginContext* ctx, const cell_t* params) { return HookChange(ctx, params[1], params[2]); }
        cell_t N_RemoveChangeHook(IPluginContext* ctx, const cell_t* params) { return UnhookChange(ctx, params[1], params[2]); }

        // AutoExecConfig(bool autoCreate=true, const char[] name="", const char[] folder="source2mod")
        cell_t N_AutoExecConfig(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            if (!plugin)
                return 0;
            std::string folder = params[0] >= 3 ? LocalString(ctx, params[3]) : "source2mod";
            // Scripts written for SourceMod pass its folder; theirs is ours.
            if (folder == "sourcemod")
                folder = "source2mod";
            plugin->RequestAutoExecConfig(params[0] < 1 || params[1], params[0] >= 2 ? LocalString(ctx, params[2]) : "", folder);
            return 0;
        }

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

        // ---- ConVars ----------------------------------------------------------------

        // What ConVar_Register gives a C++ plugin's ConVars.
        constexpr uint64 kRegisterFlags = FCVAR_RELEASE | FCVAR_GAMEDLL;

        cell_t Existing(const char* name)
        {
            const uint16 index = convars::convarsManager.GetConvarAccessIndexByName(name);
            return index == SOURCE2MOD_INVALID_CONVAR_INDEX ? 0 : ConVarHandle(index);
        }

        cell_t Created(IPluginContext* ctx, const char* name, uint16 index)
        {
            if (index == SOURCE2MOD_INVALID_CONVAR_INDEX)
                return ctx->ThrowNativeError("Could not create ConVar \"%s\"", name);
            return ConVarHandle(index);
        }

        // CreateInt(name, flags, help, def, hasMin = false, min = 0, hasMax = false, max = 0)
        cell_t N_CreateInt(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            int32 def = params[4], min = params[6], max = params[8];
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_Int32, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           params[5] != 0, params[7] != 0, &def, &min, &max));
        }

        cell_t N_CreateFloat(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            float def = sp_ctof(params[4]), min = sp_ctof(params[6]), max = sp_ctof(params[8]);
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_Float32, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           params[5] != 0, params[7] != 0, &def, &min, &max));
        }

        cell_t N_CreateBool(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            bool def = params[4] != 0;
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_Bool, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           false, false, &def, nullptr, nullptr));
        }

        cell_t N_CreateString(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            const std::string def = LocalString(ctx, params[4]);
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_String, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           false, false, const_cast<char*>(def.c_str()), nullptr, nullptr));
        }

        cell_t N_RefFind(IPluginContext* ctx, const cell_t* params)
        {
            return Existing(LocalString(ctx, params[1]));
        }

        // The access index a ConVar handle holds (+1), as IndexOf above, without a script error.
        bool IndexOf(cell_t handle, uint16* index)
        {
            void* object = Handles().Read(handle, Types().conVar);
            if (!object)
                return false;
            *index = static_cast<uint16>(reinterpret_cast<uintptr_t>(object) - 1);
            return true;
        }

        cell_t N_RefIsValidRef(IPluginContext*, const cell_t* params)
        {
            uint16 index;
            return IndexOf(params[1], &index) && ConVarRefAbstract(index).IsValidRef();
        }

        cell_t N_RefIsConVarDataAvailable(IPluginContext*, const cell_t* params)
        {
            uint16 index;
            if (!IndexOf(params[1], &index))
                return 0;
            ConVarRefAbstract ref(index);
            return ref.IsValidRef() && ref.IsConVarDataAvailable();
        }

        // HookConVarChange(handler(ConVar convar, int slot, newValue, oldValue))
        cell_t N_HookConVarChange(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            ConVarChangeHandler handler([fn](ConVarRefAbstract* ref, CSplitScreenSlot slot, const char* newValue, const char* oldValue)
            {
                if (!ref)
                    return;
                sp::CallArgs args;
                args.PushCell(ConVarHandle(ref->GetAccessIndex()));
                args.PushCell(slot.Get());
                args.PushString(newValue ? newValue : "");
                args.PushString(oldValue ? oldValue : "");
                Invoke(fn, args);
            });
            return static_cast<cell_t>(convars::convarsManager.HookConVarChange(CallerId(ctx), std::move(handler)));
        }

        cell_t N_UnhookConVarChange(IPluginContext*, const cell_t* params)
        {
            return convars::convarsManager.UnhookConVarChangeId(static_cast<Source2ModHookId>(params[1]));
        }

        // ---- the rest of ISource2ModConVars: typed values, flags, create, delete -------

        bool IndexArg(IPluginContext* ctx, cell_t handle, uint16* index)
        {
            if (IndexOf(handle, index))
                return true;
            ctx->ThrowNativeError("Invalid ConVar handle %x", handle);
            return false;
        }

        cell_t N_CVGetType(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            return IndexArg(ctx, params[1], &index) ? CV().GetType(index) : EConVarType_Invalid;
        }

        // GetFlags(ConVar, int flags[2]) / SetFlags(ConVar, const int flags[2]): all 64 bits.
        cell_t N_CVGetFlags(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            const uint64 flags = CV().GetFlags(index);
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            out[0] = static_cast<cell_t>(flags & 0xFFFFFFFFu);
            out[1] = static_cast<cell_t>(flags >> 32);
            return 0;
        }

        cell_t N_CVSetFlags(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[2], &in);
            CV().SetFlags(index, static_cast<uint32_t>(in[0]) | (static_cast<uint64>(static_cast<uint32_t>(in[1])) << 32));
            return 0;
        }

        cell_t N_CVGetDouble(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            return IndexArg(ctx, params[1], &index) ? sp_ftoc(static_cast<float>(CV().GetDouble(index))) : 0;
        }

        cell_t N_CVSetDouble(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (IndexArg(ctx, params[1], &index))
                CV().SetDouble(index, sp_ctof(params[2]));
            return 0;
        }

        void FloatsOut(IPluginContext* ctx, cell_t addr, const float* v, int n)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            for (int i = 0; i < n; i++)
                out[i] = sp_ftoc(v[i]);
        }

        void FloatsIn(IPluginContext* ctx, cell_t addr, float* v, int n)
        {
            cell_t* in;
            ctx->LocalToPhysAddr(addr, &in);
            for (int i = 0; i < n; i++)
                v[i] = sp_ctof(in[i]);
        }

        // GetVector2 / GetVector3 / GetVector4 / GetQAngle (ConVar, float[N])
        template <typename V, int N, V (ISource2ModConVars::*GET)(uint16, CSplitScreenSlot)>
        cell_t N_CVGetFloats(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            const V v = (CV().*GET)(index, CSplitScreenSlot(-1));
            FloatsOut(ctx, params[2], reinterpret_cast<const float*>(&v), N);
            return 0;
        }

        template <typename V, int N, void (ISource2ModConVars::*SET)(uint16, const V&, CSplitScreenSlot)>
        cell_t N_CVSetFloats(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            V v;
            FloatsIn(ctx, params[2], reinterpret_cast<float*>(&v), N);
            (CV().*SET)(index, v, CSplitScreenSlot(-1));
            return 0;
        }

        cell_t N_CVGetColor(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            const Color c = CV().GetColor(index);
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            out[0] = c.r();
            out[1] = c.g();
            out[2] = c.b();
            out[3] = c.a();
            return 0;
        }

        cell_t N_CVSetColor(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[2], &in);
            CV().SetColor(index, Color(in[0], in[1], in[2], in[3]));
            return 0;
        }

        cell_t N_CVDeleteConVar(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (IndexArg(ctx, params[1], &index))
                CV().DeleteConVar(index);
            return 0;
        }

        // A ConVar value of `type` read from text, laid out as CreateConVar wants it.
        struct ConVarValue
        {
            alignas(16) uint8_t bytes[32] = {};
            std::string text;

            bool Parse(EConVarType type, const char* s)
            {
                float f[4] = {};
                switch (type)
                {
                case EConVarType_Bool:
                    *reinterpret_cast<bool*>(bytes) = std::strtol(s, nullptr, 10) != 0 || std::strcmp(s, "true") == 0;
                    return true;
                case EConVarType_Int16:
                    *reinterpret_cast<int16*>(bytes) = static_cast<int16>(std::strtol(s, nullptr, 10));
                    return true;
                case EConVarType_UInt16:
                    *reinterpret_cast<uint16*>(bytes) = static_cast<uint16>(std::strtoul(s, nullptr, 10));
                    return true;
                case EConVarType_Int32:
                    *reinterpret_cast<int32*>(bytes) = static_cast<int32>(std::strtol(s, nullptr, 10));
                    return true;
                case EConVarType_UInt32:
                    *reinterpret_cast<uint32*>(bytes) = static_cast<uint32>(std::strtoul(s, nullptr, 10));
                    return true;
                case EConVarType_Int64:
                    *reinterpret_cast<int64*>(bytes) = std::strtoll(s, nullptr, 10);
                    return true;
                case EConVarType_UInt64:
                    *reinterpret_cast<uint64*>(bytes) = std::strtoull(s, nullptr, 10);
                    return true;
                case EConVarType_Float32:
                    *reinterpret_cast<float*>(bytes) = std::strtof(s, nullptr);
                    return true;
                case EConVarType_Float64:
                    *reinterpret_cast<double*>(bytes) = std::strtod(s, nullptr);
                    return true;
                case EConVarType_String:
                    text = s;
                    return true;
                case EConVarType_Color:
                {
                    int c[4] = { 0, 0, 0, 255 };
                    std::sscanf(s, "%d %d %d %d", &c[0], &c[1], &c[2], &c[3]);
                    *reinterpret_cast<Color*>(bytes) = Color(c[0], c[1], c[2], c[3]);
                    return true;
                }
                case EConVarType_Vector2:
                case EConVarType_Vector3:
                case EConVarType_Vector4:
                case EConVarType_Qangle:
                case EConVarType_VectorWS:
                    std::sscanf(s, "%f %f %f %f", &f[0], &f[1], &f[2], &f[3]);
                    std::memcpy(bytes, f, sizeof(f));
                    return true;
                default:
                    return false;
                }
            }

            void* Pointer()
            {
                return text.empty() ? static_cast<void*>(bytes) : static_cast<void*>(text.data());
            }
        };

        // CConVar.Create(name, type, flags, help, defaultValue, hasMin, min, hasMax, max): any
        // EConVarType, the values as text ("1 2 3" for a vector, "r g b a" for a color).
        cell_t N_CVCreate(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            const auto type = static_cast<EConVarType>(params[2]);
            ConVarValue def, min, max;
            if (!def.Parse(type, LocalString(ctx, params[5])))
                return ctx->ThrowNativeError("Invalid ConVar type %d", params[2]);
            const bool hasMin = params[6] != 0 && type != EConVarType_String;
            const bool hasMax = params[8] != 0 && type != EConVarType_String;
            if (hasMin)
                min.Parse(type, LocalString(ctx, params[7]));
            if (hasMax)
                max.Parse(type, LocalString(ctx, params[9]));
            // An empty string default still needs a valid char*.
            void* defValue = type == EConVarType_String ? static_cast<void*>(def.text.data()) : def.Pointer();
            return Created(ctx, name, CV().CreateConVar(name, type, LocalString(ctx, params[4]), static_cast<uint32_t>(params[3]) | kRegisterFlags,
                                                        hasMin, hasMax, defValue, hasMin ? min.Pointer() : nullptr, hasMax ? max.Pointer() : nullptr));
        }

        const sp_nativeinfo_t g_conVarNatives[] = {
            { "Source2ModConVars.AutoExecConfig", N_AutoExecConfig },
            { "ConVar.BoolValue.get", N_GetBool },
            { "ConVar.BoolValue.set", N_SetBool },
            { "ConVar.IntValue.get", N_GetInt },
            { "ConVar.IntValue.set", N_SetInt },
            { "ConVar.FloatValue.get", N_GetFloat },
            { "ConVar.FloatValue.set", N_SetFloat },
            { "ConVar.Flags.get", N_GetFlags },
            { "ConVar.Flags.set", N_SetFlags },
            { "ConVar.SetBool", N_SetBool },
            { "ConVar.SetInt", N_SetInt },
            { "ConVar.SetFloat", N_SetFloat },
            { "ConVar.GetString", N_GetString },
            { "ConVar.SetString", N_SetString },
            { "ConVar.RestoreDefault", N_RestoreDefault },
            { "ConVar.GetDefault", N_GetDefault },
            { "ConVar.GetBounds", N_GetBounds },
            { "ConVar.GetName", N_GetName },
            { "ConVar.GetDescription", N_GetDescription },
            { "ConVar.AddChangeHook", N_AddChangeHook },
            { "ConVar.RemoveChangeHook", N_RemoveChangeHook },

            { "CConVar.CreateInt", N_CreateInt },
            { "CConVar.CreateFloat", N_CreateFloat },
            { "CConVar.CreateBool", N_CreateBool },
            { "CConVar.CreateString", N_CreateString },
            { "CConVarRef.Find", N_RefFind },
            { "CConVarRef.IsValidRef", N_RefIsValidRef },
            { "CConVarRef.IsConVarDataAvailable", N_RefIsConVarDataAvailable },
            { "Source2ModConVars.HookConVarChange", N_HookConVarChange },
            { "Source2ModConVars.UnhookConVarChange", N_UnhookConVarChange },
            { "CConVar.Create", N_CVCreate },
            { "Source2ModConVars.GetType", N_CVGetType },
            { "Source2ModConVars.GetFlags", N_CVGetFlags },
            { "Source2ModConVars.SetFlags", N_CVSetFlags },
            { "Source2ModConVars.GetDouble", N_CVGetDouble },
            { "Source2ModConVars.SetDouble", N_CVSetDouble },
            { "Source2ModConVars.GetVector2", N_CVGetFloats<Vector2D, 2, &ISource2ModConVars::GetVector2> },
            { "Source2ModConVars.GetVector3", N_CVGetFloats<Vector, 3, &ISource2ModConVars::GetVector3> },
            { "Source2ModConVars.GetVector4", N_CVGetFloats<Vector4D, 4, &ISource2ModConVars::GetVector4> },
            { "Source2ModConVars.GetQAngle", N_CVGetFloats<QAngle, 3, &ISource2ModConVars::GetQAngle> },
            { "Source2ModConVars.GetColor", N_CVGetColor },
            { "Source2ModConVars.SetVector2", N_CVSetFloats<Vector2D, 2, &ISource2ModConVars::SetVector2> },
            { "Source2ModConVars.SetVector3", N_CVSetFloats<Vector, 3, &ISource2ModConVars::SetVector3> },
            { "Source2ModConVars.SetVector4", N_CVSetFloats<Vector4D, 4, &ISource2ModConVars::SetVector4> },
            { "Source2ModConVars.SetQAngle", N_CVSetFloats<QAngle, 3, &ISource2ModConVars::SetQAngle> },
            { "Source2ModConVars.SetColor", N_CVSetColor },
            { "Source2ModConVars.DeleteConVar", N_CVDeleteConVar },
            { nullptr, nullptr },
        };
    }

    cell_t ConVarHandle(uint16_t accessIndex)
    {
        auto it = g_conVarHandles.find(accessIndex);
        if (it != g_conVarHandles.end() && Handles().Read(it->second, Types().conVar))
            return it->second;

        const cell_t handle = Handles().Create(Types().conVar, reinterpret_cast<void*>(static_cast<uintptr_t>(accessIndex) + 1), 0);
        g_conVarHandles[accessIndex] = handle;
        return handle;
    }

    void ForgetConVarHooks(PluginId id)
    {
        // The hooks themselves went with convarsManager.RemoveAllForPlugin().
        std::erase_if(g_changeHooks, [id](const auto& entry) { return std::get<0>(entry.first) == id; });
    }

    const sp_nativeinfo_t* GetConVarNatives()
    {
        return g_conVarNatives;
    }
}
