/*
 * SourcePawn natives for Source2ModPatch (source2mod/gameconfig.inc): bytes
 * written over code, restored when the handle or the script goes. Two patches
 * of one range are refused, whichever script owns them.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        struct Patch : HandleObject
        {
            uint8_t* address = nullptr;
            std::vector<uint8_t> bytes;
            std::vector<uint8_t> saved;
            bool enabled = false;
        };

        std::vector<Patch*> g_enabled;

        bool WriteCode(uint8_t* address, const uint8_t* data, size_t size)
        {
#ifdef _WIN32
            DWORD old = 0;
            if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old))
                return false;
            std::memcpy(address, data, size);
            VirtualProtect(address, size, old, &old);
            FlushInstructionCache(GetCurrentProcess(), address, size);
#else
            const uintptr_t pageSize = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
            const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~(pageSize - 1);
            const size_t length = reinterpret_cast<uintptr_t>(address) + size - start;
            if (mprotect(reinterpret_cast<void*>(start), length, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
                return false;
            std::memcpy(address, data, size);
            mprotect(reinterpret_cast<void*>(start), length, PROT_READ | PROT_EXEC);
#endif
            return true;
        }

        void Disable(Patch* patch)
        {
            if (!patch->enabled)
                return;
            WriteCode(patch->address, patch->saved.data(), patch->saved.size());
            patch->enabled = false;
            std::erase(g_enabled, patch);
        }

        HandleType Type()
        {
            static const HandleType t = Handles().CreateType("Source2ModPatch", [](void* p)
            {
                auto* patch = static_cast<Patch*>(p);
                Disable(patch);
                delete patch;
            });
            return t;
        }

        Patch* Of(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Patch*>(ReadHandle(ctx, handle, Type()));
        }

        // "90 90 E9": what Source2ModGameConfig.GetPatch gives.
        bool ParseBytes(const char* text, std::vector<uint8_t>& out)
        {
            while (*text)
            {
                if (*text == ' ' || *text == '\\' || *text == 'x')
                {
                    text++;
                    continue;
                }
                char* end = nullptr;
                const char pair[3] = { text[0], text[1], '\0' };
                const unsigned long value = std::strtoul(pair, &end, 16);
                if (end != pair + 2)
                    return false;
                out.push_back(static_cast<uint8_t>(value));
                text += 2;
            }
            return !out.empty();
        }

        // Source2ModPatch(address, bytes)
        cell_t N_New(IPluginContext* ctx, const cell_t* params)
        {
            auto* address = static_cast<uint8_t*>(SchemaObjectPointer(ctx, params[1]));
            if (!address)
                return 0;
            auto* patch = new Patch();
            patch->address = address;
            if (!ParseBytes(LocalString(ctx, params[2]), patch->bytes))
            {
                delete patch;
                return ctx->ThrowNativeError("Not hex bytes: \"%s\"", LocalString(ctx, params[2]));
            }
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            const cell_t handle = Handles().Create(Type(), patch, plugin ? plugin->Id() : 0);
            if (!handle)
                delete patch;
            return handle;
        }

        cell_t N_Enable(IPluginContext* ctx, const cell_t* params)
        {
            Patch* patch = Of(ctx, params[1]);
            if (!patch)
                return 0;
            if (patch->enabled)
                return 1;
            for (const Patch* other : g_enabled)
            {
                if (patch->address < other->address + other->bytes.size() && other->address < patch->address + patch->bytes.size())
                    return 0;
            }
            patch->saved.assign(patch->address, patch->address + patch->bytes.size());
            if (!WriteCode(patch->address, patch->bytes.data(), patch->bytes.size()))
                return 0;
            patch->enabled = true;
            g_enabled.push_back(patch);
            return 1;
        }

        cell_t N_Disable(IPluginContext* ctx, const cell_t* params)
        {
            if (Patch* patch = Of(ctx, params[1]))
                Disable(patch);
            return 0;
        }

        cell_t N_Enabled(IPluginContext* ctx, const cell_t* params)
        {
            Patch* patch = Of(ctx, params[1]);
            return patch && patch->enabled;
        }

        const sp_nativeinfo_t g_natives[] = {
            { "Source2ModPatch.Source2ModPatch", N_New },
            { "Source2ModPatch.Enable", N_Enable },
            { "Source2ModPatch.Disable", N_Disable },
            { "Source2ModPatch.Enabled.get", N_Enabled },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetPatchNatives()
    {
        return g_natives;
    }
}
