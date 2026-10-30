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
 * Translations (ISource2ModTranslations): languages from configs/languages.cfg
 * and added at runtime, phrases per plugin from phrase files, JSON or calls.
 */
#pragma once
#include "core/appsystem/appsystem.h"

#include <source2mod/ISource2ModTranslations.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace translations
{
    struct Phrase
    {
        /// The #format spec of each placeholder by number (1-based; [0] unused).
        std::vector<std::string> format;
        /// The text per language index.
        std::unordered_map<int, std::string> text;
    };

    class TranslationsManager final : public ISource2ModTranslations, public appsystem::ISource2ModAppSystem
    {
    public:
        appsystem::SystemId GetSystemId() const override { return appsystem::SystemId::Translations; }

        int GetLanguageCount() override;
        bool GetLanguageInfo(int language, const char** code, const char** name) override;
        int FindLanguage(const char* codeOrName) override;
        int AddLanguage(const char* code, const char* name) override;
        int GetServerLanguage() override;
        int GetPlayerLanguage(CPlayerSlot slot) override;
        void SetPlayerLanguage(CPlayerSlot slot, int language) override;

        bool LoadPhraseFile(const char* file, char* error, size_t maxlen) override { return LoadPhraseFile(PluginId(0), file, error, maxlen); }
        bool LoadPhraseFile(PluginId owner, const char* file, char* error, size_t maxlen);
        bool LoadPhrasesJson(const char* json, char* error, size_t maxlen) override { return LoadPhrasesJson(PluginId(0), json, error, maxlen); }
        bool LoadPhrasesJson(PluginId owner, const char* json, char* error, size_t maxlen);
        void AddPhrase(const char* phrase, const char* language, const char* text, const char* format = nullptr) override { return AddPhrase(PluginId(0), phrase, language, text, format); }
        void AddPhrase(PluginId owner, const char* phrase, const char* language, const char* text, const char* format = nullptr);
        bool RemovePhrase(const char* phrase, const char* language = nullptr) override { return RemovePhrase(PluginId(0), phrase, language); }
        bool RemovePhrase(PluginId owner, const char* phrase, const char* language = nullptr);
        void ClearPhrases() override { return ClearPhrases(PluginId(0)); }
        void ClearPhrases(PluginId owner);
        bool PhraseExists(const char* phrase) override { return PhraseExists(PluginId(0), phrase); }
        bool PhraseExists(PluginId owner, const char* phrase);
        bool IsTranslatedFor(const char* phrase, int language) override { return IsTranslatedFor(PluginId(0), phrase, language); }
        bool IsTranslatedFor(PluginId owner, const char* phrase, int language);

        size_t Translate(const char* phrase, CPlayerSlot target, char* buffer, size_t maxlen) override { return Translate(PluginId(0), phrase, target, buffer, maxlen); }
        size_t Translate(PluginId owner, const char* phrase, CPlayerSlot target, char* buffer, size_t maxlen);
        size_t FormatPhrase(const char* phrase, CPlayerSlot target, const char* const* args, int numArgs, char* buffer, size_t maxlen) override { return FormatPhrase(PluginId(0), phrase, target, args, numArgs, buffer, maxlen); }
        size_t FormatPhrase(PluginId owner, const char* phrase, CPlayerSlot target,
                            const char* const* args, int numArgs, char* buffer, size_t maxlen);
        size_t TranslateLanguage(const char* phrase, int language, char* buffer, size_t maxlen) override { return TranslateLanguage(PluginId(0), phrase, language, buffer, maxlen); }
        size_t TranslateLanguage(PluginId owner, const char* phrase, int language, char* buffer, size_t maxlen);

        // ---- core side ----

        /// A plugin's phrase; nullptr when it has none by that name.
        const Phrase* Find(PluginId owner, const char* phrase);

        /// The text for a target: its language, the server's, English; nullptr when none.
        const std::string* TextFor(const Phrase& phrase, CPlayerSlot target);
        /// The same for a language index (-1 = the server's).
        const std::string* TextForLanguage(const Phrase& phrase, int language);

        /// Parses "{1:s},{2:d}" into a phrase's format.
        static void ParseFormat(Phrase& phrase, const char* format);

        void RemoveAllForPlugin(PluginId owner) override;

    private:
        struct Language
        {
            std::string code;
            std::string name;
        };

        struct PlayerLanguage
        {
            int userId = -1;    // whose override this is
            int language = -1;
        };

        void EnsureLanguages();
        int LanguageByCode(const char* code) const;
        Phrase& Slot(PluginId owner, const char* phrase);

        std::vector<Language> m_languages;
        bool m_languagesLoaded = false;
        int m_serverLanguage = 0;
        std::array<PlayerLanguage, ABSOLUTE_PLAYER_LIMIT> m_players{};
        std::unordered_map<PluginId, std::unordered_map<std::string, Phrase>> m_phrases;
    };

    extern TranslationsManager translationsManager;
}
