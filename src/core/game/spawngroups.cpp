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

#include "core/game/spawngroups.h"

#include "core/runtime/shared.h"
#include "utils/log.h"

#include <engine2/isource2engine.h>
#include <engine2/ivengineserver2.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <filesystem/ifilesystem.h>
#include <mathlib/mathlib.h>

#include <cstring>
#include <string>
#include <vector>

namespace spawngroups
{
    SpawnGroupsManager spawnGroupsManager;

    namespace
    {
        bool Valid(Source2ModSpawnGroup group)
        {
            return group != 0 && group != SPAWN_GROUP_HANDLE_DEFAULT;
        }

        // A world the engine cannot open is not refused by LoadSpawnGroup: the group
        // just never loads, and the next level load tries it again and fails with
        // NETWORK_DISCONNECT_CLIENT_NO_MAP, leaving the server idle (CS2 14197). So
        // it has to be there first: maps/<world>.vpk (a map, a prefab of its own) or
        // maps/<world>/world.vwrld_c (inside a mounted one), "maps/" added when the
        // name has none, as the engine does.
        bool WorldExists(const char* worldName)
        {
            std::string base = worldName;
            if (base.rfind("maps/", 0) != 0)
                base = "maps/" + base;

            if (g_pFullFileSystem)
            {
                if (g_pFullFileSystem->FileExists((base + ".vpk").c_str(), "GAME") ||
                    g_pFullFileSystem->FileExists((base + "/world.vwrld_c").c_str(), "GAME"))
                    return true;
            }
            return !strchr(worldName, '/') && g_pEngineServer->IsMapValid(worldName);
        }
    }

    Source2ModSpawnGroup SpawnGroupsManager::Load(PluginId owner, const SpawnGroupLoadInfo& info, SpawnGroupCallback callback)
    {
        if (!g_pEngineServer || !info.worldName || !*info.worldName)
            return 0;
        if (!WorldExists(info.worldName))
        {
            FP_WARN("Spawn group world \"{}\" does not exist (no maps/<world>.vpk or world.vwrld_c), not loaded", info.worldName);
            return 0;
        }

        SpawnGroupDesc_t desc;
        desc.m_sWorldName = info.worldName;
        desc.m_sDescriptiveName = info.name && *info.name ? info.name : info.worldName;
        if (info.entityLumpName && *info.entityLumpName)
            desc.m_sEntityLumpName = info.entityLumpName;
        if (info.entityFilterName && *info.entityFilterName)
            desc.m_sEntityFilterName = info.entityFilterName;
        if (info.localNameFixup && *info.localNameFixup)
            desc.m_sLocalNameFixup = info.localNameFixup;

        AngleMatrix(info.angles, info.origin, desc.m_vecWorldOffset);

        desc.m_bDontSpawnEntities = info.flags & SPAWNGROUP_LOAD_DONT_SPAWN_ENTITIES;
        desc.m_bSuppressMapEntities = info.flags & SPAWNGROUP_LOAD_SUPPRESS_MAP_ENTITIES;
        desc.m_bBlockUntilLoaded = info.flags & SPAWNGROUP_LOAD_BLOCK_UNTIL_LOADED;
        desc.m_bSetActivePostLoad = info.flags & SPAWNGROUP_LOAD_SET_ACTIVE;
        desc.m_bDisableBaseWorldLayer = info.flags & SPAWNGROUP_LOAD_DISABLE_BASE_LAYER;
        desc.m_bLoadStreamingData = !(info.flags & SPAWNGROUP_LOAD_NO_STREAMING_DATA);
        // Not part of a save game, like the engine's point_prefab groups.
        desc.m_bIncludeInSaveGame = false;

        const SpawnGroupHandle_t handle = g_pEngineServer->LoadSpawnGroup(desc);
        if (!Valid(handle))
        {
            FP_WARN("Spawn group \"{}\" ({}) could not be loaded", desc.m_sDescriptiveName.Get(), info.worldName);
            return 0;
        }

        Group& group = m_groups[handle];
        group.owner = owner;
        group.callback = std::move(callback);
        group.name = desc.m_sDescriptiveName.Get();
        group.ready = false;
        group.seen = g_pEngineServer->IsSpawnGroupLoaded(handle) || g_pEngineServer->IsSpawnGroupLoading(handle);
        return handle;
    }

