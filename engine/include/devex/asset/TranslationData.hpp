#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

// The extension of the tables of translations, as spreadsheets write them.
inline constexpr std::string_view translationExtension = ".csv";

// The cells of a CSV file, row by row, with what writing it back the same way needs: the character
// between cells, and the mark some programs put at the start of a UTF-8 file.
struct DEVEX_API CsvTable
{
    std::vector<std::vector<std::string>> rows;
    char delimiter = ',';
    bool byteOrderMark = false;

    bool operator==(const CsvTable&) const = default;
};

// Reads CSV text as spreadsheets write it: a cell in double quotes may hold the delimiter, line
// breaks and doubled quotes. A delimiter of 0 is guessed from the first line: a comma, a semicolon
// (what spreadsheets write where the comma is the decimal mark, as in French) or a tab.
[[nodiscard]] DEVEX_API core::Result<CsvTable> parseCsv(std::string_view text, char delimiter = 0);
// Writes the cells back, quoting those that hold the delimiter, a quote or a line break.
[[nodiscard]] DEVEX_API std::string writeCsv(const CsvTable& table);

// A table of translations: the message of each key in several languages.
struct DEVEX_API TranslationData
{
    // Codes such as "en", "fr" or "pt_BR", in the order of their columns.
    std::vector<std::string> languages;
    std::vector<std::string> keys;
    // One row of languages.size() messages per key; an empty message is a missing translation.
    std::vector<std::string> messages;

    [[nodiscard]] const std::string& message(std::size_t key, std::size_t language) const noexcept;

    bool operator==(const TranslationData&) const = default;
};

// The translations a CSV table holds, laid out as Godot reads them: the first row names the
// languages above their columns, after the column of the keys; columns whose name starts with '_'
// are notes, left out, and rows without a key are skipped. In messages, \n, \t and \\ stand for a
// line break, a tab and a backslash. A key written twice keeps its first row.
[[nodiscard]] DEVEX_API core::Result<TranslationData> readTranslationTable(const CsvTable& table);
[[nodiscard]] DEVEX_API core::Result<TranslationData> parseTranslationCsv(std::string_view text, char delimiter = 0);

// The translations of one language in the format of gettext, which Poedit and Weblate edit: what translators get
// and give back, and a table of a project as well.
inline constexpr std::string_view translationPoExtension = ".po";
inline constexpr std::string_view translationPotExtension = ".pot";
// The table of one language of a PO file: msgctxt holds the key, msgid the text of the source language and msgstr the
// translation; without msgctxt, msgid is the key. The language comes from the header (Language: fr), or else from
// fallbackLanguage, such as the name of the file says it. A fuzzy entry, which a translator has to check, counts as
// missing; obsolete entries are left out, and of a plural, only the first form is kept.
[[nodiscard]] DEVEX_API core::Result<TranslationData> parseTranslationPo(std::string_view text, std::string_view fallbackLanguage = {});
// A table as gettext writes it: the template (.pot), its keys with the messages of the source language, and the file
// (.po) of one of its languages. A key whose source message is empty takes the key for msgid.
[[nodiscard]] DEVEX_API std::string writeTranslationPot(const TranslationData& table, std::size_t sourceLanguage);
[[nodiscard]] DEVEX_API std::string writeTranslationPo(const TranslationData& table, std::size_t language, std::size_t sourceLanguage);
// The language a PO file is named after: "fr" for fr.po or game.fr.po, "pt_BR" for pt-BR.po; empty when its name says
// none.
[[nodiscard]] DEVEX_API std::string languageOfPoFile(std::string_view fileName);

// Whether a column name is a language code: two or three letters, then regions or scripts after
// '_' or '-', such as "fr", "pt_BR", "zh-Hant" or "es_419".
[[nodiscard]] DEVEX_API bool isLanguageCode(std::string_view code) noexcept;
// "fr-fr" and "FR_FR" become "fr_FR": the language in lowercase, a region in uppercase, a script with
// a capital first, joined by '_'.
[[nodiscard]] DEVEX_API std::string normalizeLanguage(std::string_view code);
// The language without its region or script: "fr" for "fr_CA".
[[nodiscard]] DEVEX_API std::string_view baseLanguage(std::string_view code) noexcept;
// The name of a language in English, as the editor lists it ("French", "Portuguese (Brazil)"), and
// in the language itself, as a menu of the game lists it ("Français", "Português (BR)"). The code
// itself for a language these do not know.
[[nodiscard]] DEVEX_API std::string languageName(std::string_view code);
[[nodiscard]] DEVEX_API std::string nativeLanguageName(std::string_view code);
// The codes of the languages games are most often translated into, for the menus of the editor.
[[nodiscard]] DEVEX_API std::span<const std::string_view> commonLanguages() noexcept;

// The characters a table writes: its keys, its messages and the names of its languages in
// themselves, which a menu of languages shows. Sorted, each once.
[[nodiscard]] DEVEX_API std::vector<std::uint32_t> translationCharacters(const TranslationData& table);

} // namespace devex::asset
