/*
 * SourcePawn natives for source2mod/stringtables.inc and the world bits next to
 * them: network string tables and the mounted addons.
 *
 * A NetworkStringTable is its table id, looked up on every call: tables are made
 * anew for each map, an id never dangles.
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/runtime/shared.h"

#include <engine2/iengineservicemgr.h>
#include <engine2/isource2engine.h>
#include <engine2/ivengineserver2.h>
#include <interfaces/interfaces.h>
#include <networkstringtabledefs.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        constexpr cell_t kNoTable = -1;

        INetworkStringTable* Table(cell_t id)
        {
            return g_pNetworkStringTableServer && id >= 0 ? g_pNetworkStringTableServer->GetTable(id) : nullptr;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_Find(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = g_pNetworkStringTableServer ? g_pNetworkStringTableServer->FindTable(LocalString(ctx, params[1])) : nullptr;
            return table ? static_cast<cell_t>(table->GetTableId()) : kNoTable;
        }

        cell_t N_GetCount(IPluginContext*, const cell_t*)
        {
            return g_pNetworkStringTableServer ? g_pNetworkStringTableServer->GetNumTables() : 0;
        }

        cell_t N_GetName(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            return ToBuffer(ctx, params[2], params[3], table ? table->GetTableName() : "");
        }

        cell_t N_NumStrings(IPluginContext*, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            return table ? table->GetNumStrings() : 0;
        }

        cell_t N_FindString(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            return table ? table->FindStringIndex(LocalString(ctx, params[2])) : INVALID_STRING_INDEX;
        }

        cell_t N_GetString(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            return ToBuffer(ctx, params[3], params[4], table ? table->GetString(params[2], true) : "");
        }

        // AddString(value, data, length)
        cell_t N_AddString(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            if (!table)
                return INVALID_STRING_INDEX;
            char* data = nullptr;
            ctx->LocalToString(params[3], &data);
            SetStringUserDataRequest_t request;
            request.m_pRawData = data;
            request.m_cbDataSize = static_cast<unsigned int>(params[4]);
            return table->AddString(true, LocalString(ctx, params[2]), params[4] > 0 ? &request : nullptr);
        }

        // GetUserData(index, buffer, maxlen): bytes as they are, the size they take even if the buffer is short.
        cell_t N_GetUserData(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            const StringUserData_t* data = table ? table->GetStringUserData(params[2]) : nullptr;
            if (!data)
                return 0;
            char* out = nullptr;
            if (data->m_pRawData && params[4] > 0 && ctx->LocalToString(params[3], &out) == SP_ERROR_NONE && out)
                std::memcpy(out, data->m_pRawData, std::min<size_t>(data->m_cbDataSize, static_cast<size_t>(params[4])));
            return static_cast<cell_t>(data->m_cbDataSize);
        }

        // SetUserData(index, data, length, force)
        cell_t N_SetUserData(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            char* data = nullptr;
            if (!table || ctx->LocalToString(params[3], &data) != SP_ERROR_NONE)
                return 0;
            SetStringUserDataRequest_t request;
            request.m_pRawData = data;
            request.m_cbDataSize = static_cast<unsigned int>(params[4]);
            table->SetStringUserData(params[2], &request, params[0] < 5 || params[5]);
            return 1;
        }

        // ---- addons -----------------------------------------------------------------

        cell_t N_GetMountedAddons(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[1], params[2], g_pEngineServiceMgr ? g_pEngineServiceMgr->GetAddonsString() : "");
        }

        cell_t N_IsAddonMounted(IPluginContext* ctx, const cell_t* params)
        {
            return g_pEngineServiceMgr && g_pEngineServiceMgr->IsAddonMounted(LocalString(ctx, params[1]));
        }

        const sp_nativeinfo_t g_natives[] = {
            { "NetworkStringTable.Find", N_Find },
            { "NetworkStringTable.GetCount", N_GetCount },
            { "NetworkStringTable.GetName", N_GetName },
            { "NetworkStringTable.NumStrings.get", N_NumStrings },
            { "NetworkStringTable.FindString", N_FindString },
            { "NetworkStringTable.GetString", N_GetString },
            { "NetworkStringTable.AddString", N_AddString },
            { "NetworkStringTable.GetUserData", N_GetUserData },
            { "NetworkStringTable.SetUserData", N_SetUserData },
            { "Source2ModMaps.GetMountedAddons", N_GetMountedAddons },
            { "Source2ModMaps.IsAddonMounted", N_IsAddonMounted },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetStringTableNatives()
    {
        return g_natives;
    }
}
