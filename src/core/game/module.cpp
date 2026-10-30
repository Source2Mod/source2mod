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
#include "core/game/module.h"
#include "utils/log.h"

#include "core/plugin.h"
// plugin.h brings in windows.h, whose GetModuleHandle macro would rename
// Source2ModModule::GetModuleHandle below.
#undef GetModuleHandle

#include <algorithm>
#include <cctype>

/// Every spelling of the server module: "server", "server.dll", "server.so",
/// "libserver", "libserver.so", any case, with or without a directory.
static bool IsServerModuleName(std::string_view name)
{
    if (auto slash = name.find_last_of("/\\"); slash != std::string_view::npos)
        name.remove_prefix(slash + 1);

    std::string bare(name);
    std::transform(bare.begin(), bare.end(), bare.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (bare.ends_with(".dll"))
        bare.resize(bare.size() - 4);
    else if (bare.ends_with(".so"))
        bare.resize(bare.size() - 3);
    if (bare.starts_with("lib"))
        bare.erase(0, 3);

    return bare == "server";
}

bool InitModuleFromName(DynLibUtils::CModule& module, std::string_view name, bool extension)
{
    if (IsServerModuleName(name))
    {
        // Unsynthesized: the game's own factory, not metamod's.
        auto factory = g_SMAPI->GetServerFactory(false);
        return factory && module.InitFromMemory(DynLibUtils::CMemory(reinterpret_cast<void*>(factory)));
    }

    return module.InitFromName(name, extension);
}

Source2ModModule::Source2ModModule(const char* name)
{
    InitFromName(name);
}

Source2ModModule::Source2ModModule(uintptr_t ptr)
{
    InitFromMemory(ptr);
}

bool Source2ModModule::InitFromName(const char* name, bool extension)
{
    m_cachedName.clear();
    return name && InitModuleFromName(m_module, name, extension);
}

bool Source2ModModule::InitFromMemory(uintptr_t ptr)
{
    m_cachedName.clear();
    return m_module.InitFromMemory(DynLibUtils::CMemory(ptr));
}

// A module that did not initialise (no such library loaded) answers nothing
// rather than reaching into DynLibUtils: on Linux GetBase() reads through the
// handle (a link_map*), which is null then.

ISource2ModMemory Source2ModModule::FindPattern(const char* pattern, ISource2ModMemory startAddress) const
{
    if (!m_module.IsValid() || !pattern)
        return ISource2ModMemory();
    return ISource2ModMemory(m_module.FindPattern(DynLibUtils::ParsePattern(pattern), DynLibUtils::CMemory(startAddress.GetAddr())).GetPtr());
}

ISource2ModMemory Source2ModModule::FindPatternInSection(const char* pattern, const char* section, ISource2ModMemory startAddress) const
{
    if (!m_module.IsValid() || !pattern || !section)
        return ISource2ModMemory();
    return ISource2ModMemory(m_module.FindPattern(DynLibUtils::ParsePattern(pattern), DynLibUtils::CMemory(startAddress.GetAddr()), m_module.GetSectionByName(section)).GetPtr());
}

ISource2ModMemory Source2ModModule::GetVirtualTableByName(const char* name, bool decorated) const
{
    if (!m_module.IsValid() || !name)
        return ISource2ModMemory();
    return ISource2ModMemory(m_module.GetVirtualTableByName(name, decorated).GetPtr());
}

ISource2ModMemory Source2ModModule::GetVirtualTableByOffset(const char* name, std::ptrdiff_t offset, bool decorated) const
{
    if (!m_module.IsValid() || !name)
        return ISource2ModMemory();

    return ISource2ModMemory(m_module.GetVirtualTableByOffset(name, offset, decorated).GetPtr());
}

ISource2ModMemory Source2ModModule::GetVirtualTableByBase(const char* name, const char* baseName, bool decorated) const
{
    if (!m_module.IsValid() || !name || !baseName)
        return ISource2ModMemory();

    return ISource2ModMemory(m_module.GetVirtualTableByBase(name, baseName, decorated).GetPtr());
}

ISource2ModMemory Source2ModModule::GetFunctionByName(const char* name) const
{
    if (!m_module.IsValid() || !name)
        return ISource2ModMemory();
    return ISource2ModMemory(m_module.GetFunctionByName(name).GetPtr());
}

ISource2ModModule::SectionInfo Source2ModModule::GetSectionByName(const char* name) const
{
    if (!m_module.IsValid() || !name)
        return { 0, 0 };
    const auto* sec = m_module.GetSectionByName(name);
    if (!sec) return { 0, 0 };
    return { reinterpret_cast<uintptr_t>(sec->GetPtr()), sec->m_nSectionSize };
}

void* Source2ModModule::GetModuleHandle() const
{
    return m_module.GetHandle();
}

ISource2ModMemory Source2ModModule::GetModuleBase() const
{
    if (!m_module.IsValid())
        return ISource2ModMemory();
    return ISource2ModMemory(m_module.GetBase().GetPtr());
}

const char* Source2ModModule::GetModulePath() const
{
    if (!m_module.IsValid())
        return "";
    return m_module.GetPath().data();
}

const char* Source2ModModule::GetModuleName() const
{
    if (!m_module.IsValid())
        return "";
    if (m_cachedName.empty())
        m_cachedName = m_module.GetName();
    return m_cachedName.c_str();
}

namespace modules
{
    ModulesManager modulesManager;

    namespace
    {
        const char* NameOf(Source2ModModuleId id)
        {
            switch (id)
            {
            case Source2ModModuleId::Server: return "server";
            case Source2ModModuleId::Engine: return "engine2";
            case Source2ModModuleId::Tier0: return "tier0";
            case Source2ModModuleId::VScript: return "vscript";
            case Source2ModModuleId::SchemaSystem: return "schemasystem";
            case Source2ModModuleId::NetworkSystem: return "networksystem";
            case Source2ModModuleId::ResourceSystem: return "resourcesystem";
            case Source2ModModuleId::FileSystem: return "filesystem_stdio";
#if defined(_WIN32)
            case Source2ModModuleId::SteamApi: return "steam_api64";
#else
            case Source2ModModuleId::SteamApi: return "libsteam_api";
#endif
            default: return nullptr;
            }
        }
    }

    void ModulesManager::Shutdown()
    {
        std::lock_guard lock(m_lock);
        m_byName.clear();
        m_modules.clear();
    }

    Source2ModModule* ModulesManager::Keep(std::unique_ptr<Source2ModModule> module)
    {
        if (!module->GetModuleHandle())
            return nullptr;

        for (const auto& kept : m_modules)
        {
            if (kept->GetModuleHandle() == module->GetModuleHandle())
                return kept.get();
        }
        m_modules.push_back(std::move(module));
        return m_modules.back().get();
    }

    Source2ModModule* ModulesManager::Get(Source2ModModuleId id)
    {
        const char* name = NameOf(id);
        return name ? Get(name) : nullptr;
    }

    Source2ModModule* ModulesManager::Get(const char* name)
    {
        if (!name || !*name)
            return nullptr;

        // engine2 is the module's real name; "engine" is the spelling gamedata
        // files have always used for it.
        const std::string key = !strcmp(name, "engine") ? "engine2" : name;

        std::lock_guard lock(m_lock);
        if (const auto it = m_byName.find(key); it != m_byName.end())
            return it->second;

        // Not remembered when it is not there: it can load later.
        Source2ModModule* module = Keep(std::make_unique<Source2ModModule>(key.c_str()));
        if (module)
            m_byName.emplace(key, module);
        return module;
    }

    Source2ModModule* ModulesManager::GetFromAddress(const void* address)
    {
        if (!address)
            return nullptr;

        std::lock_guard lock(m_lock);
        return Keep(std::make_unique<Source2ModModule>(reinterpret_cast<uintptr_t>(address)));
    }

    void** ModulesManager::FindVirtualTable(const char* className, const char* baseName, ISource2ModModule** found)
    {
        void** first = nullptr;
        const char* firstName = nullptr;
        for (int i = 0; i < static_cast<int>(Source2ModModuleId::Count); i++)
        {
            Source2ModModule* module = Get(static_cast<Source2ModModuleId>(i));
            if (!module)
                continue;

            const auto table = (baseName && *baseName ? module->GetVirtualTableByBase(className, baseName)
                                                      : module->GetVirtualTableByName(className)).GetAddr();
            if (!table)
                continue;

            if (!first)
            {
                first = reinterpret_cast<void**>(table);
                firstName = module->GetModuleName();
                if (found)
                    *found = module;
            }
            else
            {
                FP_WARN("Class {} is in {} and {}: using {}, name the module to pick", className, firstName, module->GetModuleName(), firstName);
            }
        }
        return first;
    }
}
