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

#include <bitbuf.h>
#include <engine2/iengineservicemgr.h>
#include <engine2/igameeventsystem.h>
#include <engine2/isource2engine.h>
#include <engine2/ivengineserver2.h>
#include <interfaces/interfaces.h>
#include <netmessages.pb.h>
#include <networkstringtabledefs.h>
#include <networksystem/inetworkmessages.h>
#include <recipientfilter.h>

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

        // ReplicateUserData(slot, value, data, length): the user data of one of
        // the table's strings as one client sees it, the table itself (and every
        // other client) unchanged. A CSVCMsg_UpdateStringTable with that one
        // entry goes to the slot only; the next real change of the string, or a
        // full update, puts the client back on the server's data.
        cell_t N_ReplicateUserData(IPluginContext* ctx, const cell_t* params)
        {
            INetworkStringTable* table = Table(params[1]);
            const CPlayerSlot slot(params[2]);
            if (!table || !g_pEngineServer || !g_pNetworkMessages || !shared::g_pGameEventSystem)
                return 0;
            if (slot.Get() < 0 || slot.Get() >= ABSOLUTE_PLAYER_LIMIT || !g_pEngineServer->GetPlayerNetInfo(slot))
                return 0;

            const int index = table->FindStringIndex(LocalString(ctx, params[3]));
            if (index == INVALID_STRING_INDEX)
                return 0;

            const cell_t length = params[5];
            char* data = nullptr;
            if (length < 0 || length > 1024 || ctx->LocalToString(params[4], &data) != SP_ERROR_NONE)
                return ctx->ThrowNativeError("Invalid user data length %d (0-1024)", length);

            static NetMessageInfo_t* s_update = g_pNetworkMessages->FindNetworkMessagePartial("CSVCMsg_UpdateStringTable");
            if (!s_update)
                return 0;

            // One changed entry: the index as a delta from -1, not a new string,
            // then the user data (or none).
            uint8_t buffer[2048];
            bf_write buf(buffer, sizeof(buffer));
            buf.WriteOneBit(0);
            buf.WriteVarInt32(index - 1);
            buf.WriteOneBit(0);
            if (length > 0)
            {
                buf.WriteOneBit(1);
                buf.WriteUBitVar(static_cast<unsigned int>(length));
                buf.WriteBits(data, length * 8);
            }
            else
            {
                buf.WriteOneBit(0);
            }
            if (buf.IsOverflowed())
                return 0;

            auto* msg = s_update->AllocateMessage()->ToPB<CSVCMsg_UpdateStringTable>();
            msg->set_table_id(table->GetTableId());
            msg->set_num_changed_entries(1);
            msg->mutable_string_data()->assign(buffer, buffer + BitByte(buf.GetNumBitsWritten()));

            CReliableSingleUserRecipientFilter filter(slot);
            shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, s_update, msg, 0);
            delete msg;
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
            { "NetworkStringTable.ReplicateUserData", N_ReplicateUserData },
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
