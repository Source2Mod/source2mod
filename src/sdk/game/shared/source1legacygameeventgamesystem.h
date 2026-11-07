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
 * Game system that turns each client's legacy game event listen bits into
 * listeners. events.cpp (IsClientListening) finds it by its factory's name
 * and asks it for a client's proxy.
 */

#ifndef SOURCE1LEGACYGAMEEVENTGAMESYSTEM_H
#define SOURCE1LEGACYGAMEEVENTGAMESYSTEM_H
#ifdef _WIN32
#pragma once
#endif

#include <igameevents.h>
#include <igamesystem.h>
#include <playerslot.h>
#include <tier0/utlsignalslot.h>
#include <tier0/utlvector.h>

class CMsgSource1LegacyListenEvents;
template <typename PROTO_TYPE>
class CNetMessagePB;

//-----------------------------------------------------------------------------
// Purpose: On CSource1LegacyListenEvents from a client, ListenBitsReceived removes that client's
//          proxy from every event and adds it back as a CLIENTSTUB listener of each event whose
//          bit is set; the bits are event ids, see CGameEventManager.
//-----------------------------------------------------------------------------
// class CSource1LegacyGameEventGameSystem: public CAutoGameSystem, public IGameSystem, public CUtlSlot
class CSource1LegacyGameEventGameSystem : public CAutoGameSystem, public CUtlSlot
{
public:
	// The listener a client's listen bits register it as
	class CServerSideClient_GameEventLegacyProxy : public IGameEventListener2
	{
	public:
		void FireGameEvent(IGameEvent* event) override;

		CPlayerSlot m_nSlot;
	};

	// The net message handler for CSource1LegacyListenEvents; drops slots past 63.
	// Registered as a message handler delegate, but it leaves the return value unset
	bool ListenBitsReceived(CNetMessagePB<CMsgSource1LegacyListenEvents>* pMsg);

	CServerSideClient_GameEventLegacyProxy* GetClientProxy(CPlayerSlot slot)
	{
		return (slot.Get() >= 0 && slot.Get() < 64) ? &m_ClientProxies[slot.Get()] : nullptr;
	}

protected:
	[[maybe_unused]] char pad0[0x8];

	CServerSideClient_GameEventLegacyProxy m_ClientProxies[64];

	// Whether Init registered its second net message handler, which Shutdown then removes
	bool m_bRegisteredSecondHandler;

	// 120-byte entries, destroyed on Shutdown
	[[maybe_unused]] char pad1[0x18];
};

static_assert(sizeof(CSource1LegacyGameEventGameSystem::CServerSideClient_GameEventLegacyProxy) == 16, "CServerSideClient_GameEventLegacyProxy - incorrect size on this compiler");
// The size the game system factory allocates
static_assert(sizeof(CSource1LegacyGameEventGameSystem) == 1120, "CSource1LegacyGameEventGameSystem - incorrect size on this compiler");

#endif // SOURCE1LEGACYGAMEEVENTGAMESYSTEM_H
