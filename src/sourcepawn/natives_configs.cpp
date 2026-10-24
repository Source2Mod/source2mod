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
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */

/*
 * SourcePawn natives for source2mod/configs.inc on the core's configs
 * (core/configs.cpp). A Source2ModConfig handle is the script's config; the
 * JSON values Root() / Find() give out are lent handles of the current
 * document, freed whenever a new one goes live (a reload, a Set*), so a
 * script never reads a document the core already let go of.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"
#include "natives_json.h"

#include "core/configs.h"
#include "core/pluginmanager.h"

#include <sp_callable.h>

#include <string>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            if (maxlen <= 0)
                return 0;
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        /// An unset function argument: INVALID_FUNCTION compiles to 0 with this spcomp, -1 with others.
        bool IsUnset(cell_t id)
        {
            return id == -1 || id == 0;
        }

        ISource2ModPlugin* PluginOf(PluginId id)
        {
            for (auto& p : pluginManager.m_plugins)
            {
                if (p && p->id == id)
                    return p->api;
            }
            return nullptr;
        }

        /// The script's config: the core's handle, its callback, and the
        /// value handles lent out of the current document.
        struct Config final : HandleObject, ISource2ModConfigListener
        {
            ISource2ModConfig* config = nullptr;
            PluginId owner = 0;
            std::string name;
            IPluginFunction* callback = nullptr;
            cell_t handle = 0;
            JsonValues values;

            ~Config() override
            {
                values.FreeAll();
                // Gone already when the plugin unloaded: the core drops its
                // configs before its handles. Looked up by owner and name
                // rather than trusting the pointer, which may be reused by then.
                if (config && configs::configsManager.Get(owner, name.c_str()) == config)
                    configs::configsManager.Unregister(config);
            }

            void OnConfigChanged(ISource2ModConfig* changed, ConfigChange reason) override
            {
                config = changed;
                values.FreeAll();
                if (!callback || !handle)
                    return;
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(static_cast<cell_t>(reason));
                Invoke(callback, args);
            }
        };

        HandleType ConfigType()
        {
            static const HandleType t = Handles().CreateType("Source2ModConfig", [](void* p) { delete static_cast<Config*>(p); });
            return t;
        }

        Config* ConfigOf(IPluginContext* ctx, cell_t handle)
        {
            auto* c = static_cast<Config*>(ReadHandle(ctx, handle, ConfigType()));
            return c && c->config ? c : nullptr;
        }

        // Register(name, defaults, callback, flags)
        cell_t N_ConfigRegister(IPluginContext* ctx, const cell_t* params)
        {
            const PluginId owner = CallerId(ctx);
            ISource2ModPlugin* plugin = PluginOf(owner);
            if (!plugin)
                return ctx->ThrowNativeError("Source2ModConfig.Register: the calling plugin is not loaded");

            IPluginFunction* fn = nullptr;
            if (!IsUnset(params[3]))
            {
                fn = ctx->GetFunctionById(static_cast<funcid_t>(params[3]));
                if (!fn)
                    return ctx->ThrowNativeError("Invalid function id (%X)", params[3]);
            }

            auto* c = new Config();
            c->owner = owner;
            c->name = LocalString(ctx, params[1]);
            c->callback = fn;
            c->values.owner = owner;
            c->handle = Handles().Create(ConfigType(), c, owner);
            if (!c->handle)
            {
                delete c;
                return 0;
            }

            char error[512] = "";
            ISource2ModConfig* config = configs::configsManager.Register(plugin, LocalString(ctx, params[1]), LocalString(ctx, params[2]),
                                                                         static_cast<uint32_t>(params[4]), c, error, sizeof(error));
            if (!config)
            {
                Handles().Free(c->handle, 0, true);
                return ctx->ThrowNativeError("Source2ModConfig.Register: %s", error);
            }

            c->config = config;
            return c->handle;
        }

        cell_t N_ConfigRoot(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            return c ? c->values.For(c->config->Root()) : 0;
        }

        cell_t N_ConfigDefaults(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            return c ? c->values.For(c->config->Defaults()) : 0;
        }

        cell_t N_ConfigFind(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            return c ? c->values.For(c->config->Find(LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_ConfigGetName(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            return c ? ToBuffer(ctx, params[2], params[3], c->config->GetName()) : 0;
        }

        cell_t N_ConfigGetPath(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            return c ? ToBuffer(ctx, params[2], params[3], c->config->GetPath()) : 0;
        }

        cell_t N_ConfigGetLastError(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            return c ? ToBuffer(ctx, params[2], params[3], c->config->GetLastError()) : 0;
        }

        cell_t N_ConfigGeneration(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            return c ? static_cast<cell_t>(c->config->GetGeneration()) : 0;
        }

        // A Set* makes a new document: the lent values of the old one go.
        template <typename Fn>
        cell_t Set(IPluginContext* ctx, const cell_t* params, Fn&& set)
        {
            Config* c = ConfigOf(ctx, params[1]);
            if (!c)
                return 0;
            const bool ok = set(c->config, LocalString(ctx, params[2]));
            if (ok)
                c->values.FreeAll();
            return ok;
        }

        cell_t N_ConfigSetString(IPluginContext* ctx, const cell_t* params)
        {
            return Set(ctx, params, [&](ISource2ModConfig* c, const char* p) { return c->SetString(p, LocalString(ctx, params[3])); });
        }

        cell_t N_ConfigSetInt(IPluginContext* ctx, const cell_t* params)
        {
            return Set(ctx, params, [&](ISource2ModConfig* c, const char* p) { return c->SetInt64(p, params[3]); });
        }

        cell_t N_ConfigSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            return Set(ctx, params, [&](ISource2ModConfig* c, const char* p) { return c->SetDouble(p, sp_ctof(params[3])); });
        }

        cell_t N_ConfigSetBool(IPluginContext* ctx, const cell_t* params)
        {
            return Set(ctx, params, [&](ISource2ModConfig* c, const char* p) { return c->SetBool(p, params[3] != 0); });
        }

        cell_t N_ConfigSetJSON(IPluginContext* ctx, const cell_t* params)
        {
            return Set(ctx, params, [&](ISource2ModConfig* c, const char* p) { return c->SetJSON(p, LocalString(ctx, params[3])); });
        }

        cell_t N_ConfigRemove(IPluginContext* ctx, const cell_t* params)
        {
            return Set(ctx, params, [](ISource2ModConfig* c, const char* p) { return c->Remove(p); });
        }

        // Save(error, maxlen)
        cell_t N_ConfigSave(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            if (!c)
                return 0;
            char error[512] = "";
            const bool ok = c->config->Save(error, sizeof(error));
            if (!ok)
                ToBuffer(ctx, params[2], params[3], error);
            return ok;
        }

        // Reload(error, maxlen); OnConfigChanged frees the lent values.
        cell_t N_ConfigReload(IPluginContext* ctx, const cell_t* params)
        {
            Config* c = ConfigOf(ctx, params[1]);
            if (!c)
                return 0;
            char error[512] = "";
            const bool ok = c->config->Reload(error, sizeof(error));
            if (!ok)
                ToBuffer(ctx, params[2], params[3], error);
            return ok;
        }

        const sp_nativeinfo_t g_configNatives[] = {
            { "Source2ModConfig.Register", N_ConfigRegister },
            { "Source2ModConfig.Root", N_ConfigRoot },
            { "Source2ModConfig.Defaults", N_ConfigDefaults },
            { "Source2ModConfig.Find", N_ConfigFind },
            { "Source2ModConfig.GetName", N_ConfigGetName },
            { "Source2ModConfig.GetPath", N_ConfigGetPath },
            { "Source2ModConfig.GetLastError", N_ConfigGetLastError },
            { "Source2ModConfig.Generation.get", N_ConfigGeneration },
            { "Source2ModConfig.SetString", N_ConfigSetString },
            { "Source2ModConfig.SetInt", N_ConfigSetInt },
            { "Source2ModConfig.SetFloat", N_ConfigSetFloat },
            { "Source2ModConfig.SetBool", N_ConfigSetBool },
            { "Source2ModConfig.SetJSON", N_ConfigSetJSON },
            { "Source2ModConfig.Remove", N_ConfigRemove },
            { "Source2ModConfig.Save", N_ConfigSave },
            { "Source2ModConfig.Reload", N_ConfigReload },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetConfigNatives()
    {
        return g_configNatives;
    }
}
