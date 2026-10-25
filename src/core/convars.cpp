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
#include "convars.h"
#include "commands.h"

#include "hookid.h"
#include "pluginapi.h"
#include "coreconfig.h"
#include "shared.h"
#include "utils/log.h"

#include <algorithm>

// ---- Flag setter compatible with various SDKs ----
template <typename T>
concept HasAddClear = requires(T* t, uint64_t f)
{
    t->AddFlags(f);
    t->ClearFlags(f);
};

template <typename T>
concept HasAddRemove = requires(T* t, uint64_t f)
{
    t->AddFlags(f);
    t->RemoveFlags(f);
};

template <typename T>
concept HasSetFlagBit = requires(T* t, uint64_t f)
{
    t->SetFlag(f, true);
    t->SetFlag(f, false);
};

template <typename T>
void SetAllFlagsCompat(T* data, uint64_t desired)
{
    uint64_t cur = data->GetFlags();
    uint64_t add = desired & ~cur;
    uint64_t rem = cur & ~desired;

    if constexpr (HasAddClear<T>)
    {
        if (add) data->AddFlags(add);
        if (rem) data->ClearFlags(rem);
    }
    else if constexpr (HasAddRemove<T>)
    {
        if (add) data->AddFlags(add);
        if (rem) data->RemoveFlags(rem);
    }
    else if constexpr (HasSetFlagBit<T>)
    {
        // Fallback: set/clear bitwise
        for (int i = 0; i < 64; i++)
        {
            uint64_t bit = (1ULL << i);
            bool want = (desired & bit) != 0;
            data->SetFlag(bit, want);
        }
    }
    else
    {
        static_assert(sizeof(T) == 0, "ConVarData hat keine passende Flags-API (Add/Clear/Remove/SetFlag).");
    }
}

#define CHECK_REF_RET(ref, ret) \
    if (!ref.IsValidRef()) { \
        FP_ERROR("Invalid convar access index."); \
        return ret; \
    }

#define CHECK_DATA_RET(ref, idx, ret) \
    if (!ref.IsConVarDataValid()) { \
        FP_ERROR("Convar data invalid for {}", idx); \
        return ret; \
    }

#define CHECK_REF(ref) \
    if (!ref.IsValidRef()) { \
        FP_ERROR("Invalid convar access index."); \
        return; \
    }

#define CHECK_DATA(ref, idx) \
    if (!ref.IsConVarDataValid()) { \
        FP_ERROR("Convar data invalid for {}", idx); \
        return; \
    }

// min/max are only read when their flag is set: callers pass nullptr for a
// bound they do not want, and CConVar ignores the value in that case anyway.
#define CREATE_CVAR(type) \
{ \
    auto created = new CConVar<type>(name, flags, help, *(const type*)def, \
        hasMin, hasMin ? *(const type*)min : type{}, \
        hasMax, hasMax ? *(const type*)max : type{}); \
    return created->GetAccessIndex(); \
}

namespace convars
{
    ConVarsManager convarsManager;

    uint16 ConVarsManager::GetConvarAccessIndexByName(const char* name)
    {
        ConVarRef ref(name);
        // 0 is a real ConVar, so "not found" has to be the engine's own sentinel.
        return ref.IsValidRef() ? ref.GetAccessIndex() : SOURCE2MOD_INVALID_CONVAR_INDEX;
    }

    ConVarRefAbstract ConVarsManager::GetConvarRef(uint16 accessIndex)
    {
        return ConVarRefAbstract(accessIndex);
    }

