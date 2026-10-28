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
 * SourcePawn natives for source2mod/gameconfig.inc: Source2ModGameConfig on
 * the core's gamedata (core/gameconfig.cpp).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gameconfig.h"
#include "core/module.h"
#include "core/shared.h"

#include <source2mod/sdk/schema/classes/CBaseEntity.h>

#include <tier0/buffer_string.h>

#include <khook.hpp>
#include <sp_callable.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {

        // enum Source2ModPlatform (gameconfig.inc)
        constexpr cell_t kPlatformLinux = 0;
        constexpr cell_t kPlatformWindows = 1;

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        // An optional string argument at params[n]: "" when the script was
        // compiled against an include without it.
        const char* OptionalString(IPluginContext* ctx, const cell_t* params, int n)
        {
            return params[0] >= n ? LocalString(ctx, params[n]) : "";
        }

        // A signature in a module, optionally only in one section (".text").
        uintptr_t FindSignature(const char* moduleName, const char* signature, const char* section)
        {
            Source2ModModule module(moduleName);
            if (!section || !*section)
                return module.FindPattern(signature).GetAddr();
            if (!module.GetSectionByName(section).IsValid())
                return 0;
            return module.FindPatternInSection(signature, section).GetAddr();
        }

        // ---- Source2ModGameConfig ------------------------------------------------------

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            if (!value)
                return 0;
            ctx->StringToLocalUTF8(addr, maxlen, value, nullptr);
            return 1;
        }

        cell_t N_GCGetLibrary(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetLibrary(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetSignature(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetSignature(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetSymbol(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetSymbol(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetPatch(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], gameconfig::gameConfig.GetPatch(LocalString(ctx, params[1])));
        }

        cell_t N_GCGetOffset(IPluginContext* ctx, const cell_t* params)
        {
            return gameconfig::gameConfig.GetOffset(LocalString(ctx, params[1]));
        }

        cell_t N_GCIsSymbol(IPluginContext* ctx, const cell_t* params)
        {
            return gameconfig::gameConfig.IsSymbol(LocalString(ctx, params[1]));
        }

        cell_t N_GCGetPlatform(IPluginContext*, const cell_t*)
        {
#if defined(_WIN32)
            return kPlatformWindows;
#else
            return kPlatformLinux;
#endif
        }

        // Resolves(const char[] name): the entry's signature or symbol is found.
        cell_t N_GCResolves(IPluginContext* ctx, const cell_t* params)
        {
            return gameconfig::gameConfig.ResolveSignature(LocalString(ctx, params[1])).GetAddr() != 0;
        }

        // SignatureMatches(const char[] module, const char[] signature, const char[] section = "")
        cell_t N_GCSignatureMatches(IPluginContext* ctx, const cell_t* params)
        {
            return FindSignature(LocalString(ctx, params[1]), LocalString(ctx, params[2]), OptionalString(ctx, params, 3)) != 0;
        }

        // ResolveSignature(const char[] name): the entry's address, as a SchemaObject; 0 when it does not resolve.
        cell_t N_GCResolveSignature(IPluginContext* ctx, const cell_t* params)
        {
            return SchemaObjectForPointer(gameconfig::gameConfig.ResolveSignature(LocalString(ctx, params[1])).GetPtr());
        }

        // ---- Source2ModModule: a module of the game (ISource2ModModule) ---------------
        // Addresses come back as SchemaObjects, so Source2ModSchema's *At
        // natives read and write at them and Source2ModMemory steps from them.

        HandleType ModuleType()
        {
            static const HandleType t = Handles().CreateType("Source2ModModule", [](void* p) { delete static_cast<Source2ModModule*>(p); });
            return t;
        }

        Source2ModModule* ModuleOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Source2ModModule*>(ReadHandle(ctx, handle, ModuleType()));
        }

        cell_t Address(const ISource2ModMemory& memory)
        {
            return SchemaObjectForPointer(memory.GetPtr());
        }

        // Source2ModModule(name): null when no such module is loaded.
        cell_t N_ModCreate(IPluginContext* ctx, const cell_t* params)
        {
            auto* module = new Source2ModModule(LocalString(ctx, params[1]));
            if (!module->GetModuleBase())
            {
                delete module;
                return 0;
            }
            const cell_t handle = Handles().Create(ModuleType(), module, CallerId(ctx));
            if (!handle)
                delete module;
            return handle;
        }

        // FindPattern(pattern, section = "")
        cell_t N_ModFindPattern(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            if (!module)
                return 0;
            const char* pattern = LocalString(ctx, params[2]);
            const char* section = OptionalString(ctx, params, 3);
            if (!*section)
                return Address(module->FindPattern(pattern));
            if (!module->GetSectionByName(section).IsValid())
                return 0;
            return Address(module->FindPatternInSection(pattern, section));
        }

        // GetFunctionByName(symbol)
        cell_t N_ModGetFunctionByName(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            return module ? Address(module->GetFunctionByName(LocalString(ctx, params[2]))) : 0;
        }

        // GetVirtualTable(className, decorated = false)
        cell_t N_ModGetVirtualTable(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            return module ? Address(module->GetVirtualTableByName(LocalString(ctx, params[2]), params[3] != 0)) : 0;
        }

        // GetVirtualTableByBase(className, baseName, decorated = false)
        cell_t N_ModGetVirtualTableByBase(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            return module ? Address(module->GetVirtualTableByBase(LocalString(ctx, params[2]), LocalString(ctx, params[3]), params[4] != 0)) : 0;
        }

        // GetVirtualTableByOffset(className, offset, decorated = false)
        cell_t N_ModGetVirtualTableByOffset(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            return module ? Address(module->GetVirtualTableByOffset(LocalString(ctx, params[2]), params[3], params[4] != 0)) : 0;
        }

        cell_t N_ModGetBase(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            return module ? Address(module->GetModuleBase()) : 0;
        }

        // GetName(buffer, maxlen) / GetPath(buffer, maxlen)
        template <const char* (Source2ModModule::*Get)() const>
        cell_t N_ModText(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            const char* text = module ? (module->*Get)() : nullptr;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], text ? text : "", &written);
            return static_cast<cell_t>(written);
        }

        // GetSection(name, &start, &size): false when the module has no such section.
        cell_t N_ModGetSection(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule* module = ModuleOf(ctx, params[1]);
            if (!module)
                return 0;
            const auto section = module->GetSectionByName(LocalString(ctx, params[2]));
            if (!section.IsValid())
                return 0;
            cell_t* start = nullptr;
            cell_t* size = nullptr;
            if (ctx->LocalToPhysAddr(params[3], &start) == SP_ERROR_NONE && start)
                *start = SchemaObjectForPointer(reinterpret_cast<void*>(section.base));
            if (ctx->LocalToPhysAddr(params[4], &size) == SP_ERROR_NONE && size)
                *size = static_cast<cell_t>(section.size);
            return 1;
        }

        // ---- Source2ModMemory: stepping from an address (ISource2ModMemory) ----------

        ISource2ModMemory MemoryOf(IPluginContext* ctx, cell_t object)
        {
            if (object == 0)
            {
                ctx->ThrowNativeError("Null address");
                return ISource2ModMemory();
            }
            return ISource2ModMemory(SchemaObjectPointer(ctx, object));
        }

        // Offset(object, offset)
        cell_t N_MemOffset(IPluginContext* ctx, const cell_t* params)
        {
            const ISource2ModMemory memory = MemoryOf(ctx, params[1]);
            return memory ? Address(memory.Offset(params[2])) : 0;
        }

        // Deref(object, times = 1): the pointer stored there; 0 when it is null.
        cell_t N_MemDeref(IPluginContext* ctx, const cell_t* params)
        {
            const ISource2ModMemory memory = MemoryOf(ctx, params[1]);
            return memory ? Address(memory.Deref(params[2])) : 0;
        }

        // FollowNearCall(object, opcodeOffset = 1, nextInstructionOffset = 5)
        cell_t N_MemFollowNearCall(IPluginContext* ctx, const cell_t* params)
        {
            const ISource2ModMemory memory = MemoryOf(ctx, params[1]);
            return memory ? Address(memory.FollowNearCall(params[2], params[3])) : 0;
        }

        // ResolveRelativeAddress(object, registerOffset = 0, nextInstructionOffset = 4)
        cell_t N_MemResolveRelativeAddress(IPluginContext* ctx, const cell_t* params)
        {
            const ISource2ModMemory memory = MemoryOf(ctx, params[1]);
            return memory ? Address(memory.ResolveRelativeAddress(params[2], params[3])) : 0;
        }

        // Format(object, buffer, maxlen): "0x7f1234..." for a log line; "0x0" for null.
        cell_t N_MemFormat(IPluginContext* ctx, const cell_t* params)
        {
            const uintptr_t address = params[1] ? reinterpret_cast<uintptr_t>(SchemaObjectPointer(ctx, params[1])) : 0;
            char text[32];
            std::snprintf(text, sizeof(text), "0x%" PRIxPTR, address);
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], text, &written);
            return static_cast<cell_t>(written);
        }

        // Distance(from, to): to - from in bytes, for offsets inside a module.
        cell_t N_MemDistance(IPluginContext* ctx, const cell_t* params)
        {
            const ISource2ModMemory from = MemoryOf(ctx, params[1]);
            const ISource2ModMemory to = MemoryOf(ctx, params[2]);
            return (from && to) ? static_cast<cell_t>(static_cast<intptr_t>(to.GetAddr() - from.GetAddr())) : 0;
        }

        const sp_nativeinfo_t g_gameConfigNatives[] = {
            { "Source2ModGameConfig.ResolveSignature", N_GCResolveSignature },
            { "Source2ModModule.Source2ModModule", N_ModCreate },
            { "Source2ModModule.FindPattern", N_ModFindPattern },
            { "Source2ModModule.GetFunctionByName", N_ModGetFunctionByName },
            { "Source2ModModule.GetVirtualTable", N_ModGetVirtualTable },
            { "Source2ModModule.GetVirtualTableByBase", N_ModGetVirtualTableByBase },
            { "Source2ModModule.GetVirtualTableByOffset", N_ModGetVirtualTableByOffset },
            { "Source2ModModule.Base.get", N_ModGetBase },
            { "Source2ModModule.GetName", N_ModText<&Source2ModModule::GetModuleName> },
            { "Source2ModModule.GetPath", N_ModText<&Source2ModModule::GetModulePath> },
            { "Source2ModModule.GetSection", N_ModGetSection },
            { "Source2ModMemory.Offset", N_MemOffset },
            { "Source2ModMemory.Deref", N_MemDeref },
            { "Source2ModMemory.FollowNearCall", N_MemFollowNearCall },
            { "Source2ModMemory.ResolveRelativeAddress", N_MemResolveRelativeAddress },
            { "Source2ModMemory.Format", N_MemFormat },
            { "Source2ModMemory.Distance", N_MemDistance },
            { "Source2ModGameConfig.GetLibrary", N_GCGetLibrary },
            { "Source2ModGameConfig.GetSignature", N_GCGetSignature },
            { "Source2ModGameConfig.GetSymbol", N_GCGetSymbol },
            { "Source2ModGameConfig.GetPatch", N_GCGetPatch },
            { "Source2ModGameConfig.GetOffset", N_GCGetOffset },
            { "Source2ModGameConfig.IsSymbol", N_GCIsSymbol },
            { "Source2ModGameConfig.GetPlatform", N_GCGetPlatform },
            { "Source2ModGameConfig.Resolves", N_GCResolves },
            { "Source2ModGameConfig.SignatureMatches", N_GCSignatureMatches },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetGameConfigNatives()
    {
        return g_gameConfigNatives;
    }
}
