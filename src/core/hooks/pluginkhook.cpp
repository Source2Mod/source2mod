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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "core/hooks/pluginkhook.h"

#include "core/appsystem/appsystem.h"
#include "utils/log.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pluginkhook
{
    namespace
    {
        class PluginKHook final : public KHook::IKHook
        {
        public:
            KHook::HookID_t SetupHook(void* function, void* context, void* removed_function, void* pre, void* post, void* make_return,
                                      void* make_call_original, unsigned int stack_size, bool async) override
            {
                return Track(KHook::__exported__khook->SetupHook(function, context, removed_function, pre, post, make_return, make_call_original, stack_size, async));
            }

            KHook::HookID_t SetupVirtualHook(void** vtable, int index, void* context, void* removed_function, void* pre, void* post, void* make_return,
                                             void* make_call_original, unsigned int stack_size, bool async) override
            {
                return Track(KHook::__exported__khook->SetupVirtualHook(vtable, index, context, removed_function, pre, post, make_return, make_call_original, stack_size, async));
            }

            void RemoveHook(KHook::HookID_t id, bool async, void (*hook_removal_fn)(KHook::HookID_t, void*), void* context) override
            {
                if (!async)
                {
                    KHook::__exported__khook->RemoveHook(id, false, hook_removal_fn, context);
                    std::lock_guard guard(m_mutex);
                    m_hooks.erase(id);
                    return;
                }

                {
                    std::lock_guard guard(m_mutex);
                    // KHook drops the callback of a second removal, and answers
                    // none for a hook it no longer has: ours would wait forever.
                    if (!m_hooks.count(id) || !m_removing.emplace(id, Removal{ hook_removal_fn, context }).second)
                    {
                        if (hook_removal_fn)
                            hook_removal_fn(id, context);
                        return;
                    }
                }
                KHook::__exported__khook->RemoveHook(id, true, &OnRemoved, this);
            }

            void* GetContextPtr() override { return KHook::__exported__khook->GetContextPtr(); }
            void* GetOriginalFunction() override { return KHook::__exported__khook->GetOriginalFunction(); }
            void* GetOriginalValuePtr() override { return KHook::__exported__khook->GetOriginalValuePtr(); }
            void* GetOverrideValuePtr() override { return KHook::__exported__khook->GetOverrideValuePtr(); }
            void* GetCurrentValuePtr(bool pop) override { return KHook::__exported__khook->GetCurrentValuePtr(pop); }
            void DestroyReturnValue() override { KHook::__exported__khook->DestroyReturnValue(); }
            void* FindOriginal(void* function) override { return KHook::__exported__khook->FindOriginal(function); }
            void* FindOriginalVirtual(void** vtable, int index) override { return KHook::__exported__khook->FindOriginalVirtual(vtable, index); }
            void* DoRecall(KHook::Action action, void* ptr_to_return, std::size_t return_size, void* init_op, void* deinit_op) override
            {
                return KHook::__exported__khook->DoRecall(action, ptr_to_return, return_size, init_op, deinit_op);
            }
            void SaveReturnValue(KHook::Action action, void* ptr_to_return, std::size_t return_size, void* init_op, void* deinit_op, bool original) override
            {
                KHook::__exported__khook->SaveReturnValue(action, ptr_to_return, return_size, init_op, deinit_op, original);
            }
            void* LookupSignature(void* start, std::size_t size, const char* signature) override { return KHook::__exported__khook->LookupSignature(start, size, signature); }
            bool WasOriginalFunctionSkipped() override { return KHook::__exported__khook->WasOriginalFunctionSkipped(); }

            /// What is left when the plugin is done, synchronously: the same as
            /// its own hook objects do when they are destroyed.
            size_t RemoveLeftovers()
            {
                std::vector<KHook::HookID_t> left;
                {
                    std::lock_guard guard(m_mutex);
                    for (const auto id : m_hooks)
                    {
                        if (!m_removing.count(id))
                            left.push_back(id);
                    }
                    for (const auto id : left)
                        m_hooks.erase(id);
                }
                for (const auto id : left)
                    KHook::__exported__khook->RemoveHook(id, false, nullptr, nullptr);
                return left.size();
            }

            bool HasPending()
            {
                std::lock_guard guard(m_mutex);
                return !m_removing.empty();
            }

        private:
            struct Removal
            {
                void (*fn)(KHook::HookID_t, void*);
                void* context;
            };

            KHook::HookID_t Track(KHook::HookID_t id)
            {
                if (id != KHook::INVALID_HOOK)
                {
                    std::lock_guard guard(m_mutex);
                    m_hooks.insert(id);
                }
                return id;
            }

            // On KHook's delete thread, or inside the call for a hook not yet inserted.
            static void OnRemoved(KHook::HookID_t id, void* self)
            {
                auto* hook = static_cast<PluginKHook*>(self);
                Removal removal{};
                {
                    std::lock_guard guard(hook->m_mutex);
                    const auto it = hook->m_removing.find(id);
                    if (it == hook->m_removing.end())
                        return;
                    removal = it->second;
                    hook->m_hooks.erase(id);
                }
                // Unlocked: it may remove more hooks. Pending until it returned,
                // since it runs in the library.
                if (removal.fn)
                    removal.fn(id, removal.context);

                std::lock_guard guard(hook->m_mutex);
                hook->m_removing.erase(id);
            }

            std::mutex m_mutex;
            std::unordered_set<KHook::HookID_t> m_hooks;
            std::unordered_map<KHook::HookID_t, Removal> m_removing;
        };

        // Never freed before the core goes: the plugin's own hook objects still
        // reach this when its library closes, after everything else of it is gone.
        std::mutex s_mutex;
        std::unordered_map<PluginId, std::unique_ptr<PluginKHook>> s_hooks;

        PluginKHook* Find(PluginId owner)
        {
            std::lock_guard guard(s_mutex);
            const auto it = s_hooks.find(owner);
            return it != s_hooks.end() ? it->second.get() : nullptr;
        }

        class PluginKHookSystem final : public appsystem::ISource2ModAppSystem
        {
            appsystem::SystemId GetSystemId() const override { return appsystem::SystemId::PluginKHook; }
            void RemoveAllForPlugin(PluginId id) override
            {
                auto* hook = Find(id);
                if (!hook)
                    return;

                if (const size_t left = hook->RemoveLeftovers())
                    FP_WARN("Plugin {} left {} KHook hook(s) behind, removed them.", id, left);
            }
        } s_system;
    }

    KHook::IKHook* ForPlugin(PluginId owner)
    {
        if (owner == 0)
            return KHook::__exported__khook;

        std::lock_guard guard(s_mutex);
        auto& hook = s_hooks[owner];
        if (!hook)
            hook = std::make_unique<PluginKHook>();
        return hook.get();
    }

    bool HasPendingRemovals(PluginId owner)
    {
        auto* hook = Find(owner);
        return hook && hook->HasPending();
    }

    void WaitForRemovals(PluginId owner)
    {
        // KHook's delete thread looks every 5 ms.
        while (HasPendingRemovals(owner))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
