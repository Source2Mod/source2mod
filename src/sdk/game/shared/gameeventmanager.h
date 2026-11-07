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
 * The game event manager as the game lays it out (libserver.so /
 * server.dll; CS2 1.41.9.x): every event the
 * .gameevents files define and who listens to it.
 *
 * Every LoadEventsFromFile defines its events again, and an event defined
 * again starts with no listeners: core.gameevents, game.gameevents and
 * mod.gameevents load one after the other, so a listener added after the
 * first file is gone from the events the later ones define (events.cpp adds
 * Source2Mod's back after each file).
 */

#ifndef GAMEEVENTMANAGER_H
#define GAMEEVENTMANAGER_H
#ifdef _WIN32
#pragma once
#endif

#include <igameevents.h>
#include <tier0/threadtools.h>
#include <tier0/utlstring.h>
#include <tier0/utlvector.h>

//-----------------------------------------------------------------------------
// Purpose: One listener of one event
//-----------------------------------------------------------------------------
class CGameEventCallback
{
public:
	// IGameEventListener2* for SERVERSIDE / CLIENTSIDE, a CServerSideClient_GameEventLegacyProxy for CLIENTSTUB
	void* m_pCallback;
	int m_nListenerType; // CGameEventManager::SERVERSIDE ...
};

//-----------------------------------------------------------------------------
// Purpose: One event: its id, its keys and its listeners
//-----------------------------------------------------------------------------
class CGameEventDescriptor
{
public:
	// The .gameevents file that defined it last
	const char* m_pszFilename;

	// Its index in CGameEventManager::m_GameEvents, the bit a client's listen bits use for it
	int m_nEventId;

	// Where its name is in the manager's name table, see CGameEventManager::GetEventName
	int m_nEventMapIndex;

	// CreateEvent without bForce returns nullptr while this is empty
	CUtlVector<CGameEventCallback*> m_Listeners;

	bool m_bLocal; // "local": never networked
	bool m_bReliable; // "reliable"
	bool m_bHasToolsDesc; // "_tools_event_desc"
	CUtlString m_sToolsDesc;

protected:
	// The keys, 72 bytes each
	[[maybe_unused]] char pad0[0x20];
};

static_assert(sizeof(CGameEventDescriptor) == 88, "CGameEventDescriptor - incorrect size on this compiler");

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
// class CGameEventManager: public IGameEventManager2, public IBaseInterface
class CGameEventManager : public IGameEventManager2
{
public:
	enum
	{
		SERVERSIDE = 0, // AddListener with bServerSide
		CLIENTSIDE, // AddListener without bServerSide
		CLIENTSTUB, // a client's listen bits, see CSource1LegacyGameEventGameSystem
	};

	int GetEventCount() const { return m_GameEvents.Count(); }

	// Event ids are indices into m_GameEvents
	CGameEventDescriptor* GetEventDescriptor(int nEventId);
	// By name, from the table's names; nullptr when the game has no such event
	CGameEventDescriptor* GetEventDescriptor(const char* pszName);
	const char* GetEventName(const CGameEventDescriptor* pDescriptor) const;

protected:
	CUtlVector<CGameEventDescriptor> m_GameEvents;
	CUtlVector<CGameEventCallback*> m_Listeners;

	// The loaded .gameevents files
	[[maybe_unused]] char pad0[0x80];

	// name -> index in m_GameEvents
	struct EventMapEntry_t
	{
		[[maybe_unused]] char pad0[0x10];
		const char* m_pszName;
		int m_nIndex;
	};
	static_assert(sizeof(EventMapEntry_t) == 32, "CGameEventManager::EventMapEntry_t - incorrect size on this compiler");

	struct EventMap_t
	{
		[[maybe_unused]] char pad0[0xC];
		uint32 m_nAllocationCount;
		EventMapEntry_t* m_pElements;
		[[maybe_unused]] char pad1[0x10];
	};
	EventMap_t m_EventMap;

	CThreadFastMutex m_Mutex;
};

#endif // GAMEEVENTMANAGER_H
