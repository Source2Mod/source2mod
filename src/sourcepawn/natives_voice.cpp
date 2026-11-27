/*
 * SourcePawn natives for voice chat: Source2ModVoice (source2mod/voice.inc), on
 * ISource2ModVoice. What a script sets is that script's, like a plugin's.
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/game/voice.h"

#include <engine2/ivengineserver2.h>
#include <interfaces/interfaces.h>

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

        cell_t N_SetVoiceFlags(IPluginContext* ctx, const cell_t* params)
        {
            voice::voiceManager.SetVoiceFlags(CallerId(ctx), CPlayerSlot(params[1]), params[2]);
            return 0;
        }

        cell_t N_GetVoiceFlags(IPluginContext*, const cell_t* params)
        {
            return voice::voiceManager.GetVoiceFlags(CPlayerSlot(params[1]));
        }

        cell_t N_SetListenOverride(IPluginContext* ctx, const cell_t* params)
        {
            if (params[3] < 0 || params[3] > static_cast<cell_t>(Source2ModListen::Hear))
                return ctx->ThrowNativeError("Invalid listen override %d", params[3]);
            voice::voiceManager.SetListenOverride(CallerId(ctx), CPlayerSlot(params[1]), CPlayerSlot(params[2]), static_cast<Source2ModListen>(params[3]));
            return 0;
        }

        // SetClientProximity(receiver, sender, useProximity): whether the
        // receiver hears the sender only within proximity range, instead of
        // over the whole map. The engine's own per-client bit; not merged
        // between plugins like the listen overrides.
        cell_t N_SetClientProximity(IPluginContext* ctx, const cell_t* params)
        {
            if (!g_pEngineServer)
                return 0;
            return g_pEngineServer->SetClientProximity(CPlayerSlot(params[1]), CPlayerSlot(params[2]), params[3] != 0);
        }

        cell_t N_GetListenOverride(IPluginContext*, const cell_t* params)
        {
            return static_cast<cell_t>(voice::voiceManager.GetListenOverride(CPlayerSlot(params[1]), CPlayerSlot(params[2])));
        }

        cell_t N_IsSelfMuted(IPluginContext*, const cell_t* params)
        {
            return voice::voiceManager.IsSelfMuted(CPlayerSlot(params[1]), CPlayerSlot(params[2]));
        }

        const sp_nativeinfo_t g_voiceNatives[] = {
            { "Source2ModVoice.SetVoiceFlags", N_SetVoiceFlags },
            { "Source2ModVoice.GetVoiceFlags", N_GetVoiceFlags },
            { "Source2ModVoice.SetListenOverride", N_SetListenOverride },
            { "Source2ModVoice.GetListenOverride", N_GetListenOverride },
            { "Source2ModVoice.IsSelfMuted", N_IsSelfMuted },
            { "Source2ModVoice.SetClientProximity", N_SetClientProximity },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetVoiceNatives()
    {
        return g_voiceNatives;
    }
}