    const char* ConVarsManager::GetName(uint16 accessIndex)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, "");

        return ref.GetName();
    }

    const char* ConVarsManager::GetHelpText(uint16 accessIndex)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, "");

        return ref.GetHelpText();
    }

    EConVarType ConVarsManager::GetType(uint16 accessIndex)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, EConVarType_Int32);

        return ref.GetType();
    }

    uint64 ConVarsManager::GetFlags(uint16 accessIndex)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, 0);

        return ref.GetFlags();
    }

    void ConVarsManager::SetFlags(uint16 accessIndex, uint64 flags)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        SetAllFlagsCompat(ref.GetConVarData(), flags);
    }

    void* ConVarsManager::GetValueAddress(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, nullptr);
        CHECK_DATA_RET(ref, accessIndex, nullptr);

        return ref.GetConVarData()->ValueOrDefault(slot);
    }

    const char* ConVarsManager::GetString(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, "");
        CHECK_DATA_RET(ref, accessIndex, "");

        thread_local static std::string buffer;

        CBufferString buf;
        ref.GetValueAsString(buf, slot);
        buffer = buf.Get();

        return buffer.c_str();
    }

    void ConVarsManager::SetString(uint16 accessIndex, const char* value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetString(value, slot);
    }

    bool ConVarsManager::GetBool(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, false);
        CHECK_DATA_RET(ref, accessIndex, false);

        return ref.GetAs<bool>(slot);
    }

    int32 ConVarsManager::GetInt(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, 0);
        CHECK_DATA_RET(ref, accessIndex, 0);

        return ref.GetAs<int32>(slot);
    }

    float ConVarsManager::GetFloat(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, 0);
        CHECK_DATA_RET(ref, accessIndex, 0);

        return ref.GetAs<float>(slot);
    }

    double ConVarsManager::GetDouble(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, 0);
        CHECK_DATA_RET(ref, accessIndex, 0);

        return ref.GetAs<double>(slot);
    }

    void ConVarsManager::SetBool(uint16 accessIndex, bool value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<bool>(value, slot);
    }

    void ConVarsManager::SetInt(uint16 accessIndex, int32 value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<int32>(value, slot);
    }

    void ConVarsManager::SetFloat(uint16 accessIndex, float value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<float>(value, slot);
    }

    void ConVarsManager::SetDouble(uint16 accessIndex, double value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<double>(value, slot);
    }

    Vector2D ConVarsManager::GetVector2(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, Vector2D{});
        CHECK_DATA_RET(ref, accessIndex, Vector2D{});

        return ref.GetAs<Vector2D>(slot);
    }

    Vector ConVarsManager::GetVector3(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, Vector{});
        CHECK_DATA_RET(ref, accessIndex, Vector{});

        return ref.GetAs<Vector>(slot);
    }

    Vector4D ConVarsManager::GetVector4(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, Vector4D{});
        CHECK_DATA_RET(ref, accessIndex, Vector4D{});

        return ref.GetAs<Vector4D>(slot);
    }

    QAngle ConVarsManager::GetQAngle(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, QAngle{});
        CHECK_DATA_RET(ref, accessIndex, QAngle{});

        return ref.GetAs<QAngle>(slot);
    }

    Color ConVarsManager::GetColor(uint16 accessIndex, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF_RET(ref, Color{});
        CHECK_DATA_RET(ref, accessIndex, Color{});

        return ref.GetAs<Color>(slot);
    }

    void ConVarsManager::SetVector2(uint16 accessIndex, const Vector2D& value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<Vector2D>(value, slot);
    }

    void ConVarsManager::SetVector3(uint16 accessIndex, const Vector& value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<Vector>(value, slot);
    }

    void ConVarsManager::SetVector4(uint16 accessIndex, const Vector4D& value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<Vector4D>(value, slot);
    }

    void ConVarsManager::SetQAngle(uint16 accessIndex, const QAngle& value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<QAngle>(value, slot);
    }

    void ConVarsManager::SetColor(uint16 accessIndex, const Color& value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        ref.SetAs<Color>(value, slot);
    }

    void ConVarsManager::GetValue(uint16 accessIndex, void* outValue, CSplitScreenSlot slot)
    {
        if (!outValue)
        {
            FP_ERROR("GetValue: outValue is null");
            return;
        }

        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        auto addr = ref.GetConVarData()->ValueOrDefault(slot);
        if (!addr)
        {
            FP_ERROR("GetValue: value address null for {}", accessIndex);
            return;
        }

        std::memcpy(outValue, addr, sizeof(CVValue_t));
    }

    void ConVarsManager::SetValue(uint16 accessIndex, const void* value, CSplitScreenSlot slot)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);
        CHECK_DATA(ref, accessIndex);

        switch (ref.GetType())
        {
        case EConVarType_Int16:   ref.SetAs<int16>(*(int16*)value, slot); break;
        case EConVarType_UInt16:  ref.SetAs<uint16>(*(uint16*)value, slot); break;
        case EConVarType_UInt32:  ref.SetAs<uint32>(*(uint32*)value, slot); break;
        case EConVarType_Int32:   ref.SetAs<int32>(*(int32*)value, slot); break;
        case EConVarType_UInt64:  ref.SetAs<uint64>(*(uint64*)value, slot); break;
        case EConVarType_Int64:   ref.SetAs<int64>(*(int64*)value, slot); break;
        case EConVarType_Bool:    ref.SetAs<bool>(*(bool*)value, slot); break;
        case EConVarType_Float32: ref.SetAs<float>(*(float*)value, slot); break;
        case EConVarType_Float64: ref.SetAs<double>(*(double*)value, slot); break;

        case EConVarType_String:
            ref.SetString((const char*)value, slot);
            break;

        case EConVarType_Vector2:
            ref.SetAs<Vector2D>(*(Vector2D*)value, slot);
            break;

        case EConVarType_Vector3:
            ref.SetAs<Vector>(*(Vector*)value, slot);
            break;

        case EConVarType_Vector4:
            ref.SetAs<Vector4D>(*(Vector4D*)value, slot);
            break;

        case EConVarType_Qangle:
            ref.SetAs<QAngle>(*(QAngle*)value, slot);
            break;

        default:
            FP_ERROR("Unsupported convar type: {}", (int)ref.GetType());
            break;
        }
    }

    uint16 ConVarsManager::CreateConVar(const char* name, EConVarType type, const char* help,
                                        uint64 flags, bool hasMin, bool hasMax,
                                        void* def, const void* min, const void* max)
    {
        ConVarRef ref(name);
        if (ref.IsValidRef())
        {
            FP_ERROR("ConVar '{}' already exists", name);
            return ref.GetAccessIndex();
        }

        if (!name || (!def && type != EConVarType_String) || (hasMin && !min) || (hasMax && !max))
        {
            FP_ERROR("CreateConVar '{}': missing name, default or an enabled min/max value", name ? name : "");
            return SOURCE2MOD_INVALID_CONVAR_INDEX;
        }

        switch (type)
        {
        case EConVarType_Int16: CREATE_CVAR(int16);
        case EConVarType_UInt16: CREATE_CVAR(uint16);
        case EConVarType_UInt32: CREATE_CVAR(uint32);
        case EConVarType_Int32: CREATE_CVAR(int32);
        case EConVarType_UInt64: CREATE_CVAR(uint64);
        case EConVarType_Int64: CREATE_CVAR(int64);
        case EConVarType_Bool: CREATE_CVAR(bool);
        case EConVarType_Float32: CREATE_CVAR(float);
        case EConVarType_Float64: CREATE_CVAR(double);

        case EConVarType_String:
            {
                auto created = new CConVar<CUtlString>(
                    name, flags, help,
                    (const char*)def,
                    hasMin, (const char*)min,
                    hasMax, (const char*)max
                );
                return created->GetAccessIndex();
            }

        case EConVarType_Vector2: CREATE_CVAR(Vector2D);
        case EConVarType_Vector3: CREATE_CVAR(Vector);
        case EConVarType_Vector4: CREATE_CVAR(Vector4D);
        case EConVarType_Qangle: CREATE_CVAR(QAngle);
        case EConVarType_Color: CREATE_CVAR(Color);

        default:
            FP_ERROR("Unsupported convar type: {}", (int)type);
            return SOURCE2MOD_INVALID_CONVAR_INDEX;
        }
    }

    void ConVarsManager::DeleteConVar(uint16 accessIndex)
    {
        auto ref = GetRef(accessIndex);
        CHECK_REF(ref);

        auto data = ref.GetConVarData();
        if (!data)
        {
            FP_ERROR("Convar data null for {}", accessIndex);
            return;
        }

        data->Invalidate();
    }

    void ConVarsManager::UnlockConVars()
    {
        if (coreconfig::coreConfig.UnlockConVars)
        {
            for (ConVarRefAbstract ref(ConVarRef((uint16)0)); ref.IsValidRef(); ref = ConVarRefAbstract(ConVarRef(ref.GetAccessIndex() + 1)))
            {
                if (!ref.IsFlagSet(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY)) continue;
                ref.RemoveFlags(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
            }
        }
    }

    /* =========================
    Change callbacks
    ========================= */

    // ICvar only takes a plain function pointer, and keeps one list of them for
    // the whole process. So the manager installs exactly one -- this -- and fans
    // out from there, which is what lets a plugin register a capturing lambda.
    // The last parameter is void* in older hl2sdk and ConVarUserInfoSet_t* since
    // 5fc47fd; a template lets FnChangeCallbackGlobal_t pick whichever it is.
    template <typename TUserInfo>
    static void OnGlobalConVarChanged(ConVarRefAbstract* ref, CSplitScreenSlot nSlot,
                                      const char* pNewValue, const char* pOldValue, TUserInfo /*userinfo*/)
    {
        convarsManager.DispatchConVarChange(ref, nSlot, pNewValue, pOldValue);
    }

    void ConVarsManager::EnsureGlobalCallbackInstalled()
    {
        if (m_bGlobalCallbackInstalled || !g_pCVar)
            return;

        g_pCVar->InstallGlobalChangeCallback(OnGlobalConVarChanged);
        m_bGlobalCallbackInstalled = true;
    }

    void ConVarsManager::RemoveGlobalCallbackIfIdle()
    {
        // Nothing left to fan out to -- stop paying for the callback.
        if (!m_bGlobalCallbackInstalled || !m_changeHandlers.empty() || !g_pCVar)
            return;

        g_pCVar->RemoveGlobalChangeCallback(OnGlobalConVarChanged);
        m_bGlobalCallbackInstalled = false;
    }

    Source2ModConVarHookId ConVarsManager::HookConVarChange(PluginId owner, ConVarChangeHandler handler)
    {
        if (!handler)
            return 0;

        const Source2ModHookId id = hookid::Next();
        m_changeHandlers.push_back({ owner, std::move(handler), id });
        EnsureGlobalCallbackInstalled();
        return id;
    }

    bool ConVarsManager::UnhookConVarChange(PluginId owner, const ConVarChangeHandler& handler)
    {
        if (!handler.HasIdentity())
            return false;

        const bool found = std::erase_if(m_changeHandlers, [owner, &handler](const ConVarChangeEntry& e)
        {
            return e.owner == owner && handler.SameAs(e.handler);
        }) > 0;

        RemoveGlobalCallbackIfIdle();
        return found;
    }

    bool ConVarsManager::UnhookConVarChangeId(Source2ModHookId id)
    {
        const bool found = std::erase_if(m_changeHandlers, [id](const ConVarChangeEntry& e) { return e.id == id; }) > 0;
        RemoveGlobalCallbackIfIdle();
        return found;
    }

    void ConVarsManager::DispatchConVarChange(ConVarRefAbstract* ref, CSplitScreenSlot slot,
                                              const char* pszNewValue, const char* pszOldValue)
    {
        if (m_changeHandlers.empty())
            return;

        // Copied first: a handler is free to register or drop one, either of which
        // would move the vector out from under an iterator.
        const std::vector<ConVarChangeEntry> handlers = m_changeHandlers;

        for (const auto& entry : handlers)
            entry.handler(ref, slot, pszNewValue, pszOldValue);
    }

    void ConVarsManager::TrackConVar(PluginId owner, ConVarRefAbstract* ref)
    {
        if (ref && ref->IsValidRef())
            m_pluginConVars.push_back({ owner, *ref, ref });
    }

    void ConVarsManager::TrackConCommand(PluginId owner, ConCommandRef* ref)
    {
        if (ref && ref->IsValidRef())
            m_pluginConCommands.push_back({ owner, *ref, ref });
    }

    void ConVarsManager::RemoveAllForPlugin(PluginId id)
    {
        std::erase_if(m_changeHandlers, [id](const ConVarChangeEntry& e)
        {
            return e.owner == id;
        });

        RemoveGlobalCallbackIfIdle();

        // The engine has no real unregister, so a plugin's ConVar / ConCommand
        // is taken out by hand: callbacks off (and a ConVar's data invalidated,
        // as Metamod's META_CONVAR_REGISTER does), then out of CCvar's name table
        // and its list, so the plugin loading again registers it anew and
        // find/cvarlist do not meet the old entry. Metamod's way alone (hide,
        // keep the entry) leaves a reloaded command dead: the hidden one keeps
        // the name. Where CCvar's table does not look as expected the entry is
        // only hidden. The slot is free afterwards, so the plugin's own ref is
        // invalidated too -- its destructor at dlclose would unregister it again.
        std::erase_if(m_pluginConVars, [id](PluginConVar& entry)
        {
            if (entry.owner != id)
                return false;
            if (g_pCVar && entry.copy.IsValidRef())
            {
                const std::string name = entry.copy.GetName();
                g_pCVar->UnregisterConVarCallbacks(entry.copy);
                if (ConVarData* data = entry.copy.GetConVarData())
                    data->Invalidate();
                // The plugin's ref only while it still names this slot: a ConVar it
                // destroyed itself already invalidated its own.
                const uint16 index = entry.copy.GetAccessIndex();
                if (ReleaseConVarName(name.c_str(), index) && entry.plugin && entry.plugin->GetAccessIndex() == index)
                    entry.plugin->InvalidateRef();
            }
            return true;
        });

        std::erase_if(m_pluginConCommands, [id](PluginConCommand& entry)
        {
            if (entry.owner != id)
                return false;
            if (g_pCVar && entry.copy.IsValidRef())
            {
                const std::string name = entry.copy.GetName();
                g_pCVar->UnregisterConCommandCallbacks(entry.copy);
                const uint16 index = entry.copy.GetAccessIndex();
                if (commands::ReleaseConCommandName(name.c_str(), index))
                {
                    if (entry.plugin && entry.plugin->GetAccessIndex() == index)
                        entry.plugin->InvalidateRef();
                }
                else
                    entry.copy.AddFlags(FCVAR_DEFENSIVE | FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
            }
            return true;
        });
    }

    bool ConVarsManager::ReleaseConVarName(const char* name, uint16 accessIndex)
    {
        // As commands::ReleaseConCommandName: CCvar's layout is reverse
        // engineered, so the name is only dropped from m_ConVarHashes when the
        // table and FindConVar agree on the access index.
        if (!g_pCVar || !name || !*name)
            return false;

        auto& hashes = static_cast<CCvar*>(g_pCVar)->m_ConVarHashes;
        const CUtlStringToken token(name);
        const auto handle = hashes.Find(token, token.GetHashCode());

        if (handle == hashes.InvalidHandle() || hashes.Element(handle) != accessIndex)
        {
            FP_WARN("Could not release ConVar '{}': CCvar's table does not look the way this build expects", name);
            return false;
        }

        // The name and the list entry both, so find/cvarlist do not meet the old
        // one next to the next registration of the name.
        if (!hashes.Remove(token, token.GetHashCode()))
            return false;
        auto* pCvar = static_cast<CCvar*>(g_pCVar);
        pCvar->m_ConVarList.Remove(accessIndex);
        --pCvar->m_ConVarCount;
        return true;
    }

    void ConVarsManager::Shutdown()
    {
        m_changeHandlers.clear();
        RemoveGlobalCallbackIfIdle();
    }
}
