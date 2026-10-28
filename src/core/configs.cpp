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
#include "core/configs.h"

#include "core/coreconfig.h"
#include "core/paths.h"
#include "core/pluginmanager.h"
#include "core/scheduler.h"
#include "core/slowguard.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <tier0_strtools.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>

namespace configs
{
    ConfigsManager configsManager;

    using ojson = nlohmann::ordered_json;
    namespace fs = std::filesystem;

    /* =========================
    Helpers
    ========================= */

    static bool ValidName(const char* pszName)
    {
        if (!pszName || !*pszName || *pszName == '/')
            return false;

        const std::string name(pszName);
        if (name.back() == '/' || name.find("..") != std::string::npos || name.find("//") != std::string::npos)
            return false;

        return std::all_of(name.begin(), name.end(), [](unsigned char c)
        {
            return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/';
        });
    }

    static bool ReadText(const std::string& path, std::string& text)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs)
            return false;

        std::ostringstream ss;
        ss << ifs.rdbuf();
        text = ss.str();
        return true;
    }

    /// Parses a file people edit by hand: comments allowed, a BOM skipped.
    static bool ParseText(const std::string& text, ojson& out, std::string& error)
    {
        try
        {
            out = ojson::parse(text, nullptr, true, true);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }

        if (!out.is_object())
        {
            error = "the root is not a JSON object";
            return false;
        }

        return true;
    }

    static std::unique_ptr<json::CJSONDocument> MakeDoc(const ojson& data)
    {
        const std::string text = data.dump();
        return std::make_unique<json::CJSONDocument>(text.c_str(), static_cast<int>(text.size()));
    }

    static const char* KindOf(const ojson& v)
    {
        if (v.is_object())  return "an object";
        if (v.is_array())   return "an array";
        if (v.is_string())  return "a string";
        if (v.is_boolean()) return "a bool";
        if (v.is_number())  return "a number";
        return "null";
    }

    static std::string EscapeToken(const std::string& key)
    {
        std::string out;
        for (char c : key)
        {
            if (c == '~')      out += "~0";
            else if (c == '/') out += "~1";
            else               out += c;
        }
        return out;
    }

    /// The admin's file over the defaults: missing keys come from the
    /// defaults, a wrong-typed value is replaced by the default. Objects with
    /// members are merged key by key; an empty object, an array or a null
    /// default takes whatever the admin wrote.
    static void Merge(ojson& user, const ojson& defaults, const std::string& pointer,
                      std::vector<std::string>& added, std::vector<std::string>& wrongType)
    {
        for (const auto& [key, def] : defaults.items())
        {
            const std::string at = pointer + "/" + EscapeToken(key);
            auto it = user.find(key);

            if (it == user.end())
            {
                user[key] = def;
                added.push_back(at);
                continue;
            }

            if (def.is_null())
                continue;

            if (std::string(KindOf(*it)) != KindOf(def))
            {
                wrongType.push_back(at + " is " + KindOf(*it) + ", expected " + KindOf(def) + "; using the default");
                *it = def;
                continue;
            }

            if (def.is_object() && !def.empty())
                Merge(*it, def, at, added, wrongType);
        }
    }

    static std::string Join(const std::vector<std::string>& items, const char* separator)
    {
        std::string out;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (i)
                out += separator;
            out += items[i];
        }
        return out;
    }

    static PluginId IdOf(ISource2ModPlugin* plugin)
    {
        for (auto& p : pluginManager.m_plugins)
        {
            if (p && p->api == plugin)
                return p->id;
        }
        return -1;
    }

    /* =========================
    ConfigFile
    ========================= */

    const ISource2ModJSONValue* ConfigFile::Find(const char* pszPointer) const
    {
        if (!pszPointer)
            return nullptr;

        const ISource2ModJSONValue* node = Root();
        if (!*pszPointer)
            return node;
        if (*pszPointer != '/')
            return nullptr;

        std::string pointer(pszPointer + 1);
        size_t start = 0;
        while (node)
        {
            const size_t end = pointer.find('/', start);
            std::string token = pointer.substr(start, end == std::string::npos ? std::string::npos : end - start);

            // ~1 before ~0, as RFC 6901 says.
            for (size_t pos; (pos = token.find("~1")) != std::string::npos;)
                token.replace(pos, 2, "/");
            for (size_t pos; (pos = token.find("~0")) != std::string::npos;)
                token.replace(pos, 2, "~");

            if (node->IsArray())
            {
                char* tail = nullptr;
                const long index = strtol(token.c_str(), &tail, 10);
                node = (token.empty() || *tail) ? nullptr : node->At(static_cast<int>(index));
            }
            else
            {
                node = node->Get(token.c_str());
            }

            if (end == std::string::npos)
                break;
            start = end + 1;
        }

        return node;
    }

    void ConfigFile::Publish(ojson data)
    {
        m_Data = std::move(data);
        if (m_pDoc)
            configsManager.Retire(std::move(m_pDoc));
        m_pDoc = MakeDoc(m_Data);
        m_nGeneration++;
    }

    bool ConfigFile::Set(const char* pszPointer, ojson value)
    {
        if (!pszPointer)
            return false;

        try
        {
            ojson data = m_Data;
            data[ojson::json_pointer(pszPointer)] = std::move(value);
            Publish(std::move(data));
            return true;
        }
        catch (const std::exception& ex)
        {
            FP_WARN("{}: cannot set {}: {}", m_sPath, pszPointer, ex.what());
            return false;
        }
    }

    bool ConfigFile::SetString(const char* pszPointer, const char* pszValue) { return Set(pszPointer, pszValue ? pszValue : ""); }
    bool ConfigFile::SetInt64(const char* pszPointer, int64_t nValue) { return Set(pszPointer, nValue); }
    bool ConfigFile::SetDouble(const char* pszPointer, double flValue) { return Set(pszPointer, flValue); }
    bool ConfigFile::SetBool(const char* pszPointer, bool bValue) { return Set(pszPointer, bValue); }

    bool ConfigFile::SetJSON(const char* pszPointer, const char* pszJson)
    {
        ojson value = ojson::parse(pszJson ? pszJson : "", nullptr, false, true);
        if (value.is_discarded())
        {
            FP_WARN("{}: cannot set {}: the value is not valid JSON", m_sPath, pszPointer ? pszPointer : "");
            return false;
        }
        return Set(pszPointer, std::move(value));
    }

    bool ConfigFile::Remove(const char* pszPointer)
    {
        if (!pszPointer || !*pszPointer)
            return false;

        try
        {
            const ojson patch = ojson::array({ { { "op", "remove" }, { "path", pszPointer } } });
            Publish(m_Data.patch(patch));
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool ConfigFile::Write(const std::string& text, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(fs::path(m_sPath).parent_path(), ec);

        if (fs::exists(m_sPath, ec))
            fs::copy_file(m_sPath, m_sPath + ".bak", fs::copy_options::overwrite_existing, ec);

        const std::string tmp = m_sPath + ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                error = "cannot open " + tmp + " for writing";
                return false;
            }
            ofs << text;
        }

        fs::rename(tmp, m_sPath, ec);
        if (ec)
        {
            error = "cannot replace " + m_sPath + ": " + ec.message();
            return false;
        }

        RememberFile();
        return true;
    }

    bool ConfigFile::Save(char* error, size_t maxlen)
    {
        std::string why;
        if (!Write(m_Data.dump(4) + "\n", why))
        {
            if (error && maxlen)
                V_snprintf(error, maxlen, "%s", why.c_str());
            return false;
        }
        return true;
    }

    bool ConfigFile::Reload(char* error, size_t maxlen)
    {
        std::string why;
        if (!Read(ConfigChange::Reload, why))
        {
            if (error && maxlen)
                V_snprintf(error, maxlen, "%s", why.c_str());
            return false;
        }
        Notify(ConfigChange::Reload);
        return true;
    }

    bool ConfigFile::ReloadFromCommand(char* error, size_t maxlen)
    {
        std::string why;
        if (!Read(ConfigChange::Command, why))
        {
            if (error && maxlen)
                V_snprintf(error, maxlen, "%s", why.c_str());
            return false;
        }
        Notify(ConfigChange::Command);
        return true;
    }

    void ConfigFile::RememberFile()
    {
        std::error_code ec;
        m_LastWrite = fs::last_write_time(m_sPath, ec);
        m_bPending = false;

        std::string text;
        m_nLastHash = ReadText(m_sPath, text) ? std::hash<std::string>{}(text) : 0;
    }

    void ConfigFile::Notify(ConfigChange reason)
    {
        if (!m_pListener)
            return;

        slow::Guard guard("config callback", m_sName.c_str(), m_nOwner);
        m_pListener->OnConfigChanged(this, reason);
    }

    bool ConfigFile::Read(ConfigChange reason, std::string& error)
    {
        std::error_code ec;
        const bool first = !m_bLoaded;

        if (!(m_nFlags & SOURCE2MOD_CONFIG_NO_EXAMPLE))
        {
            std::string current;
            if (!ReadText(m_sExamplePath, current) || current != m_sDefaultsText)
            {
                fs::create_directories(fs::path(m_sExamplePath).parent_path(), ec);
                std::ofstream ofs(m_sExamplePath, std::ios::binary | std::ios::trunc);
                if (ofs)
                    ofs << m_sDefaultsText;
                else
                    FP_WARN("Could not write {}", m_sExamplePath);
            }
        }

        if (!fs::exists(m_sPath, ec))
        {
            std::string why;
            if (Write(m_sDefaultsText, why))
                FP_INFO("{} not found, created it from the defaults.", m_sPath);
            else
                FP_WARN("{} not found and not created: {}", m_sPath, why);
        }

        std::string text;
        ojson data;
        std::string parseError;
        const bool read = ReadText(m_sPath, text);
        const size_t hash = read ? std::hash<std::string>{}(text) : 0;

        if (!read)
            parseError = "cannot open the file";
        else
            ParseText(text, data, parseError);

        if (!parseError.empty())
        {
            error = m_sPath + ": " + parseError;
            if (!first || (m_nFlags & SOURCE2MOD_CONFIG_REQUIRED))
                return false;

            // The first read without a usable file: run on the defaults and
            // say so loudly, rather than leave the plugin with nothing.
            FP_ERROR("{} -- using the defaults until it is fixed", error);
            data = m_Defaults;
        }

        std::vector<std::string> added, wrongType;
        if (!(m_nFlags & SOURCE2MOD_CONFIG_NO_MERGE))
            Merge(data, m_Defaults, "", added, wrongType);

        for (const auto& w : wrongType)
            FP_WARN("{}: {}", m_sPath, w);

        if (m_pListener)
        {
            auto doc = MakeDoc(data);
            char why[512] = "";
            bool ok;
            {
                slow::Guard guard("config callback", m_sName.c_str(), m_nOwner);
                ok = m_pListener->OnConfigValidate(this, doc->Root(), why, sizeof(why));
            }
            if (!ok)
            {
                error = m_sPath + ": " + (why[0] ? why : "refused by the plugin");
                return false;
            }
        }

        if (!added.empty() && parseError.empty())
        {
            if (m_nFlags & SOURCE2MOD_CONFIG_WRITE_MISSING)
            {
                std::string why;
                if (Write(data.dump(4) + "\n", why))
                    FP_INFO("{}: added {} new setting(s) from the defaults ({}), the old file is {}.bak", m_sPath, added.size(), Join(added, ", "), m_sPath);
                else
                    FP_WARN("{}: new settings not written: {}", m_sPath, why);
            }
            else
            {
                FP_INFO("{}: {} new setting(s) from the defaults ({}), see {}", m_sPath, added.size(), Join(added, ", "),
                        (m_nFlags & SOURCE2MOD_CONFIG_NO_EXAMPLE) ? "the plugin's documentation" : m_sExamplePath.c_str());
            }
        }

        Publish(std::move(data));
        m_bLoaded = true;
        m_sLastError = parseError.empty() ? Join(wrongType, "; ") : error;

        // What the watcher compares against: this read, unless the file was
        // just rewritten above (Write remembered that one).
        if (!(m_nFlags & SOURCE2MOD_CONFIG_WRITE_MISSING) || added.empty())
        {
            m_LastWrite = fs::last_write_time(m_sPath, ec);
            m_nLastHash = hash;
            m_bPending = false;
        }

        (void)reason;
        return true;
    }

    /* =========================
    ConfigsManager
    ========================= */

    namespace
    {
        class Declarations final : public ISource2ModConfigDeclarations
        {
        public:
            struct Entry
            {
                std::string name;
                std::string defaults;
                uint32_t flags;
            };

            void Add(const char* pszName, const char* pszDefaults, uint32_t nFlags) override
            {
                entries.push_back({ pszName ? pszName : "", pszDefaults ? pszDefaults : "", nFlags });
            }

            std::vector<Entry> entries;
        };
    }

    ConfigFile* ConfigsManager::Create(PluginId owner, const std::string& directory, const char* pszName, const char* pszDefaults,
                                       uint32_t nFlags, ISource2ModConfigListener* listener, std::string& error)
    {
        if (!ValidName(pszName))
        {
            error = std::string("invalid config name '") + (pszName ? pszName : "") + "'";
            return nullptr;
        }

        if (Get(owner, pszName))
        {
            error = std::string("config '") + pszName + "' is already registered";
            return nullptr;
        }

        auto config = std::make_unique<ConfigFile>();
        config->m_nOwner = owner;
        config->m_sName = pszName;
        config->m_sPath = directory + "/" + pszName + ".json";
        config->m_sExamplePath = directory + "/" + pszName + ".example.json";
        config->m_sDefaultsText = pszDefaults ? pszDefaults : "";
        config->m_nFlags = nFlags;
        config->m_pListener = listener;

        std::string why;
        if (!ParseText(config->m_sDefaultsText, config->m_Defaults, why))
        {
            error = std::string("the defaults of '") + pszName + "' are not valid: " + why;
            return nullptr;
        }

        config->m_pDefaultsDoc = MakeDoc(config->m_Defaults);

        if (!config->Read(ConfigChange::Loaded, error))
            return nullptr;

        m_configs.push_back(std::move(config));
        return m_configs.back().get();
    }

    bool ConfigsManager::AddListener(ISource2ModPlugin* plugin, ISource2ModConfigListener* listener, char* error, size_t maxlen)
    {
        auto fail = [&](const std::string& why)
        {
            if (error && maxlen)
                V_snprintf(error, maxlen, "%s", why.c_str());
            return false;
        };

        const PluginId owner = IdOf(plugin);
        if (owner < 0)
            return fail("AddListener: unknown plugin");
        if (!listener)
            return fail("AddListener: no listener");

        const char* directory = paths::pathsManager.PluginConfigDirectory(owner);
        if (!directory)
            return fail("AddListener: the plugin has no name to keep its configs under");

        // A second call replaces the first: its declared configs go with it.
        for (size_t i = m_configs.size(); i-- > 0;)
        {
            if (m_configs[i]->m_nOwner == owner && m_configs[i]->m_bDeclared)
                Unregister(m_configs[i].get());
        }
        m_listeners[owner] = listener;

        Declarations declarations;
        {
            slow::Guard guard("config callback", "OnDeclareConfigs", owner);
            listener->OnDeclareConfigs(&declarations);
        }

        std::vector<ConfigFile*> created;
        for (const auto& entry : declarations.entries)
        {
            std::string why;
            ConfigFile* config = Create(owner, directory, entry.name.c_str(), entry.defaults.c_str(), entry.flags, listener, why);
            if (!config)
            {
                // All or nothing: the plugin's Load() fails on this.
                for (ConfigFile* c : created)
                    Unregister(c);
                m_listeners.erase(owner);
                return fail(why);
            }
            config->m_bDeclared = true;
            created.push_back(config);
        }

        for (ConfigFile* config : created)
            config->Notify(ConfigChange::Loaded);

        return true;
    }

    void ConfigsManager::RemoveListener(ISource2ModPlugin* plugin)
    {
        const PluginId owner = IdOf(plugin);
        if (owner < 0)
            return;

        m_listeners.erase(owner);
        for (auto& config : m_configs)
        {
            if (config->m_nOwner == owner && config->m_bDeclared)
                config->m_pListener = nullptr;
        }
    }

    ISource2ModConfig* ConfigsManager::Register(ISource2ModPlugin* plugin, const char* pszName, const char* pszDefaults, uint32_t nFlags,
                                                ISource2ModConfigListener* listener, char* error, size_t maxlen)
    {
        std::string why;
        ConfigFile* config = nullptr;

        const PluginId owner = IdOf(plugin);
        const char* directory = owner >= 0 ? paths::pathsManager.PluginConfigDirectory(owner) : nullptr;

        if (owner < 0)
            why = "Register: unknown plugin";
        else if (!directory)
            why = "Register: the plugin has no name to keep its configs under";
        else
            config = Create(owner, directory, pszName, pszDefaults, nFlags, listener, why);

        if (!config)
        {
            if (error && maxlen)
                V_snprintf(error, maxlen, "%s", why.c_str());
            return nullptr;
        }

        config->Notify(ConfigChange::Loaded);
        return config;
    }

    ConfigFile* ConfigsManager::RegisterCore(const char* pszName, const char* pszDefaults, uint32_t nFlags,
                                             ISource2ModConfigListener* listener, std::string& error)
    {
        ConfigFile* config = Create(0, paths::GetConfigsDirectory(), pszName, pszDefaults, nFlags, listener, error);
        if (config)
            config->Notify(ConfigChange::Loaded);
        return config;
    }

    void ConfigsManager::Unregister(ISource2ModConfig* config)
    {
        auto it = std::find_if(m_configs.begin(), m_configs.end(), [config](const auto& c) { return c.get() == config; });
        if (it == m_configs.end())
            return;

        // Its documents may still be read this frame.
        if ((*it)->m_pDoc)
            Retire(std::move((*it)->m_pDoc));
        if ((*it)->m_pDefaultsDoc)
            Retire(std::move((*it)->m_pDefaultsDoc));
        m_configs.erase(it);
    }

    ISource2ModConfig* ConfigsManager::Get(PluginId owner, const char* pszName)
    {
        if (!pszName)
            return nullptr;

        for (auto& config : m_configs)
        {
            if (config->m_nOwner == owner && config->m_sName == pszName)
                return config.get();
        }
        return nullptr;
    }

    void ConfigsManager::Retire(std::unique_ptr<json::CJSONDocument> doc)
    {
        m_retired.push_back(std::move(doc));
        if (m_bRetireQueued)
            return;

        m_bRetireQueued = true;
        scheduler::schedulerManager.NextFrame(0, [this]()
        {
            m_retired.clear();
            m_bRetireQueued = false;
        });
    }

    void ConfigsManager::RemoveAllForPlugin(PluginId id)
    {
        m_listeners.erase(id);

        // Nothing of the plugin's is called from here on: the listeners are
        // its code. The documents are the core's and go right away.
        m_configs.erase(std::remove_if(m_configs.begin(), m_configs.end(), [id](const auto& c) { return c->m_nOwner == id; }),
                        m_configs.end());
    }

    void ConfigsManager::StartWatching()
    {
        if (m_pPollTimer || !coreconfig::coreConfig.ConfigHotReloadEnabled)
            return;

        m_pPollTimer = scheduler::schedulerManager.AddTimer(0, 1.0f, [this]() { Poll(); }, TIMER_FLAG_REPEAT);
    }

    void ConfigsManager::Poll()
    {
        // By index: a callback may register or unregister configs.
        for (size_t i = 0; i < m_configs.size(); i++)
        {
            ConfigFile* config = m_configs[i].get();
            if (config->m_nFlags & SOURCE2MOD_CONFIG_NO_WATCH)
                continue;

            std::error_code ec;
            const auto write = fs::last_write_time(config->m_sPath, ec);
            if (ec || write == config->m_LastWrite)
            {
                config->m_bPending = false;
                continue;
            }

            // Seen once: wait one more poll, an editor may still be writing.
            if (!config->m_bPending || write != config->m_PendingWrite)
            {
                config->m_bPending = true;
                config->m_PendingWrite = write;
                continue;
            }

            config->m_bPending = false;

            std::string text;
            if (ReadText(config->m_sPath, text) && std::hash<std::string>{}(text) == config->m_nLastHash)
            {
                config->m_LastWrite = write;
                continue;
            }

            std::string error;
            if (!config->Read(ConfigChange::FileChanged, error))
            {
                FP_ERROR("{} not reloaded: {}", config->m_sPath, error);
                // Not again until it changes once more.
                config->m_LastWrite = write;
                config->m_nLastHash = std::hash<std::string>{}(text);
                continue;
            }

            FP_INFO("{} changed, reloaded.", config->m_sPath);
            config->Notify(ConfigChange::FileChanged);
        }
    }

    void ConfigsManager::Shutdown()
    {
        if (m_pPollTimer)
        {
            scheduler::schedulerManager.KillTimer(m_pPollTimer);
            m_pPollTimer = nullptr;
        }

        m_listeners.clear();
        m_configs.clear();
        m_retired.clear();
        m_bRetireQueued = false;
    }
}
