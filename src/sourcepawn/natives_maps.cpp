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
 * SourcePawn natives for maps: Source2ModMaps (source2mod/maps.inc) and
 * SourceMod's own names for the same (GetCurrentMap, IsMapValid, FindMap,
 * GetMapDisplayName -- halflife.inc; SetNextMap, GetNextMap, ForceChangeLevel,
 * GetMapHistory* -- nextmap.inc; ReadMapList -- source2mod.inc), all on
 * ISource2ModMaps.
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/game/maps.h"

#include <interfaces/interfaces.h>
#include <matchmaking/igametypes.h>
#include <tier0/utlstringlist.h>

#include <functional>
#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // ByteCountToCells(PLATFORM_MAX_PATH): what a map name list holds per item.
        constexpr cell_t kMapBlockSize = 64;

        maps::MapsManager& Maps()
        {
            return maps::mapsManager;
        }

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

        // ---- shared by both spellings ------------------------------------------

        // (char[] buffer, int maxlen)
        cell_t N_GetCurrentMap(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[1], params[2], Maps().GetCurrentMap());
        }

        // (const char[] map)
        cell_t N_IsMapValid(IPluginContext* ctx, const cell_t* params)
        {
            return Maps().IsMapValid(LocalString(ctx, params[1]));
        }

        // (const char[] map, char[] found, int maxlen)
        cell_t N_FindMap(IPluginContext* ctx, const cell_t* params)
        {
            std::string found;
            const Source2ModFindMapResult result = Maps().FindMap(LocalString(ctx, params[1]), found);
            ToBuffer(ctx, params[2], params[3], found.c_str());
            return result;
        }

        // (const char[] map)
        cell_t N_SetNextMap(IPluginContext* ctx, const cell_t* params)
        {
            return Maps().SetNextMap(LocalString(ctx, params[1]));
        }

        // (char[] map, int maxlen)
        cell_t N_GetNextMap(IPluginContext* ctx, const cell_t* params)
        {
            const char* next = Maps().GetNextMap();
            ToBuffer(ctx, params[1], params[2], next);
            return *next != '\0';
        }

        // (const char[] map, const char[] reason)
        cell_t N_ChangeLevel(IPluginContext* ctx, const cell_t* params)
        {
            const char* reason = params[0] >= 2 ? LocalString(ctx, params[2]) : "";
            return Maps().ChangeLevel(LocalString(ctx, params[1]), reason);
        }

        cell_t N_GetMapHistorySize(IPluginContext*, const cell_t*)
        {
            return Maps().GetMapHistorySize();
        }

        // (int item, char[] map, int mapLen, char[] reason, int reasonLen, int &startTime)
        bool MapHistory(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModMapHistoryEntry entry;
            if (!Maps().GetMapHistory(params[1], entry))
                return false;
            ToBuffer(ctx, params[2], params[3], entry.map.c_str());
            ToBuffer(ctx, params[4], params[5], entry.reason.c_str());
            cell_t* start;
            ctx->LocalToPhysAddr(params[6], &start);
            *start = static_cast<cell_t>(entry.startTime);
            return true;
        }

        // ---- SourceMod -----------------------------------------------------------

        cell_t ListSerial(const std::vector<std::string>& list)
        {
            size_t h = 0;
            for (const std::string& map : list)
                h = h * 31 + std::hash<std::string>()(map);
            return static_cast<cell_t>(h & 0x7fffffff);
        }

        // ---- Source2ModMaps ------------------------------------------------------

        cell_t N_MapsGetMapHistory(IPluginContext* ctx, const cell_t* params)
        {
            return MapHistory(ctx, params);
        }

        // public static native ArrayList ReadMapList(const char[] name="default", int flags=0, ArrayList list=null);
        cell_t N_MapsReadMapList(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<std::string> list;
            if (!Maps().ReadMapList(LocalString(ctx, params[1]), list, params[2]))
                return 0;
            return FillStringList(ctx, params[3], list, false, kMapBlockSize, CallerId(ctx));
        }

        // ---- Source2ModGameTypes (IGameTypes, gamemodes.txt / gamemodes_server.txt) ----

        // Null on a server that does not load matchmaking; every native then answers "nothing".
        IGameTypes* GameTypes() { return g_pGameTypes; }

        cell_t N_GTGetCurrentGameType(IPluginContext*, const cell_t*)
        {
            return GameTypes() ? GameTypes()->GetCurrentGameType() : -1;
        }

        cell_t N_GTGetCurrentGameMode(IPluginContext*, const cell_t*)
        {
            return GameTypes() ? GameTypes()->GetCurrentGameMode() : -1;
        }

        // (char[] buffer, int maxlen)
        cell_t N_GTGetCurrentGameModeName(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = GameTypes() ? GameTypes()->GetCurrentGameModeName() : nullptr;
            return ToBuffer(ctx, params[1], params[2], name ? name : "");
        }

        // (char[] buffer, int maxlen)
        cell_t N_GTGetCurrentMapGroup(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = GameTypes() ? GameTypes()->GetCurrentMapGroupName() : nullptr;
            return ToBuffer(ctx, params[1], params[2], name ? name : "");
        }

        // (int gameType, char[] buffer, int maxlen)
        cell_t N_GTGetGameTypeName(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = GameTypes() ? GameTypes()->GetGameTypeFromInt(params[1]) : nullptr;
            return ToBuffer(ctx, params[2], params[3], name ? name : "");
        }

        // (int gameType, int gameMode, char[] buffer, int maxlen)
        cell_t N_GTGetGameModeName(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = GameTypes() ? GameTypes()->GetGameModeFromInt(params[1], params[2]) : nullptr;
            return ToBuffer(ctx, params[3], params[4], name ? name : "");
        }

        // (const char[] mapGroup)
        cell_t N_GTIsValidMapGroup(IPluginContext* ctx, const cell_t* params)
        {
            return GameTypes() && GameTypes()->IsValidMapGroupName(LocalString(ctx, params[1]));
        }

        // (const char[] mapGroup, ArrayList list = null)
        cell_t N_GTGetMapGroupMaps(IPluginContext* ctx, const cell_t* params)
        {
            const CUtlStringList* maps = GameTypes() ? GameTypes()->GetMapGroupMapList(LocalString(ctx, params[1])) : nullptr;
            if (!maps)
                return 0;
            std::vector<std::string> list;
            for (int i = 0; i < maps->Count(); i++)
                if ((*maps)[i])
                    list.emplace_back((*maps)[i]);
            return FillStringList(ctx, params[2], list, false, kMapBlockSize, CallerId(ctx));
        }

        // (const char[] mapGroup, char[] buffer, int maxlen)
        cell_t N_GTGetRandomMap(IPluginContext* ctx, const cell_t* params)
        {
            const char* map = GameTypes() ? GameTypes()->GetRandomMap(LocalString(ctx, params[1])) : nullptr;
            return map ? ToBuffer(ctx, params[2], params[3], map) : 0;
        }

        // (int gameType, int gameMode)
        cell_t N_GTGetMaxPlayers(IPluginContext*, const cell_t* params)
        {
            return GameTypes() ? GameTypes()->GetMaxPlayersForTypeAndMode(params[1], params[2], 0) : 0;
        }

        const sp_nativeinfo_t g_mapNatives[] = {
            // halflife.inc / nextmap.inc / source2mod.inc

            // source2mod/maps.inc
            { "Source2ModMaps.GetCurrentMap", N_GetCurrentMap },
            { "Source2ModMaps.IsMapValid", N_IsMapValid },
            { "Source2ModMaps.FindMap", N_FindMap },
            { "Source2ModMaps.SetNextMap", N_SetNextMap },
            { "Source2ModMaps.GetNextMap", N_GetNextMap },
            { "Source2ModMaps.ChangeLevel", N_ChangeLevel },
            { "Source2ModMaps.GetMapHistorySize", N_GetMapHistorySize },
            { "Source2ModMaps.GetMapHistory", N_MapsGetMapHistory },
            { "Source2ModMaps.ReadMapList", N_MapsReadMapList },
            { "Source2ModGameTypes.GetCurrentGameType", N_GTGetCurrentGameType },
            { "Source2ModGameTypes.GetCurrentGameMode", N_GTGetCurrentGameMode },
            { "Source2ModGameTypes.GetCurrentGameModeName", N_GTGetCurrentGameModeName },
            { "Source2ModGameTypes.GetCurrentMapGroup", N_GTGetCurrentMapGroup },
            { "Source2ModGameTypes.GetGameTypeName", N_GTGetGameTypeName },
            { "Source2ModGameTypes.GetGameModeName", N_GTGetGameModeName },
            { "Source2ModGameTypes.IsValidMapGroup", N_GTIsValidMapGroup },
            { "Source2ModGameTypes.GetMapGroupMaps", N_GTGetMapGroupMaps },
            { "Source2ModGameTypes.GetRandomMap", N_GTGetRandomMap },
            { "Source2ModGameTypes.GetMaxPlayers", N_GTGetMaxPlayers },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetMapNatives()
    {
        return g_mapNatives;
    }
}