    bool SpawnGroupsManager::Unload(Source2ModSpawnGroup group)
    {
        if (!g_pEngineServer || !Valid(group))
            return false;
        if (!g_pEngineServer->IsSpawnGroupLoaded(group) && !g_pEngineServer->IsSpawnGroupLoading(group))
        {
            m_groups.erase(group);
            return false;
        }

        // Forgotten first: the engine hands the handle out again.
        m_groups.erase(group);
        g_pEngineServer->UnloadSpawnGroup(group, kSGUO_None);
        return true;
    }

    Source2ModSpawnGroup SpawnGroupsManager::FindByName(const char* name)
    {
        if (!g_pEngineServer || !name || !*name)
            return 0;
        for (auto& [handle, group] : m_groups)
            if (group.name == name)
                return handle;
        // The engine looks up the world name ("de_dust2", "maps/prefabs/...").
        const SpawnGroupHandle_t handle = g_pEngineServer->FindSpawnGroupByName(name);
        return Valid(handle) ? handle : 0;
    }

    bool SpawnGroupsManager::IsLoaded(Source2ModSpawnGroup group)
    {
        return g_pEngineServer && Valid(group) && g_pEngineServer->IsSpawnGroupLoaded(group);
    }

    bool SpawnGroupsManager::IsLoading(Source2ModSpawnGroup group)
    {
        return g_pEngineServer && Valid(group) && g_pEngineServer->IsSpawnGroupLoading(group);
    }

    bool SpawnGroupsManager::MakeActive(Source2ModSpawnGroup group)
    {
        if (!IsLoaded(group))
            return false;
        g_pEngineServer->MakeSpawnGroupActive(group);
        return true;
    }

    Source2ModSpawnGroup SpawnGroupsManager::GetEntitySpawnGroup(CEntityInstance* entity)
    {
        if (!entity || !entity->m_pEntity)
            return 0;
        const SpawnGroupHandle_t handle = entity->m_pEntity->GetSpawnGroup();
        return Valid(handle) ? handle : 0;
    }

    void SpawnGroupsManager::OnGameFrame(bool)
    {
        if (m_groups.empty() || !g_pEngineServer)
            return;

        // Callbacks may load or unload groups: collect first.
        std::vector<std::pair<Source2ModSpawnGroup, bool>> changed;
        for (auto& [handle, group] : m_groups)
        {
            const bool loaded = g_pEngineServer->IsSpawnGroupLoaded(handle);
            const bool loading = !loaded && g_pEngineServer->IsSpawnGroupLoading(handle);
            if (loaded || loading)
                group.seen = true;

            if (!group.ready && loaded)
                changed.emplace_back(handle, true);
            else if (group.seen && !loaded && !loading)
                changed.emplace_back(handle, false);
        }

        for (auto [handle, loaded] : changed)
        {
            auto it = m_groups.find(handle);
            if (it == m_groups.end())
                continue;

            SpawnGroupCallback callback = it->second.callback;
            if (loaded)
                it->second.ready = true;
            else
                m_groups.erase(it);

            if (callback)
                callback(handle, loaded);
        }
    }

    void SpawnGroupsManager::OnLevelShutdown()
    {
        // The engine unloads them with the map, and hands the handles out again.
        std::vector<std::pair<Source2ModSpawnGroup, SpawnGroupCallback>> gone;
        for (auto& [handle, group] : m_groups)
            if (group.callback)
                gone.emplace_back(handle, group.callback);
        m_groups.clear();

        for (auto& [handle, callback] : gone)
            callback(handle, false);
    }

    void SpawnGroupsManager::RemoveAllForPlugin(PluginId owner)
    {
        std::vector<Source2ModSpawnGroup> mine;
        for (auto& [handle, group] : m_groups)
            if (group.owner == owner)
                mine.push_back(handle);

        for (Source2ModSpawnGroup handle : mine)
            Unload(handle);
    }

    void SpawnGroupsManager::Shutdown()
    {
        // The groups stay with the map; only the callbacks into plugins go.
        m_groups.clear();
    }
}
