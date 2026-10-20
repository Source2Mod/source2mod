/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (Ë™Â·Ů â—Ź S l y n x â—ŹŮ Â·Ë™)" PĹ™ikryl.
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
 * As a special exception, Michal "Slynx (Ë™Â·Ů â—Ź S l y n x â—ŹŮ Â·Ë™)" PĹ™ikryl
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
 *   - Michal "Slynx (Ë™Â·Ů â—Ź S l y n x â—ŹŮ Â·Ë™)" PĹ™ikryl
 *
 * Project: Source2Mod
 */
/*
 * The JSON value handles of natives_json.cpp that other natives lend too
 * (natives_http.cpp: an HTTP response's JSON body).
 */
#pragma once

#include <source2mod/ISource2ModTypes.h>

#include <sp_vm_api.h>

#include <unordered_map>

class ISource2ModJSONValue;

namespace sourcepawn
{
    /// The value handles taken from one document or response, by the JSON
    /// node they wrap (the core makes a new wrapper on every Get/At).
    struct JsonValues
    {
        PluginId owner = 0;
        std::unordered_map<const void*, cell_t> handles;

        /// The handle for `value`: the same node gives the same handle.
        cell_t For(const ISource2ModJSONValue* value);

        /// Frees every handle taken; they go with the document or response.
        void FreeAll();
    };
}
