#pragma once

#include "core/appsystem/appsystem.h"

#include <source2mod/ISource2ModVoice.h>

#include <cstdint>
#include <map>
#include <memory>

class CCommand;

namespace voice
{
    /// ISource2ModVoice: one hook on SetClientListening, each plugin's flags and
    /// overrides kept apart and combined for it.
    class VoiceManager final : public ISource2ModVoice, public appsystem::ISource2ModAppSystem
    {
    public:
        appsystem::SystemId GetSystemId() const override { return appsystem::SystemId::Voice; }
        bool Init(char*, size_t) override { return true; }
        void Shutdown() override;
        void OnClientDisconnect(CPlayerSlot slot) override;
        void RemoveAllForPlugin(PluginId id) override;

        void SetVoiceFlags(CPlayerSlot slot, int flags) override { SetVoiceFlags(PluginId(0), slot, flags); }
        void SetVoiceFlags(PluginId owner, CPlayerSlot slot, int flags);
        int GetVoiceFlags(CPlayerSlot slot) override;
        void SetListenOverride(CPlayerSlot receiver, CPlayerSlot sender, Source2ModListen listen) override { SetListenOverride(PluginId(0), receiver, sender, listen); }
        void SetListenOverride(PluginId owner, CPlayerSlot receiver, CPlayerSlot sender, Source2ModListen listen);
        Source2ModListen GetListenOverride(CPlayerSlot receiver, CPlayerSlot sender) override;
        bool IsSelfMuted(CPlayerSlot receiver, CPlayerSlot sender) override;

        /// The client's "vban" command: the first 32 slots it muted itself.
        void OnClientCommand(CPlayerSlot slot, const CCommand& args);

    private:
        static constexpr int kSlots = 64;

        struct OwnerState
        {
            int flags[kSlots] = {};
            uint8_t listen[kSlots][kSlots] = {};
        };

        void EnsureHook();
        void Recombine();
        bool Decide(int receiver, int sender, bool& listen) const;

        std::map<PluginId, std::unique_ptr<OwnerState>> m_owners;
        // What the hook reads, once per pair per frame: every owner together.
        int m_flags[kSlots] = {};
        uint8_t m_listen[kSlots][kSlots] = {};
        uint32_t m_selfMutes[kSlots] = {};
        Source2ModHookId m_hook = 0;
    };

    extern VoiceManager voiceManager;
}
