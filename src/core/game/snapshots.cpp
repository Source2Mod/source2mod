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

#include "core/game/snapshots.h"

#include "core/runtime/shared.h"

#include <engine2/isource2server.h>
#include <entity2/entityclass.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <networksystem/iflattenedserializers.h>
#include <networksystem/serializedentity.h>
#include <tier0/utlleanvector.h>

#include <cstring>
#include <string>

namespace snapshots
{
    SnapshotsManager snapshotsManager;

    namespace
    {
        constexpr NetworkSerializationMode_t kMode = NET_SERIALIZATION_MODE_SERVER;

        // ServerClass as the engine lays it out (IServerGameDLL::GetAllServerClasses,
        // the list the sign-on send tables come from). CEntityClass carries the same
        // serializer, but s2sdk's offset of it there is not the game's.
        struct ServerClassHead
        {
            const char* dllClassName;
            const char* networkName;
            CEntityClass* entityClass;
            FlattenedSerializerDesc_t flattenedSerializer;
        };

        const ServerClassHead* ServerClassOf(CEntityInstance* entity)
        {
            if (!entity || !entity->m_pEntity || !entity->m_pEntity->m_pClass || !g_pSource2Server)
                return nullptr;
            CEntityClass* cls = entity->m_pEntity->m_pClass;
            CUtlVector<ServerClass*>* classes = g_pSource2Server->GetAllServerClasses();
            if (!classes)
                return nullptr;
            for (int i = 0; i < classes->Count(); i++)
            {
                const auto* c = reinterpret_cast<const ServerClassHead*>(classes->Element(i));
                if (c && c->entityClass == cls)
                    return c->flattenedSerializer.m_handle ? c : nullptr;
            }
            return nullptr;
        }

        const FlattenedSerializerDesc_t* SerializerOf(CEntityInstance* entity)
        {
            const ServerClassHead* c = ServerClassOf(entity);
            return c ? &c->flattenedSerializer : nullptr;
        }

        // Encodes the entity now; null when it cannot be. ReleaseSerializedEntity() after.
        CSerializedEntity* Encode(CEntityInstance* entity, const FlattenedSerializerDesc_t*& desc)
        {
            if (!g_pFlattenedSerializers || !g_pSerializedEntities)
                return nullptr;
            desc = SerializerOf(entity);
            if (!desc)
                return nullptr;

            CSerializedEntity* ser = g_pSerializedEntities->AllocateSerializedEntity(*desc);
            if (!ser)
                return nullptr;

            CSendProxyDataList proxies{};
            if (!g_pFlattenedSerializers->Encode(*desc, ser, entity, entity->GetEntityIndex().Get(), kMode, &proxies))
            {
                g_pSerializedEntities->ReleaseSerializedEntity(ser);
                return nullptr;
            }
            return ser;
        }
    }

    bool SnapshotsManager::IsAvailable()
    {
        return g_pFlattenedSerializers && g_pSerializedEntities;
    }

    const char* SnapshotsManager::GetNetworkClassName(CEntityInstance* entity)
    {
        const ServerClassHead* c = ServerClassOf(entity);
        return c ? c->networkName : nullptr;
    }

    int SnapshotsManager::Dump(CEntityInstance* entity, SnapshotLineCallback callback)
    {
        const FlattenedSerializerDesc_t* desc = nullptr;
        CSerializedEntity* ser = Encode(entity, desc);
        if (!ser)
            return -1;

        CUtlVector<CUtlString> lines;
        g_pFlattenedSerializers->DumpSerializedEntityToLines(*desc, "", ser, entity->GetEntityIndex().Get(), kMode, &lines);
        g_pSerializedEntities->ReleaseSerializedEntity(ser);

        if (callback)
            for (int i = 0; i < lines.Count(); i++)
                callback(lines[i].Get());
        return lines.Count();
    }

    namespace
    {
        // One field line of IFlattenedSerializers::DumpSerializedEntityToLines (14197):
        //   "<label>:  <n> : <path a/b/c> bits <x> to <y> [<k> bits] : <name> : <type> = '<value>'[ [COMPLEX]]"
        // n counts the entity's fields in order, the order GetSerializedEntityFieldCount
        // gives their packed paths in. "metadata(...)" lines carry no field.
        struct DumpField
        {
            int index = -1;
            std::string name, type, value;
        };

        bool ParseDumpLine(const char* line, DumpField& out)
        {
            const char* bits = strstr(line, " bits ");
            const char* close = bits ? strstr(bits, " bits] : ") : nullptr;
            if (!bits || !close)
                return false;

            // The index: the number before the path, "<n> : <path>".
            const char* colon = strstr(line, ":  ");
            if (!colon || colon > bits)
                return false;
            out.index = atoi(colon + 3);

            const char* name = close + strlen(" bits] : ");
            const char* nameEnd = strstr(name, " : ");
            if (!nameEnd)
                return false;
            const char* type = nameEnd + 3;
            const char* typeEnd = strstr(type, " = '");
            if (!typeEnd)
                return false;
            const char* value = typeEnd + 4;
            const char* valueEnd = strrchr(value, '\'');
            if (!valueEnd)
                return false;

            out.name.assign(name, nameEnd);
            out.type.assign(type, typeEnd);
            out.value.assign(value, valueEnd);
            return true;
        }
    }

    int SnapshotsManager::GetFields(CEntityInstance* entity, SnapshotFieldCallback callback)
    {
        const FlattenedSerializerDesc_t* desc = nullptr;
        CSerializedEntity* ser = Encode(entity, desc);
        if (!ser)
            return -1;

        CUtlLeanVectorFixedGrowable<uint32> paths;
        g_pSerializedEntities->GetSerializedEntityFieldCount(ser, &paths);
        CUtlVector<CUtlString> lines;
        g_pFlattenedSerializers->DumpSerializedEntityToLines(*desc, "", ser, entity->GetEntityIndex().Get(), kMode, &lines);
        g_pSerializedEntities->ReleaseSerializedEntity(ser);

        int count = 0;
        DumpField field;
        for (int i = 0; i < lines.Count(); i++)
        {
            if (!ParseDumpLine(lines[i].Get(), field))
                continue;
            count++;
            if (callback)
            {
                const uint32_t path = field.index >= 0 && field.index < paths.Count() ? paths[field.index] : 0;
                callback(path, field.name.c_str(), field.type.c_str(), field.value.c_str());
            }
        }
        return count;
    }

    bool SnapshotsManager::GetField(CEntityInstance* entity, const char* name, char* value, size_t maxlen)
    {
        if (!name || !value || !maxlen)
            return false;
        bool found = false;
        GetFields(entity, [&](uint32_t, const char* field, const char*, const char* text)
        {
            if (!found && !strcmp(field, name))
            {
                found = true;
                snprintf(value, maxlen, "%s", text);
            }
        });
        return found;
    }
}
