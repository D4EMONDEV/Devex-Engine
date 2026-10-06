#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/TranslationData.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace devex::asset {

// A value that takes the place of {name} in a translated text.
struct DEVEX_API TranslationValue
{
    std::string_view name;
    std::string_view value;
};

// The translations of a game: the messages of its tables, merged, and the language it shows. A key
// is looked up in that language, then in the fallback language, as Godot does; without a message,
// the key shows as it is written, so that a text which is no key stays as it is.
class DEVEX_API Localization
{
public:
    // Replaces the tables. A message missing from one table is taken from the next that has it.
    void setTables(std::span<const std::shared_ptr<const TranslationData>> tables);
    // What shows where the language has no message: English unless set.
    void setFallbackLanguage(std::string_view language);
    [[nodiscard]] const std::string& fallbackLanguage() const noexcept;

    // Every language a table has a column for, sorted.
    [[nodiscard]] const std::vector<std::string>& languages() const noexcept;
    // The language of the tables closest to the one wanted: itself, its base language (fr for
    // fr_CA), or the same language of another region (fr_FR for fr); nothing when none is.
    [[nodiscard]] std::optional<std::string> closestLanguage(std::string_view wanted) const;
    // Shows a language: the closest one the tables have, or the one asked when none is close. An
    // empty language shows the keys themselves, as the editor does before one is previewed.
    void setLanguage(std::string_view language);
    [[nodiscard]] const std::string& language() const noexcept;

    // The message of a key in the language or the fallback one; null when neither has one.
    [[nodiscard]] const std::string* find(std::string_view key) const;
    // The message, or the key itself.
    [[nodiscard]] std::string_view translate(std::string_view key) const;
    // The same, with each {name} replaced by its value; {{ and }} write braces, and a name without a
    // value stays as it is written.
    [[nodiscard]] std::string translate(std::string_view key, std::span<const TranslationValue> values) const;

    // Changes whenever translate may give something else: other tables, another language.
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    struct KeyHash
    {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    [[nodiscard]] std::optional<std::size_t> columnOf(std::string_view language) const;
    void updateChain();

    std::vector<std::string> m_languages;
    // The row of each key in m_messages, which holds one message per language.
    std::unordered_map<std::string, std::size_t, KeyHash, std::equal_to<>> m_keys;
    std::vector<std::string> m_messages;
    std::string m_language;
    std::string m_fallback = "en";
    // The columns a key is looked up in, in order.
    std::vector<std::size_t> m_chain;
    std::uint64_t m_revision = 0;
};

} // namespace devex::asset
