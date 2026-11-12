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

#pragma once
#include "core/appsystem/appsystem.h"
#include "source2mod/ISource2ModSpawnGroups.h"

#include <map>
#include <string>

namespace spawngroups {
    /**
     * Spawn groups plugins load (ISource2ModSpawnGroups), through the engine's
     * IVEngineServer2::LoadSpawnGroup.
     *
     * Whether a group is ready is asked once a frame (IsSpawnGroupLoaded) for the
     * groups still loading, not heard from the game systems' PostSpawnGroupLoad:
     * that also covers a group the engine finished inside LoadSpawnGroup itself
     * (SPAWNGROUP_LOAD_BLOCK_UNTIL_LOADED), before there was a handle to match.
     */
    class SpawnGroupsManager final : public ISource2ModSpawnGroups, public appsystem::ISource2ModAppSystem {
    public:
        appsystem::SystemId GetSystemId() const override { return appsystem::SystemId::SpawnGroups; }

        Source2ModSpawnGroup Load(const SpawnGroupLoadInfo& info, SpawnGroupCallback callback = SpawnGroupCallback()) override
        {
            return Load(PluginId(0), info, std::move(callback));
        }
        Source2ModSpawnGroup Load(PluginId owner, const SpawnGroupLoadInfo& info, SpawnGroupCallback callback = SpawnGroupCallback());
        bool Unload(Source2ModSpawnGroup group) override;
        Source2ModSpawnGroup FindByName(const char* name) override;
        bool IsLoaded(Source2ModSpawnGroup group) override;
        bool IsLoading(Source2ModSpawnGroup group) override;
        bool MakeActive(Source2ModSpawnGroup group) override;
        Source2ModSpawnGroup GetEntitySpawnGroup(CEntityInstance* entity) override;

        void OnGameFrame(bool simulating) override;
        void OnLevelShutdown() override;
        void RemoveAllForPlugin(PluginId owner) override;
        void Shutdown() override;

    private:
        struct Group
        {
            PluginId owner = 0;
            SpawnGroupCallback callback;
            // SpawnGroupLoadInfo::name; the engine's FindSpawnGroupByName only
            // knows the world name.
            std::string name;
            bool ready = false;
            // Seen loading or loaded once: only then is "neither" a group gone,
            // not one the engine has not started on yet.
            bool seen = false;
        };

        std::map<Source2ModSpawnGroup, Group> m_groups;
    };

    extern SpawnGroupsManager spawnGroupsManager;
}
