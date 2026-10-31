/*
 * SourcePawn natives for Source2ModMenuInstance (source2mod/menus.inc): a menu
 * whose drawing and keys are the script's own, one IMenuInstance per player.
 *
 * The handle shares its state with the open instances. Closing the handle marks
 * the state dead and closes them on the next frame: the close may come from one
 * of their own callbacks, and the core still reads the instance after that.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/ui/menus.h"
#include "core/runtime/scheduler.h"

#include <source2mod/sdk/schema/classes/CCSPlayerController.h>

#include <sp_callable.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        class ScriptInstance;

        // How long the client shows one send of the panel, and when an unchanged one goes again.
        constexpr int kPanelSeconds = 30;
        constexpr float kRefreshAfter = kPanelSeconds - 5.0f;

        struct State
        {
            cell_t handle = 0;
            PluginId owner = 0;
            bool dead = false;
            IPluginFunction* onDisplay = nullptr;
            IPluginFunction* onKey = nullptr;
            IPluginFunction* onClose = nullptr;
            IPluginFunction* onReset = nullptr;
            cell_t data = 0;
            uint32_t keyMask = 0x3FEu;
            bool acceptsKeys = true;
            float refreshInterval = 0.0f;
            int timeout = 0;
            std::unordered_map<int, ScriptInstance*> open;
        };

        struct MenuHandle : HandleObject
        {
            std::shared_ptr<State> state;
        };

        float Now()
        {
            return Plat_FloatTime();
        }

        class ScriptInstance final : public IMenuInstance
        {
        public:
            ScriptInstance(CCSPlayerController* player, std::shared_ptr<State> state) : IMenuInstance(player, nullptr), state_(std::move(state)), slot_(player->GetSlot()), openedAt_(Now())
            {
                state_->open[slot_] = this;
            }

            ~ScriptInstance() override
            {
                auto it = state_->open.find(slot_);
                if (it != state_->open.end() && it->second == this)
                    state_->open.erase(it);
            }

            void Display() override
            {
                if (state_->dead || !state_->onDisplay)
                    return;
                lastDraw_ = Now();
                sp::CallArgs args;
                args.PushCell(state_->handle);
                args.PushCell(EntityCell(player_));
                args.PushCell(state_->data);
                Invoke(state_->onDisplay, args);
            }

            void OnKeyPress(CCSPlayerController* player, int key) override
            {
                if (state_->dead || !state_->onKey)
                    return;
                sp::CallArgs args;
                args.PushCell(state_->handle);
                args.PushCell(EntityCell(player));
                args.PushCell(key);
                args.PushCell(state_->data);
                Invoke(state_->onKey, args);
            }

            void Reset() override
            {
                // Also called on every close, when this is no longer the active one.
                if (state_->dead || !state_->onReset || menus::menuManager.GetActiveMenu(player_) != this)
                    return;
                sp::CallArgs args;
                args.PushCell(state_->handle);
                args.PushCell(EntityCell(player_));
                args.PushCell(state_->data);
                Invoke(state_->onReset, args);
            }

            void Close() override
            {
                menus::menuManager.CloseActiveMenu(player_);
            }

            void OnFrame() override
            {
                const float now = Now();
                if (state_->timeout > 0 && now - openedAt_ >= state_->timeout)
                {
                    menus::menuManager.CloseWith(slot_, MenuEndReason::Timeout);
                    return;
                }
                if (state_->refreshInterval > 0.0f && now - lastDraw_ >= state_->refreshInterval)
                {
                    Display();
                    return;
                }
                if (!sentHtml_.empty() && now - sentAt_ >= kRefreshAfter)
                    Send();
            }

            bool AcceptsKeys() const override
            {
                return state_->acceptsKeys;
            }

            uint32_t KeyMask() const override
            {
                return state_->keyMask;
            }

            void OnEnd(MenuEndReason reason) override
            {
                if (state_->dead || !state_->onClose)
                    return;
                sp::CallArgs args;
                args.PushCell(state_->handle);
                args.PushCell(EntityCell(player_));
                args.PushCell(static_cast<cell_t>(reason));
                args.PushCell(state_->data);
                Invoke(state_->onClose, args);
            }

            void ShowHtml(const char* html)
            {
                if (sentHtml_ == html)
                    return;
                sentHtml_ = html;
                Send();
            }

        private:
            void Send()
            {
                sentAt_ = Now();
                player_->PrintToCenterHtml(sentHtml_.c_str(), kPanelSeconds, true);
            }

            std::shared_ptr<State> state_;
            int slot_;
            float openedAt_;
            float lastDraw_ = 0.0f;
            float sentAt_ = 0.0f;
            std::string sentHtml_;
        };

        void CloseAll(const std::shared_ptr<State>& state)
        {
            std::vector<int> slots;
            for (const auto& [slot, instance] : state->open)
                slots.push_back(slot);
            for (int slot : slots)
                menus::menuManager.CloseWith(slot, MenuEndReason::Closed);
        }

        HandleType Type()
        {
            static const HandleType t = Handles().CreateType("Source2ModMenuInstance", [](void* p)
            {
                auto* h = static_cast<MenuHandle*>(p);
                h->state->dead = true;
                std::shared_ptr<State> state = h->state;
                delete h;
                scheduler::schedulerManager.NextFrame(0, [state]() { CloseAll(state); });
            });
            return t;
        }

        std::shared_ptr<State> Of(IPluginContext* ctx, cell_t handle)
        {
            auto* h = static_cast<MenuHandle*>(ReadHandle(ctx, handle, Type()));
            return h ? h->state : nullptr;
        }

        CCSPlayerController* PlayerArg(IPluginContext* ctx, cell_t entity)
        {
            return static_cast<CCSPlayerController*>(EntityArg(ctx, entity));
        }

        bool IsUnset(cell_t id)
        {
            return id == -1 || id == 0;
        }

        ScriptInstance* OpenFor(State& state, CCSPlayerController* player)
        {
            auto it = player ? state.open.find(player->GetSlot()) : state.open.end();
            return it != state.open.end() ? it->second : nullptr;
        }

        // Source2ModMenuInstance(onDisplay, onKey, onClose, data)
        cell_t N_New(IPluginContext* ctx, const cell_t* params)
        {
            auto state = std::make_shared<State>();
            state->onDisplay = ctx->GetFunctionById(static_cast<funcid_t>(params[1]));
            state->onKey = ctx->GetFunctionById(static_cast<funcid_t>(params[2]));
            if (!state->onDisplay || !state->onKey)
                return ctx->ThrowNativeError("A custom menu needs its display and key functions");
            state->onClose = IsUnset(params[3]) ? nullptr : ctx->GetFunctionById(static_cast<funcid_t>(params[3]));
            state->data = params[4];
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            state->owner = plugin ? plugin->Id() : 0;
            auto* h = new MenuHandle();
            h->state = state;
            state->handle = Handles().Create(Type(), h, state->owner);
            if (!state->handle)
            {
                delete h;
                return 0;
            }
            return state->handle;
        }

        cell_t N_Open(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            CCSPlayerController* player = state ? PlayerArg(ctx, params[2]) : nullptr;
            if (player)
                menus::menuManager.OpenMenu(state->owner, player, new ScriptInstance(player, state));
            return 0;
        }

        cell_t N_Redraw(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            if (ScriptInstance* instance = state ? OpenFor(*state, PlayerArg(ctx, params[2])) : nullptr)
                instance->Display();
            return 0;
        }

        // Reset(player, redraw)
        cell_t N_Reset(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            if (ScriptInstance* instance = state ? OpenFor(*state, PlayerArg(ctx, params[2])) : nullptr)
            {
                instance->Reset();
                if (params[3])
                    instance->Display();
            }
            return 0;
        }

        // Close(player): NULL_ENTITY for everyone.
        cell_t N_Close(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            if (!state)
                return 0;
            if (params[2] == -1)
            {
                CloseAll(state);
                return 0;
            }
            CCSPlayerController* player = PlayerArg(ctx, params[2]);
            if (player && OpenFor(*state, player))
                menus::menuManager.CloseWith(player->GetSlot(), MenuEndReason::Closed);
            return 0;
        }

        cell_t N_IsOpenFor(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            return state && OpenFor(*state, PlayerArg(ctx, params[2]));
        }

        // ShowHtml(player, html)
        cell_t N_ShowHtml(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            if (ScriptInstance* instance = state ? OpenFor(*state, PlayerArg(ctx, params[2])) : nullptr)
                instance->ShowHtml(LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_OnReset(IPluginContext* ctx, const cell_t* params)
        {
            if (auto state = Of(ctx, params[1]))
                state->onReset = IsUnset(params[2]) ? nullptr : ctx->GetFunctionById(static_cast<funcid_t>(params[2]));
            return 0;
        }

        cell_t N_KeyMaskGet(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            return state ? static_cast<cell_t>(state->keyMask) : 0;
        }

        cell_t N_KeyMaskSet(IPluginContext* ctx, const cell_t* params)
        {
            if (auto state = Of(ctx, params[1]))
                state->keyMask = static_cast<uint32_t>(params[2]);
            return 0;
        }

        cell_t N_AcceptsKeysGet(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            return state && state->acceptsKeys;
        }

        cell_t N_AcceptsKeysSet(IPluginContext* ctx, const cell_t* params)
        {
            if (auto state = Of(ctx, params[1]))
                state->acceptsKeys = params[2] != 0;
            return 0;
        }

        cell_t N_RefreshGet(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            return sp_ftoc(state ? state->refreshInterval : 0.0f);
        }

        cell_t N_RefreshSet(IPluginContext* ctx, const cell_t* params)
        {
            if (auto state = Of(ctx, params[1]))
                state->refreshInterval = sp_ctof(params[2]);
            return 0;
        }

        cell_t N_TimeoutGet(IPluginContext* ctx, const cell_t* params)
        {
            auto state = Of(ctx, params[1]);
            return state ? state->timeout : 0;
        }

        cell_t N_TimeoutSet(IPluginContext* ctx, const cell_t* params)
        {
            if (auto state = Of(ctx, params[1]))
                state->timeout = params[2];
            return 0;
        }

        // Source2ModMenus.GetActiveMenuOwner(player): 0 for the core or no menu.
        cell_t N_GetActiveMenuOwner(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(menus::menuManager.OwnerOf(PlayerArg(ctx, params[1])));
        }

        // Source2ModMenus.GetActiveMenuTitle(player, buffer, maxlen)
        cell_t N_GetActiveMenuTitle(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = PlayerArg(ctx, params[1]);
            IMenu* menu = player ? menus::menuManager.GetOpenMenu(player) : nullptr;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], menu ? menu->Title().c_str() : "", &written);
            return static_cast<cell_t>(written);
        }

        const sp_nativeinfo_t g_natives[] = {
            { "Source2ModMenuInstance.Source2ModMenuInstance", N_New },
            { "Source2ModMenuInstance.Open", N_Open },
            { "Source2ModMenuInstance.Redraw", N_Redraw },
            { "Source2ModMenuInstance.Reset", N_Reset },
            { "Source2ModMenuInstance.Close", N_Close },
            { "Source2ModMenuInstance.IsOpenFor", N_IsOpenFor },
            { "Source2ModMenuInstance.ShowHtml", N_ShowHtml },
            { "Source2ModMenuInstance.OnReset", N_OnReset },
            { "Source2ModMenuInstance.KeyMask.get", N_KeyMaskGet },
            { "Source2ModMenuInstance.KeyMask.set", N_KeyMaskSet },
            { "Source2ModMenuInstance.AcceptsKeys.get", N_AcceptsKeysGet },
            { "Source2ModMenuInstance.AcceptsKeys.set", N_AcceptsKeysSet },
            { "Source2ModMenuInstance.RefreshInterval.get", N_RefreshGet },
            { "Source2ModMenuInstance.RefreshInterval.set", N_RefreshSet },
            { "Source2ModMenuInstance.Timeout.get", N_TimeoutGet },
            { "Source2ModMenuInstance.Timeout.set", N_TimeoutSet },
            { "Source2ModMenus.GetActiveMenuOwner", N_GetActiveMenuOwner },
            { "Source2ModMenus.GetActiveMenuTitle", N_GetActiveMenuTitle },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetCustomMenuNatives()
    {
        return g_natives;
    }
}
