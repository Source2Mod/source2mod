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
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once
#include "core/appsystem/appsystem.h"

#include <source2mod/ISource2ModConfigs.h>

#include "core/data/json.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Timer;

namespace configs
{
    /// One config file: the defaults it was declared with, the merged document
    /// plugins read, and what the watcher needs to notice a change.
    class ConfigFile final : public ISource2ModConfig
    {
    public:
        const char* GetName() const override { return m_sName.c_str(); }
        const char* GetPath() const override { return m_sPath.c_str(); }
        const ISource2ModJSONValue* Root() const override { return m_pDoc->Root(); }
        const ISource2ModJSONValue* Defaults() const override { return m_pDefaultsDoc->Root(); }
        const ISource2ModJSONValue* Find(const char* pszPointer) const override;
        uint32_t GetGeneration() const override { return m_nGeneration; }
        const char* GetLastError() const override { return m_sLastError.c_str(); }

        bool SetString(const char* pszPointer, const char* pszValue) override;
        bool SetInt64(const char* pszPointer, int64_t nValue) override;
        bool SetDouble(const char* pszPointer, double flValue) override;
        bool SetBool(const char* pszPointer, bool bValue) override;
        bool SetJSON(const char* pszPointer, const char* pszJson) override;
        bool Remove(const char* pszPointer) override;

        bool Save(char* error, size_t maxlen) override;
        bool Reload(char* error, size_t maxlen) override;
        /// Reload() for "s2m config reload": the listener hears ConfigChange::Command.
        bool ReloadFromCommand(char* error, size_t maxlen);

        /// The merged document itself, for the core's own readers.
        const nlohmann::ordered_json& Data() const { return m_Data; }
        PluginId Owner() const { return m_nOwner; }
        uint32_t Flags() const { return m_nFlags; }

    private:
        friend class ConfigsManager;

        /// Reads the file (example and first-run copy included) into a new
        /// generation. False with `error` when it did not go live; the first
        /// read falls back to the defaults unless the config is REQUIRED.
        bool Read(ConfigChange reason, std::string& error);
        /// Swaps a new document in; the old one is freed next frame.
        void Publish(nlohmann::ordered_json data);
        bool Set(const char* pszPointer, nlohmann::ordered_json value);
        bool Write(const std::string& text, std::string& error);
        void RememberFile();
        void Notify(ConfigChange reason);

        PluginId m_nOwner = 0;
        std::string m_sName;
        std::string m_sPath;
        std::string m_sExamplePath;
        std::string m_sDefaultsText;
        uint32_t m_nFlags = 0;
        ISource2ModConfigListener* m_pListener = nullptr;
        /// Declared through AddListener (RemoveListener drops the listener).
        bool m_bDeclared = false;
        bool m_bLoaded = false;

        nlohmann::ordered_json m_Defaults;
        nlohmann::ordered_json m_Data;
        std::unique_ptr<json::CJSONDocument> m_pDefaultsDoc;
        std::unique_ptr<json::CJSONDocument> m_pDoc;
        uint32_t m_nGeneration = 0;
        std::string m_sLastError;

        // The watcher: the file as last read or written, and a change seen
        // once that waits for the next poll to be sure the write is over.
        std::filesystem::file_time_type m_LastWrite{};
        size_t m_nLastHash = 0;
        bool m_bPending = false;
        std::filesystem::file_time_type m_PendingWrite{};
    };

    class ConfigsManager final : public ISource2ModConfigs, public appsystem::ISource2ModAppSystem
    {
    public:
        appsystem::SystemId GetSystemId() const override { return appsystem::SystemId::Configs; }

        bool AddListener(ISource2ModConfigListener* listener, char* error, size_t maxlen) override { return AddListener(PluginId(0), listener, error, maxlen); }
        bool AddListener(PluginId owner, ISource2ModConfigListener* listener, char* error, size_t maxlen);
        void RemoveListener(ISource2ModConfigListener* listener) override { RemoveListener(PluginId(0), listener); }
        void RemoveListener(PluginId owner, ISource2ModConfigListener* listener);
        ISource2ModConfig* Register(const char* pszName, const char* pszDefaults, uint32_t nFlags,
            ISource2ModConfigListener* listener, char* error, size_t maxlen) override;
        /// Register() for the core's own callers, which know whose it is: a
        /// SourcePawn plugin's natives run in the core's code.
        ISource2ModConfig* RegisterFor(PluginId owner, const char* pszName, const char* pszDefaults, uint32_t nFlags,
            ISource2ModConfigListener* listener, char* error, size_t maxlen);
        void Unregister(ISource2ModConfig* config) override;
        ISource2ModConfig* Get(const char* pszName) override { return Get(PluginId(0), pszName); }
        ISource2ModConfig* Get(PluginId owner, const char* pszName);

        /// One of the core's own files, directly in configs/ (owner 0).
        ConfigFile* RegisterCore(const char* pszName, const char* pszDefaults, uint32_t nFlags,
                                 ISource2ModConfigListener* listener, std::string& error);

        /// Every config, for "s2m config".
        const std::vector<std::unique_ptr<ConfigFile>>& All() const { return m_configs; }

        /// Polls the watched files once a second (core.json ConfigHotReloadEnabled).
        void StartWatching();
        void Shutdown() override;
        void RemoveAllForPlugin(PluginId id) override;

        /// Keeps a replaced document readable until the next frame.
        void Retire(std::unique_ptr<json::CJSONDocument> doc);

    private:
        ConfigFile* Create(PluginId owner, const std::string& directory, const char* pszName, const char* pszDefaults,
                           uint32_t nFlags, ISource2ModConfigListener* listener, std::string& error);
        void Poll();

        std::vector<std::unique_ptr<ConfigFile>> m_configs;
        std::unordered_map<PluginId, ISource2ModConfigListener*> m_listeners;
        std::vector<std::unique_ptr<json::CJSONDocument>> m_retired;
        bool m_bRetireQueued = false;
        Timer* m_pPollTimer = nullptr;
    };

    extern ConfigsManager configsManager;
}
