/*
 * SourcePawn natives for the server console's output (Source2ModEngine in
 * source2mod/engine.inc): the engine's logging listener, and a command run
 * right now with its output captured.
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/runtime/scheduler.h"

#include <interfaces/interfaces.h>
#include <sp_callable.h>
#include <tier0/logginglisteners.h>
#include <tier0/logging.h>
#include <tier0/convar.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        struct ConsoleListener
        {
            int id;
            PluginId owner;
            IPluginFunction* fn;
        };

        struct Line
        {
            std::string channel;
            int severity;
            std::string message;
        };

        std::vector<ConsoleListener> g_listeners;
        int g_nextId = 1;
        const std::thread::id g_mainThread = std::this_thread::get_id();

        // Other threads print too: their lines wait for the game thread.
        std::mutex g_queueLock;
        std::vector<Line> g_queue;
        bool g_flushQueued = false;

        std::string* g_capture = nullptr;
        size_t g_captureLimit = 0;
        thread_local bool g_inListener = false;

        void Deliver(const char* channel, int severity, const char* message)
        {
            g_inListener = true;
            const std::vector<ConsoleListener> listeners = g_listeners;
            for (const ConsoleListener& l : listeners)
            {
                sp::CallArgs args;
                args.PushString(channel);
                args.PushCell(severity);
                args.PushString(message);
                Invoke(l.fn, args);
            }
            g_inListener = false;
        }

        void Flush()
        {
            std::vector<Line> lines;
            {
                std::lock_guard<std::mutex> guard(g_queueLock);
                lines.swap(g_queue);
                g_flushQueued = false;
            }
            for (const Line& line : lines)
                Deliver(line.channel.c_str(), line.severity, line.message.c_str());
        }

        class Listener final : public ILoggingListener
        {
        public:
            void Log(const LoggingContext_t* context, const tchar* message) override
            {
                if (!message || g_inListener)
                    return;

                const bool main = std::this_thread::get_id() == g_mainThread;
                if (main && g_capture && g_capture->size() < g_captureLimit)
                    g_capture->append(message, std::min(std::strlen(message), g_captureLimit - g_capture->size()));

                if (g_listeners.empty())
                    return;

                const CLoggingSystem::LoggingChannel_t* info = LoggingSystem_GetChannel(context->m_ChannelID);
                const char* channel = info ? info->m_Name : nullptr;
                if (main)
                {
                    Flush();
                    Deliver(channel ? channel : "", context->m_Severity, message);
                    return;
                }

                std::lock_guard<std::mutex> guard(g_queueLock);
                if (g_queue.size() < 1024)
                    g_queue.push_back({ channel ? channel : "", context->m_Severity, message });
            }
        };

        Listener g_listener;
        bool g_registered = false;

        // Late: changing log_flags with one registered at load is known to crash.
        void EnsureRegistered()
        {
            if (g_registered)
                return;
            g_registered = true;
            LoggingSystem_RegisterBackdoorLoggingListener(&g_listener);
            LoggingSystem_EnableBackdoorLoggingListeners(true);
        }

        // AddConsoleListener(callback)
        cell_t N_AddConsoleListener(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[1]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[1]);
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            EnsureRegistered();
            const int id = g_nextId++;
            g_listeners.push_back({ id, plugin ? plugin->Id() : 0, fn });
            return id;
        }

        cell_t N_RemoveConsoleListener(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            const PluginId owner = plugin ? plugin->Id() : 0;
            std::erase_if(g_listeners, [&](const ConsoleListener& l) { return l.id == params[1] && l.owner == owner; });
            return 0;
        }

        // ServerCommandEx(output, maxlen, format, ...): run now on the server's own console, unlike ServerCommand.
        cell_t N_ServerCommandEx(IPluginContext* ctx, const cell_t* params)
        {
            char line[1024];
            if (!FormatParams(ctx, params, 3, line, sizeof(line)))
                return 0;

            CCommand args;
            args.Tokenize(line);
            size_t written = 0;
            if (args.ArgC() < 1)
            {
                ctx->StringToLocalUTF8(params[1], params[2], "", &written);
                return 0;
            }

            const ConCommandRef command(args[0], true);
            if (!command.IsValidRef())
                return ctx->ThrowNativeError("No such console command: \"%s\"", args[0]);

            EnsureRegistered();

            // Some commands listen only to the server's own slot.
            CCommandContext context(CT_FIRST_SPLITSCREEN_CLIENT, -1);
            std::string output;
            std::string* outer = g_capture;
            const size_t outerLimit = g_captureLimit;
            g_capture = &output;
            g_captureLimit = static_cast<size_t>(params[2] > 0 ? params[2] : 0);
            g_pCVar->DispatchConCommand(command, context, args);
            g_capture = outer;
            g_captureLimit = outerLimit;

            ctx->StringToLocalUTF8(params[1], params[2], output.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        const sp_nativeinfo_t g_consoleNatives[] = {
            { "Source2ModEngine.AddConsoleListener", N_AddConsoleListener },
            { "Source2ModEngine.RemoveConsoleListener", N_RemoveConsoleListener },
            { "Source2ModEngine.ServerCommandEx", N_ServerCommandEx },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetConsoleNatives()
    {
        return g_consoleNatives;
    }

    void ForgetConsoleListeners(PluginId id)
    {
        std::erase_if(g_listeners, [id](const ConsoleListener& l) { return l.owner == id; });
    }

    void ShutdownConsoleListeners()
    {
        if (g_registered)
            LoggingSystem_UnregisterLoggingListener(&g_listener);
        g_registered = false;
        g_listeners.clear();
    }
}
