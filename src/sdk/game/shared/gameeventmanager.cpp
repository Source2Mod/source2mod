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

#include "gameeventmanager.h"

#include <cstddef>

CGameEventDescriptor* CGameEventManager::GetEventDescriptor(int nEventId)
{
	static_assert(offsetof(CGameEventManager, m_GameEvents) == 8, "CGameEventManager::m_GameEvents - not at 8 on this compiler");
	static_assert(offsetof(CGameEventManager, m_EventMap) == 184, "CGameEventManager::m_EventMap - not at 184 on this compiler");
	static_assert(offsetof(CGameEventManager, m_Mutex) == 224, "CGameEventManager::m_Mutex - not at 224 on this compiler");

	if (nEventId < 0 || nEventId >= m_GameEvents.Count())
		return nullptr;

	return &m_GameEvents[nEventId];
}

CGameEventDescriptor* CGameEventManager::GetEventDescriptor(const char* pszName)
{
	if (!pszName || !*pszName)
		return nullptr;

	// Through the table's own names rather than LookupEventId: what the
	// descriptors hold is what the manager acts on
	for (int i = 0; i < m_GameEvents.Count(); i++)
	{
		const char* pszEventName = GetEventName(&m_GameEvents[i]);
		if (pszEventName && !V_strcmp(pszEventName, pszName))
			return &m_GameEvents[i];
	}

	return nullptr;
}

const char* CGameEventManager::GetEventName(const CGameEventDescriptor* pDescriptor) const
{
	if (!pDescriptor || pDescriptor->m_nEventMapIndex < 0 || !(m_EventMap.m_nAllocationCount & 0x7FFFFFFF))
		return nullptr;

	return m_EventMap.m_pElements[pDescriptor->m_nEventMapIndex].m_pszName;
}
