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

/*
 * What a plugin gets as KHook::__exported__khook: metamod's engine behind a
 * wrapper of its own, as metamod does for its plugins, so that whatever hooks
 * the plugin leaves behind come out before its library closes. Metamod knows
 * Source2Mod only as one plugin, and does not track inline hooks at all.
 */
#pragma once

#include <source2mod/ISource2ModTypes.h>

#include <khook.hpp>

namespace pluginkhook
{
    /// The plugin's engine; @p owner 0, the core, gets metamod's own.
    KHook::IKHook* ForPlugin(PluginId owner);

    /// An async removal the plugin asked for has not finished: its callbacks
    /// still run in the plugin's library, so that stays open.
    bool HasPendingRemovals(PluginId owner);

    /// For the closes that cannot wait for the next frame.
    void WaitForRemovals(PluginId owner);
}
