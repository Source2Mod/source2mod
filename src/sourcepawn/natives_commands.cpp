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
 * SourcePawn natives for source2mod/commands.inc: Source2ModCommands,
 * Source2ModCommandContext and Source2ModCommandArgs on the core's commands
 * manager (core/commands.cpp). Everything is registered under the calling
 * script's id, so the manager drops it when the script unloads.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/commands.h"

#include <source2mod/ISource2ModScheduler.h>

#include <tier0/convar.h>

#include <sp_callable.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

        // ---- commands -------------------------------------------------------------

        // The context and the arguments of the command being run, as handles
        // the handler gets; freed when it returns.
        HandleType ContextType()
        {
            static const HandleType t = Handles().CreateType("Source2ModCommandContext", nullptr, 0, false);
            return t;
        }

        HandleType ArgsType()
        {
            static const HandleType t = Handles().CreateType("Source2ModCommandArgs", nullptr, 0, false);
            return t;
        }

        struct CommandCall
        {
            cell_t context;
            cell_t args;

            CommandCall(const Source2ModCommandContext& c, const Source2ModCommandArgs& a, PluginId owner)
            {
                context = Handles().Create(ContextType(), const_cast<Source2ModCommandContext*>(&c), owner);
                args = Handles().Create(ArgsType(), const_cast<Source2ModCommandArgs*>(&a), owner);
            }

            ~CommandCall()
            {
                Handles().Free(context, 0, true);
                Handles().Free(args, 0, true);
            }
        };

        const Source2ModCommandContext* ContextOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<const Source2ModCommandContext*>(ReadHandle(ctx, handle, ContextType()));
        }

        const Source2ModCommandArgs* ArgsOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<const Source2ModCommandArgs*>(ReadHandle(ctx, handle, ArgsType()));
        }

        ChatHandler MakeChatHandler(IPluginFunction* fn, PluginId owner)
        {
            return ChatHandler([fn, owner](const Source2ModCommandContext& c, const Source2ModCommandArgs& a, bool post)
            {
                CommandCall call(c, a, owner);
                sp::CallArgs args;
                args.PushCell(call.context);
                args.PushCell(call.args);
                args.PushCell(post ? 1 : 0);
                Invoke(fn, args);
            });
        }

        const char* Permission(IPluginContext* ctx, cell_t addr)
        {
            const char* p = LocalString(ctx, addr);
            return *p ? p : nullptr;
        }

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        // RegisterConCommand(name, Source2ModChatHandler handler, permission = "")
        cell_t N_RegisterConCommand(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return static_cast<cell_t>(commands::commandsManager.RegisterConCommand(owner, LocalString(ctx, params[1]),
                                                                                    MakeChatHandler(fn, owner), Permission(ctx, params[3])));
        }

        cell_t N_RegisterChatListener(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return static_cast<cell_t>(commands::commandsManager.RegisterChatListener(owner, LocalString(ctx, params[1]),
                                                                                      MakeChatHandler(fn, owner), Permission(ctx, params[3])));
        }

        // HookConCommand(name, Source2ModCommandHandler handler, bool post)
        cell_t N_HookConCommand(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            CommandHandler handler([fn, owner](const Source2ModCommandContext& c, const Source2ModCommandArgs& a, bool post) -> Action
            {
                CommandCall call(c, a, owner);
                sp::CallArgs args;
                args.PushCell(call.context);
                args.PushCell(call.args);
                args.PushCell(post ? 1 : 0);
                cell_t res = 0;
                Invoke(fn, args, &res);
                return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
            });
            return static_cast<cell_t>(commands::commandsManager.HookConCommand(owner, LocalString(ctx, params[1]), std::move(handler), params[3] != 0));
        }

        cell_t N_Unregister(IPluginContext*, const cell_t* params)
        {
            return static_cast<ISource2ModCommands&>(commands::commandsManager).UnregisterConCommand(static_cast<Source2ModCommandId>(params[1]));
        }

        // ReplyToCommand(context, format, ...)
        cell_t N_ReplyToCommand(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            char msg[1024];
            if (c && FormatParams(ctx, params, 2, msg, sizeof(msg)))
                commands::commandsManager.ReplyToCommand(*c, msg);
            return 0;
        }

        cell_t N_CtxGetPlayerSlot(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c ? c->GetPlayerSlot().Get() : -1;
        }

        cell_t N_CtxGetCommandTarget(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c ? c->GetCommandTarget() : 0;
        }

        cell_t N_CtxGetSource(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c ? static_cast<cell_t>(c->GetSource()) : 0;
        }

        cell_t N_CtxIsFromChat(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c && c->IsFromChat();
        }

        cell_t N_ArgsArgC(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? a->ArgC() : 0;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_ArgsArg(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? ToBuffer(ctx, params[3], params[4], a->Arg(params[2])) : 0;
        }

        cell_t N_ArgsArgS(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? ToBuffer(ctx, params[2], params[3], a->ArgS()) : 0;
        }

        cell_t N_ArgsGetCommandString(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? ToBuffer(ctx, params[2], params[3], a->GetCommandString()) : 0;
        }

        // ---- Source2ModCommandLine: text through the engine's tokenizer -----------

        // The engine's own CCommand, so a script splits a line exactly as the
        // engine does before it looks a ConCommand up -- it also breaks on
        // { } ( ) ' : ("say:x" is say), which a split on spaces would miss.
        HandleType CommandLineType()
        {
            static const HandleType t = Handles().CreateType("Source2ModCommandLine", [](void* p) { delete static_cast<CCommand*>(p); });
            return t;
        }

        const CCommand* CommandLineOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<const CCommand*>(ReadHandle(ctx, handle, CommandLineType()));
        }

        // Source2ModCommandLine(text)
        cell_t N_LineCreate(IPluginContext* ctx, const cell_t* params)
        {
            auto* command = new CCommand();
            command->Tokenize(LocalString(ctx, params[1]));
            const cell_t handle = Handles().Create(CommandLineType(), command, CallerId(ctx));
            if (!handle)
                delete command;
            return handle;
        }

        cell_t N_LineArgC(IPluginContext* ctx, const cell_t* params)
        {
            const CCommand* c = CommandLineOf(ctx, params[1]);
            return c ? c->ArgC() : 0;
        }

        cell_t N_LineArg(IPluginContext* ctx, const cell_t* params)
        {
            const CCommand* c = CommandLineOf(ctx, params[1]);
            return c ? ToBuffer(ctx, params[3], params[4], c->Arg(params[2])) : 0;
        }

        cell_t N_LineArgS(IPluginContext* ctx, const cell_t* params)
        {
            const CCommand* c = CommandLineOf(ctx, params[1]);
            return c ? ToBuffer(ctx, params[2], params[3], c->ArgS()) : 0;
        }

        cell_t N_LineGetCommandString(IPluginContext* ctx, const cell_t* params)
        {
            const CCommand* c = CommandLineOf(ctx, params[1]);
            return c ? ToBuffer(ctx, params[2], params[3], c->GetCommandString()) : 0;
        }

        // IsCommand(name): Arg(0) is `name`, case-insensitively, as the engine
        // matches a ConCommand ("SAY" runs say).
        cell_t N_LineIsCommand(IPluginContext* ctx, const cell_t* params)
        {
            const CCommand* c = CommandLineOf(ctx, params[1]);
            return c && c->ArgC() > 0 && !V_stricmp(c->Arg(0), LocalString(ctx, params[2]));
        }

        const sp_nativeinfo_t g_commandNatives[] = {
            { "Source2ModCommandLine.Source2ModCommandLine", N_LineCreate },
            { "Source2ModCommandLine.ArgC", N_LineArgC },
            { "Source2ModCommandLine.Arg", N_LineArg },
            { "Source2ModCommandLine.ArgS", N_LineArgS },
            { "Source2ModCommandLine.GetCommandString", N_LineGetCommandString },
            { "Source2ModCommandLine.IsCommand", N_LineIsCommand },
            { "Source2ModCommands.RegisterConCommand", N_RegisterConCommand },
            { "Source2ModCommands.UnregisterConCommand", N_Unregister },
            { "Source2ModCommands.RegisterChatListener", N_RegisterChatListener },
            { "Source2ModCommands.UnregisterChatListener", N_Unregister },
            { "Source2ModCommands.HookConCommand", N_HookConCommand },
            { "Source2ModCommands.UnhookConCommand", N_Unregister },
            { "Source2ModCommands.ReplyToCommand", N_ReplyToCommand },
            { "Source2ModCommandContext.GetPlayerSlot", N_CtxGetPlayerSlot },
            { "Source2ModCommandContext.GetCommandTarget", N_CtxGetCommandTarget },
            { "Source2ModCommandContext.GetSource", N_CtxGetSource },
            { "Source2ModCommandContext.IsFromChat", N_CtxIsFromChat },
            { "Source2ModCommandArgs.ArgC", N_ArgsArgC },
            { "Source2ModCommandArgs.Arg", N_ArgsArg },
            { "Source2ModCommandArgs.ArgS", N_ArgsArgS },
            { "Source2ModCommandArgs.GetCommandString", N_ArgsGetCommandString },
            { nullptr, nullptr },
        };
    }

    cell_t LendCommandArgs(const Source2ModCommandArgs* args, PluginId owner)
    {
        return Handles().Create(ArgsType(), const_cast<Source2ModCommandArgs*>(args), owner);
    }

    cell_t LendCommandContext(const Source2ModCommandContext* context, PluginId owner)
    {
        return Handles().Create(ContextType(), const_cast<Source2ModCommandContext*>(context), owner);
    }

    const sp_nativeinfo_t* GetCommandNatives()
    {
        return g_commandNatives;
    }
}
