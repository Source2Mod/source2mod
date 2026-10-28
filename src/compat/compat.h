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
#pragma once

#include <cstddef>

// Older revisions of the source2mod interfaces. A plugin built against
// "ISource2ModCommands004" calls the vtable 004 had; when 005 inserted,
// moved or changed a method, that vtable is gone from the current class.
// Each such revision is frozen into src/compat/<ISource2ModX00N>.h, and a
// generated adapter (src/compat/gen/) implements it by forwarding every
// method to the current interface -- by name and signature, so nothing is
// written twice. Methods whose signature changed are written by hand in
// src/compat/<ISource2ModX00N>.cpp. tools/ifacecompat/ifacecompat.py does
// the freezing and the generating; its header says how.
namespace compat
{
    /// The current revision of an interface, by its version string ("ISource2ModCommands005").
    using CurrentInterfaceFn = void* (*)(const char* iface);

    struct FrozenInterface
    {
        const char* iface;                          // "ISource2ModCommands004"
        void* (*get)(CurrentInterfaceFn current);   // the adapter, null while the current one is missing
    };

    /// Every frozen revision the core serves (src/compat/gen/registry.gen.cpp).
    const FrozenInterface* FrozenInterfaces(size_t& count);
}
