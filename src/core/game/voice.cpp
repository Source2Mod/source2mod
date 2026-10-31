#include "core/game/voice.h"

#include "core/game/functions.h"
#include "core/hooks/hooks.h"
#include "core/runtime/shared.h"
#include "utils/log.h"

#include <engine2/ivengineserver2.h>
#include <khook.hpp>
#include <source2mod/sdk/schema/classes/CCSPlayerController.h>
#include <convar.h>

#include <cstdio>
#include <cstring>

namespace voice
{
    VoiceManager voiceManager;

    namespace
    {
        bool ValidSlot(int slot)
        {
            return slot >= 0 && slot < 64;
        }
    }

    void VoiceManager::EnsureHook()
    {
        if (m_hook || !g_pEngineServer)
            return;

        static const Source2ModType kParams[] = { Source2ModType::Pointer, Source2ModType::Int32, Source2ModType::Int32, Source2ModType::Bool };
        ISource2ModFunction* function = functions::functionsManager.FromVirtual(
            PluginId(0), KHook::GetVtableIndex(&IVEngineServer2::SetClientListening), { Source2ModType::Bool, kParams, 4, true });

        ISource2ModHooks& hooks = hooks::hooksManager;
        m_hook = hooks.Hook(HookPoint::Function(function), HookTarget::Instance(g_pEngineServer), [this](FunctionContext& ctx, bool)
        {
            bool listen = false;
            if (Decide(ctx.GetArg<int>(1), ctx.GetArg<int>(2), listen))
                ctx.SetArg<bool>(3, listen);
            return Action::Ignore;
        });
        if (!m_hook)
            FP_ERROR("Could not hook IVEngineServer2::SetClientListening: voice flags and overrides do nothing");
    }

    bool VoiceManager::Decide(int receiver, int sender, bool& listen) const
    {
        if (!ValidSlot(receiver) || !ValidSlot(sender))
            return false;

        if (sender < 32 && (m_selfMutes[receiver] & (1u << sender)))
        {
            listen = false;
            return true;
        }

        const int senderFlags = m_flags[sender];
        const int receiverFlags = m_flags[receiver];
        if (senderFlags & Source2ModVoice_Muted)
        {
            listen = false;
            return true;
        }

        const auto override_ = static_cast<Source2ModListen>(m_listen[receiver][sender]);
        if (override_ != Source2ModListen::Default)
        {
            listen = override_ == Source2ModListen::Hear;
            return true;
        }

        if ((senderFlags & Source2ModVoice_All) || (receiverFlags & Source2ModVoice_ListenAll))
        {
            listen = true;
            return true;
        }

        if ((senderFlags & Source2ModVoice_Team) || (receiverFlags & Source2ModVoice_ListenTeam))
        {
            CCSPlayerController* receiverController = CCSPlayerController::FromSlot(receiver);
            CCSPlayerController* senderController = CCSPlayerController::FromSlot(sender);
            if (!receiverController || !senderController)
                return false;
            listen = receiverController->m_iTeamNum() == senderController->m_iTeamNum();
            return true;
        }

        return false;
    }

    void VoiceManager::Recombine()
    {
        std::memset(m_flags, 0, sizeof(m_flags));
        std::memset(m_listen, 0, sizeof(m_listen));
        for (const auto& [owner, state] : m_owners)
        {
            for (int receiver = 0; receiver < kSlots; receiver++)
            {
                m_flags[receiver] |= state->flags[receiver];
                for (int sender = 0; sender < kSlots; sender++)
                {
                    const uint8_t listen = state->listen[receiver][sender];
                    // A mute from any plugin wins over a hear from another.
                    if (listen == static_cast<uint8_t>(Source2ModListen::Mute) || (listen && !m_listen[receiver][sender]))
                        m_listen[receiver][sender] = listen;
                }
            }
        }
    }

    void VoiceManager::SetVoiceFlags(PluginId owner, CPlayerSlot slot, int flags)
    {
        if (!ValidSlot(slot.Get()))
            return;
        EnsureHook();

        auto& state = m_owners[owner];
        if (!state)
            state = std::make_unique<OwnerState>();
        state->flags[slot.Get()] = flags;

        int combined = 0;
        for (const auto& [id, other] : m_owners)
            combined |= other->flags[slot.Get()];
        m_flags[slot.Get()] = combined;
    }

    int VoiceManager::GetVoiceFlags(CPlayerSlot slot)
    {
        return ValidSlot(slot.Get()) ? m_flags[slot.Get()] : 0;
    }

    void VoiceManager::SetListenOverride(PluginId owner, CPlayerSlot receiver, CPlayerSlot sender, Source2ModListen listen)
    {
        const int r = receiver.Get();
        const int s = sender.Get();
        if (!ValidSlot(r) || !ValidSlot(s))
            return;
        EnsureHook();

        auto& state = m_owners[owner];
        if (!state)
            state = std::make_unique<OwnerState>();
        state->listen[r][s] = static_cast<uint8_t>(listen);

        uint8_t combined = 0;
        for (const auto& [id, other] : m_owners)
        {
            const uint8_t value = other->listen[r][s];
            if (value == static_cast<uint8_t>(Source2ModListen::Mute) || (value && !combined))
                combined = value;
        }
        m_listen[r][s] = combined;
    }

    Source2ModListen VoiceManager::GetListenOverride(CPlayerSlot receiver, CPlayerSlot sender)
    {
        if (!ValidSlot(receiver.Get()) || !ValidSlot(sender.Get()))
            return Source2ModListen::Default;
        return static_cast<Source2ModListen>(m_listen[receiver.Get()][sender.Get()]);
    }

    bool VoiceManager::IsSelfMuted(CPlayerSlot receiver, CPlayerSlot sender)
    {
        const int s = sender.Get();
        return ValidSlot(receiver.Get()) && s >= 0 && s < 32 && (m_selfMutes[receiver.Get()] & (1u << s));
    }

    void VoiceManager::OnClientCommand(CPlayerSlot slot, const CCommand& args)
    {
        if (!ValidSlot(slot.Get()) || args.ArgC() < 2 || V_stricmp(args.Arg(0), "vban"))
            return;

        EnsureHook();
        unsigned int mask = 0;
        std::sscanf(args.Arg(1), "%x", &mask);
        m_selfMutes[slot.Get()] = mask;
    }

    void VoiceManager::OnClientDisconnect(CPlayerSlot slot)
    {
        const int s = slot.Get();
        if (!ValidSlot(s))
            return;

        m_selfMutes[s] = 0;
        for (const auto& [owner, state] : m_owners)
        {
            state->flags[s] = 0;
            for (int other = 0; other < kSlots; other++)
            {
                state->listen[s][other] = 0;
                state->listen[other][s] = 0;
            }
        }
        Recombine();
    }

    void VoiceManager::RemoveAllForPlugin(PluginId id)
    {
        if (m_owners.erase(id))
            Recombine();
    }

    void VoiceManager::Shutdown()
    {
        if (m_hook)
            static_cast<ISource2ModHooks&>(hooks::hooksManager).Unhook(m_hook);
        m_hook = 0;
        m_owners.clear();
    }
}
