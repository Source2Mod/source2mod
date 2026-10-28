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

#include "core/coreconfig.h"

#include "core/configs.h"
#include "core/permissions.h"
#include "core/slowguard.h"
#include "utils/log.h"

#include <tier0_strtools.h>

#include "config_defaults_gen.h"

namespace coreconfig
{
    CoreConfig coreConfig;

    bool CoreConfig::Init(const std::string& path, char* conf_error, int conf_error_size)
    {
        m_sPath = path;

        std::string error;
        if (!configs::configsManager.RegisterCore("core", kCoreConfigDefaults, SOURCE2MOD_CONFIG_DEFAULT, this, error))
        {
            V_snprintf(conf_error, conf_error_size, "%s", error.c_str());
            return false;
        }

        return true;
    }

    void CoreConfig::Apply(const nlohmann::ordered_json& data)
    {
        PublicChatTrigger = data.value("PublicChatTrigger", PublicChatTrigger);
        SilentChatTrigger = data.value("SilentChatTrigger", SilentChatTrigger);
        PluginHotReloadEnabled = data.value("PluginHotReloadEnabled", PluginHotReloadEnabled);
        PluginAutoLoadEnabled = data.value("PluginAutoLoadEnabled", PluginAutoLoadEnabled);
        ConfigHotReloadEnabled = data.value("ConfigHotReloadEnabled", ConfigHotReloadEnabled);
        UnlockConCommands = data.value("UnlockConCommands", UnlockConCommands);
        UnlockConVars = data.value("UnlockConVars", UnlockConVars);
        CrashHandlerEnabled = data.value("CrashHandlerEnabled", CrashHandlerEnabled);
        CrashDiscordWebhook = data.value("CrashDiscordWebhook", CrashDiscordWebhook);
        HttpFallback = data.value("HttpFallback", HttpFallback);
        SlowCallbackWarnMs = data.value("SlowCallbackWarnMs", SlowCallbackWarnMs);
        SteamAuthMode = data.value("SteamAuthMode", SteamAuthMode);
        AccessDeniedMessage = data.value("AccessDeniedMessage", AccessDeniedMessage);
        GeoIPDatabase = data.value("GeoIPDatabase", GeoIPDatabase);
        ClientPrefsDatabase = data.value("ClientPrefsDatabase", ClientPrefsDatabase);
        ServerLang = data.value("ServerLang", ServerLang);
    }

    void CoreConfig::OnConfigChanged(ISource2ModConfig* config, ConfigChange reason)
    {
        const auto& data = static_cast<configs::ConfigFile*>(config)->Data();

        try
        {
            if (reason == ConfigChange::Loaded)
            {
                Apply(data);
                return;
            }

            // Read once at startup: a change waits for the next start.
            const CoreConfig before = *this;
            Apply(data);

            if (PluginHotReloadEnabled != before.PluginHotReloadEnabled || PluginAutoLoadEnabled != before.PluginAutoLoadEnabled ||
                ConfigHotReloadEnabled != before.ConfigHotReloadEnabled || UnlockConCommands != before.UnlockConCommands ||
                UnlockConVars != before.UnlockConVars || CrashHandlerEnabled != before.CrashHandlerEnabled ||
                CrashDiscordWebhook != before.CrashDiscordWebhook || GeoIPDatabase != before.GeoIPDatabase ||
                ClientPrefsDatabase != before.ClientPrefsDatabase || ServerLang != before.ServerLang)
                FP_WARN("core.json: some of the changed settings take effect after a restart");

            slow::g_flWarnMs = SlowCallbackWarnMs;
            permissions::permissionsManager.ApplyCoreConfig();
        }
        catch (const std::exception& ex)
        {
            FP_ERROR("core.json: {}", ex.what());
        }
    }

    const std::string CoreConfig::GetPath() const { return m_sPath; }

    bool CoreConfig::IsTriggerInternal(std::vector<std::string> triggers, const std::string& message,
                                        std::string& prefix) const
    {
        for (std::string& trigger : triggers)
        {
            if (message.rfind(trigger, 0) == 0)
            {
                prefix = trigger;
                return true;
            }
        }

        return false;
    }

    bool CoreConfig::IsSilentChatTrigger(const std::string& message, std::string& prefix) const
    {
        return IsTriggerInternal(SilentChatTrigger, message, prefix);
    }

    bool CoreConfig::IsPublicChatTrigger(const std::string& message, std::string& prefix) const
    {
        return IsTriggerInternal(PublicChatTrigger, message, prefix);
    }
}
