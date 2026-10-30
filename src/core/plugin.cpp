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
#include "core/plugin.h"
#include "core/appsystem/appsystem.h"

#include "core/game/addresses.h"
#include "core/game/commands.h"
#include "core/data/configs.h"
#include "core/game/convars.h"
#include "core/data/cookies.h"
#include "core/data/paths.h"
#include "core/runtime/scheduler.h"
#include "core/runtime/crashhandler.h"
#include "core/ui/customhud.h"
#include "core/game/events.h"
#include "core/data/gameconfig.h"
#include "core/hooks/dynamichooks.h"
#include "core/hooks/gamehooks.h"
#include "core/game/gamesystems.h"
#include "core/game/geoip.h"
#include "core/net/http.h"
#include "core/hooks/inlinehooks.h"
#include "core/game/maps.h"
#include "core/data/mysql.h"
#include "core/net/net.h"
#include "core/net/websockets.h"
#include "core/game/networkmessages.h"
#include "core/game/patches.h"
#include "core/game/permissions.h"
#include "core/plugins/pluginapi.h"
#include "core/plugins/pluginmanager.h"
#include "core/game/raytrace.h"
#include "core/game/scripts.h"
#include "core/ui/slotinput.h"
#include "core/data/coreconfig.h"
#include "core/runtime/shared.h"
#include "core/runtime/slowguard.h"
#include "core/game/sounds.h"
#include "core/game/transmit.h"
#include "sourcepawn/scriptplugin.h"
#include "utils/log.h"
#include "utils/paths.h"
#include "core/hooks/virtualhooks.h"

#include <source2mod/utils/plat.h>

// Name, author, version ...: plugin-metadata.json, through tools/version_gen.py.
#include "version_gen.h"

#define BUILD_TIMESTAMP __DATE__ " " __TIME__

Source2ModCore g_Source2ModCore;
PLUGIN_EXPOSE(Source2Mod, g_Source2ModCore);

bool Source2ModCore::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();
    // PLUGIN_SAVEVARS() just fetched metamod's KHook; every source2mod plugin gets the
    // same engine through SOURCE2MOD_KHOOK_INTERFACE, so hooks placed anywhere on the
    // server can call through each other's originals.

    if (late)
    {
        g_SMAPI->Format(error, maxlen, "Late load is not supported");
        return false;
    }

#ifdef _WIN32
    // Everything the server prints is UTF-8, but a Windows console shows it in
    // the OEM code page (852, 437, ...) until told otherwise.
    if (GetConsoleWindow())
        SetConsoleOutputCP(CP_UTF8);
#endif

    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2ServerConfig, ISource2ServerConfig, SOURCE2SERVERCONFIG_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2GameClients, ISource2GameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2GameEntities, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pSoundSystem, ISoundSystem, SOUNDSYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkSystem, INetworkSystem, NETWORKSYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkStringTableServer, INetworkStringTableContainer, SOURCE2ENGINETOSERVERSTRINGTABLE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServiceMgr, IEngineServiceMgr, ENGINESERVICEMGR_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pHostStateMgr, IHostStateMgr, HOSTSTATEMGR_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pLocalize, ILocalize, LOCALIZE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

    // IGameEventSystem has no global in interfaces.h at all. The schema system
    // and game resource service do, but as the interface type -- Source2Mod
    // needs the concrete classes' methods, so it downcasts once here.
    GET_V_IFACE_CURRENT(GetEngineFactory, shared::g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);

    shared::g_pSchemaSystem = static_cast<CSchemaSystem*>(g_pSchemaSystem);

    log::Init();

    // Before anything below goes looking for one of them.
    paths::EnsureLayout();

    char initError[512] = "";
    if (!appsystem::InitAll(initError, sizeof(initError)))
    {
        FP_ERROR("{}", initError);
        return false;
    }

    g_SMAPI->AddListener(this, this);

    FP_DEBUG("Load() success!");

    if (!pluginManager.LoadAll())
    {
        FP_ERROR("Could not load plugins.");
        return false;
    }

    pluginManager.StartFileWatcher();
    configs::configsManager.StartWatching();

    return true;
}

bool Source2ModCore::Unload(char* error, size_t maxlen)
{
    pluginManager.StopFileWatcher();

    // Whatever a plugin registered that holds code inside its own library --
    // timers, HTTP callbacks, database connections, entity listeners, open
    // menus -- goes with that plugin, one at a time, while it is still
    // mapped. See the RemoveAllForPlugin() calls in PluginManager.
    pluginManager.UnloadAll();

    appsystem::ShutdownAll();

    // A refused late load returned before log::Init(), and Metamod still calls
    // Unload() on a plugin whose Load() failed: no logger to write to then.
    if (log::GetLogger())
        FP_DEBUG("Unload() success!");

    log::Close();

    return true;
}

void Source2ModCore::AllPluginsLoaded()
{
    pluginManager.FireMetamodLoaded();
}

// These two are IMetamodListener's, so the id is a Metamod plugin's -- they go
// to Source2Mod's own OnMetamodPluginLoad/Unload, not to the callbacks that
// report .smx plugins coming and going.
void Source2ModCore::OnPluginLoad(PluginId id)
{
    pluginManager.FireMetamodPluginLoaded(id);
}

void Source2ModCore::OnPluginUnload(PluginId id)
{
    pluginManager.FireMetamodPluginUnloaded(id);
}

void Source2ModCore::OnLevelInit(char const* pMapName, char const* pMapEntities, char const* pOldLevel,
                              char const* pLandmarkName, bool loadGame, bool background)
{
    appsystem::LevelInit(pMapName);

    pluginManager.OnLevelInit(pMapName, pMapEntities, pOldLevel, pLandmarkName, loadGame, background);
}

void* Source2ModCore::OnMetamodQuery(const char* iface, int* ret)
{
    // Only what a source2mod plugin chose to expose through OnMetamodQuery, which
    // answers like OnSource2ModQuery unless the plugin says otherwise. The
    // source2mod's own interfaces are deliberately not offered here: they are for
    // source2mod plugins; a metamod plugin already has KHook from metamod itself.
    for (auto& p : pluginManager.m_plugins)
    {
        for (auto* l : p->listeners)
        {
            if (void* res = l->OnMetamodQuery(iface, ret))
            {
                if (ret) *ret = META_IFACE_OK;
                return res;
            }
        }
    }

    if (ret) *ret = META_IFACE_FAILED;
    return nullptr;
}

void Source2ModCore::OnLevelShutdown()
{
    pluginManager.OnLevelShutdown();

    // Layouts, playing sounds, managed visibility and the scripts' entities do
    // not survive the level change; persistent scripts come back on the next map.
    appsystem::LevelShutdown();
}

const char* Source2ModCore::GetAuthor() { return PLUGIN_AUTHOR; }
const char* Source2ModCore::GetName() { return PLUGIN_DISPLAY_NAME; }
const char* Source2ModCore::GetDescription() { return PLUGIN_DESCRIPTION; }
const char* Source2ModCore::GetURL() { return PLUGIN_URL; }
const char* Source2ModCore::GetLicense() { return PLUGIN_LICENSE; }
const char* Source2ModCore::GetVersion() { return PLUGIN_FULL_VERSION; }
const char* Source2ModCore::GetDate() { return BUILD_TIMESTAMP; }
const char* Source2ModCore::GetLogTag() { return PLUGIN_LOGTAG; }
