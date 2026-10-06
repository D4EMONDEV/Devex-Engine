#include <devex/asset/TranslationData.hpp>

#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <unordered_set>
#include <utility>

namespace devex::asset {
namespace {

// What the editor and the games call a language: its name in English, and in itself.
struct LanguageNames
{
    std::string_view code;
    std::string_view english;
    std::string_view native;
};

constexpr std::array<LanguageNames, 36> languageNames{{
    {"ar", "Arabic", "العربية"},
    {"bg", "Bulgarian", "Български"},
    {"ca", "Catalan", "Català"},
    {"cs", "Czech", "Čeština"},
    {"da", "Danish", "Dansk"},
    {"de", "German", "Deutsch"},
    {"el", "Greek", "Ελληνικά"},
    {"en", "English", "English"},
    {"eo", "Esperanto", "Esperanto"},
    {"es", "Spanish", "Español"},
    {"et", "Estonian", "Eesti"},
    {"fi", "Finnish", "Suomi"},
    {"fr", "French", "Français"},
    {"he", "Hebrew", "עברית"},
    {"hi", "Hindi", "हिन्दी"},
    {"hr", "Croatian", "Hrvatski"},
    {"hu", "Hungarian", "Magyar"},
    {"id", "Indonesian", "Bahasa Indonesia"},
    {"it", "Italian", "Italiano"},
    {"ja", "Japanese", "日本語"},
    {"ko", "Korean", "한국어"},
    {"lt", "Lithuanian", "Lietuvių"},
    {"lv", "Latvian", "Latviešu"},
    {"nb", "Norwegian Bokmål", "Norsk bokmål"},
    {"nl", "Dutch", "Nederlands"},
    {"pl", "Polish", "Polski"},
    {"pt", "Portuguese", "Português"},
    {"ro", "Romanian", "Română"},
    {"ru", "Russian", "Русский"},
    {"sk", "Slovak", "Slovenčina"},
    {"sr", "Serbian", "Српски"},
    {"sv", "Swedish", "Svenska"},
    {"th", "Thai", "ไทย"},
    {"tr", "Turkish", "Türkçe"},
    {"uk", "Ukrainian", "Українська"},
    {"vi", "Vietnamese", "Tiếng Việt"},
}};

constexpr std::array<std::pair<std::string_view, std::string_view>, 20> regionNames{{
    {"419", "Latin America"},
    {"AR", "Argentina"},
    {"AT", "Austria"},
    {"AU", "Australia"},
    {"BE", "Belgium"},
    {"BR", "Brazil"},
    {"CA", "Canada"},
    {"CH", "Switzerland"},
    {"CN", "China"},
    {"DE", "Germany"},
    {"ES", "Spain"},
    {"FR", "France"},
    {"GB", "United Kingdom"},
    {"HK", "Hong Kong"},
    {"IE", "Ireland"},
    {"MX", "Mexico"},
    {"NZ", "New Zealand"},
    {"PT", "Portugal"},
    {"TW", "Taiwan"},
    {"US", "United States"},
}};

constexpr std::array<std::string_view, 30> common{
    "en", "fr", "de", "es", "it", "pt", "pt_BR", "ru", "pl", "nl", "sv", "da", "fi", "nb", "cs",
    "hu", "ro", "el", "tr", "uk", "ar", "he", "hi", "th", "vi", "id", "ja", "ko", "zh_Hans", "zh_Hant",
};

[[nodiscard]] bool isLetters(std::string_view part) noexcept
{
    return std::ranges::all_of(part, [](unsigned char character) { return std::isalpha(character) != 0; });
}

[[nodiscard]] bool isDigits(std::string_view part) noexcept
{
    return std::ranges::all_of(part, [](unsigned char character) { return std::isdigit(character) != 0; });
}

// The parts of a code, split at '_' and '-'.
[[nodiscard]] std::vector<std::string_view> codeParts(std::string_view code)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= code.size(); ++index)
    {
        if (index == code.size() || code[index] == '_' || code[index] == '-')
        {
            parts.push_back(code.substr(start, index - start));
            start = index + 1;
        }
    }
    return parts;
}

[[nodiscard]] const LanguageNames* findLanguage(std::string_view language) noexcept
{
    const auto found = std::ranges::find(languageNames, language, &LanguageNames::code);
    return found != languageNames.end() ? &*found : nullptr;
}

// The text of a message as it reads: \n, \t and \\ as the characters they stand for.
[[nodiscard]] std::string unescape(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] == '\\' && index + 1 < text.size())
        {
            const char next = text[index + 1];
            if (next == 'n' || next == 't' || next == '\\')
            {
                result += next == 'n' ? '\n' : next == 't' ? '\t' : '\\';
                ++index;
                continue;
            }
        }
        result += text[index];
    }
    return result;
}

// The delimiter most present on the first line, outside quotes; a comma when there is none.
[[nodiscard]] char guessDelimiter(std::string_view text) noexcept
{
    std::array<std::pair<char, int>, 3> counts{{{',', 0}, {';', 0}, {'\t', 0}}};
    bool quoted = false;
    for (const char character : text)
    {
        if (character == '"')
        {
            quoted = !quoted;
        }
        else if (!quoted && (character == '\n' || character == '\r'))
        {
            break;
        }
        else if (!quoted)
        {
            for (auto& [delimiter, count] : counts)
            {
                count += character == delimiter ? 1 : 0;
            }
        }
    }
    const auto most = std::ranges::max_element(counts, {}, &std::pair<char, int>::second);
    return most->second > 0 ? most->first : ',';
}

// The characters of a UTF-8 text, each once in `found`; invalid bytes are skipped.
void collectCharacters(std::string_view text, std::unordered_set<std::uint32_t>& found)
{
    for (std::size_t index = 0; index < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[index]);
        const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || index + length > text.size())
        {
            ++index;
            continue;
        }
        std::uint32_t codepoint = length == 1 ? lead : lead & (0xFFu >> (length + 1));
        bool valid = true;
        for (std::size_t next = 1; next < length; ++next)
        {
            const auto byte = static_cast<unsigned char>(text[index + next]);
            valid = valid && (byte & 0xC0) == 0x80;
            codepoint = (codepoint << 6) | (byte & 0x3Fu);
        }
        index += valid ? length : 1;
        // Line breaks and tabs are not drawn.
        if (valid && codepoint >= 0x20)
        {
            found.insert(codepoint);
        }
    }
}

} // namespace

core::Result<CsvTable> parseCsv(std::string_view text, char delimiter)
{
    CsvTable table;
    if (text.starts_with("\xEF\xBB\xBF"))
    {
        table.byteOrderMark = true;
        text.remove_prefix(3);
    }
    table.delimiter = delimiter != 0 ? delimiter : guessDelimiter(text);

    std::vector<std::string> row;
    std::string cell;
    bool quoted = false;
    bool cellStart = true;
    std::size_t line = 1;
    std::size_t quoteLine = 0;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const char character = text[index];
        if (quoted)
        {
            if (character == '"')
            {
                // Two quotes in a row write one.
                if (index + 1 < text.size() && text[index + 1] == '"')
                {
                    cell += '"';
                    ++index;
                }
                else
                {
                    quoted = false;
                }
                continue;
            }
            line += character == '\n' ? 1 : 0;
            cell += character;
            continue;
        }
        if (character == '"' && cellStart)
        {
            quoted = true;
            cellStart = false;
            quoteLine = line;
            continue;
        }
        if (character == table.delimiter)
        {
            row.push_back(std::exchange(cell, {}));
            cellStart = true;
            continue;
        }
        if (character == '\r' || character == '\n')
        {
            row.push_back(std::exchange(cell, {}));
            table.rows.push_back(std::exchange(row, {}));
            cellStart = true;
            if (character == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
            {
                ++index;
            }
            ++line;
            continue;
        }
        cell += character;
        cellStart = false;
    }
    if (quoted)
    {
        return core::makeError(core::ErrorCode::Parse, "the quote that opens a cell on line {} is never closed", quoteLine);
    }
    // A last line without its line break.
    if (!cellStart || !row.empty())
    {
        row.push_back(std::move(cell));
        table.rows.push_back(std::move(row));
    }
    return table;
}

std::string writeCsv(const CsvTable& table)
{
    std::string text = table.byteOrderMark ? "\xEF\xBB\xBF" : "";
    for (const std::vector<std::string>& row : table.rows)
    {
        for (std::size_t index = 0; index < row.size(); ++index)
        {
            if (index > 0)
            {
                text += table.delimiter;
            }
            const std::string& cell = row[index];
            if (cell.find_first_of(std::string{table.delimiter, '"', '\n', '\r'}) == std::string::npos)
            {
                text += cell;
                continue;
            }
            text += '"';
            for (const char character : cell)
            {
                text += character;
                if (character == '"')
                {
                    text += '"';
                }
            }
            text += '"';
        }
        text += '\n';
    }
    return text;
}

const std::string& TranslationData::message(std::size_t key, std::size_t language) const noexcept
{
    static const std::string none;
    const std::size_t index = key * languages.size() + language;
    return language < languages.size() && index < messages.size() ? messages[index] : none;
}

core::Result<TranslationData> readTranslationTable(const CsvTable& table)
{
    if (table.rows.empty() || table.rows.front().size() < 2)
    {
        return core::makeError(core::ErrorCode::Parse,
                               "the first row names the languages, such as en or fr, after the column of the keys");
    }
    TranslationData data;
    std::vector<std::size_t> columns;
    const std::vector<std::string>& header = table.rows.front();
    for (std::size_t column = 1; column < header.size(); ++column)
    {
        const std::string& name = header[column];
        if (name.empty() || name.starts_with('_'))
        {
            continue;
        }
        if (!isLanguageCode(name))
        {
            return core::makeError(core::ErrorCode::Parse,
                                   "column {} is named '{}', which is not a language code such as en or pt_BR; "
                                   "a column of notes starts with _",
                                   column + 1, name);
        }
        std::string language = normalizeLanguage(name);
        if (std::ranges::find(data.languages, language) != data.languages.end())
        {
            return core::makeError(core::ErrorCode::Parse, "the language {} has two columns", language);
        }
        data.languages.push_back(std::move(language));
        columns.push_back(column);
    }
    if (data.languages.empty())
    {
        return core::makeError(core::ErrorCode::Parse, "the table has no column of a language, such as en or fr");
    }

    std::unordered_set<std::string_view> seen;
    for (std::size_t row = 1; row < table.rows.size(); ++row)
    {
        const std::vector<std::string>& cells = table.rows[row];
        if (cells.empty() || cells.front().empty())
        {
            continue;
        }
        if (!seen.insert(cells.front()).second)
        {
            DEVEX_LOG_WARNING("The key {} is written twice in a table of translations: row {} is left out", cells.front(), row + 1);
            continue;
        }
        data.keys.push_back(cells.front());
        for (const std::size_t column : columns)
        {
            data.messages.push_back(column < cells.size() ? unescape(cells[column]) : std::string{});
        }
    }
    return data;
}

core::Result<TranslationData> parseTranslationCsv(std::string_view text, char delimiter)
{
    const core::Result<CsvTable> table = parseCsv(text, delimiter);
    if (!table)
    {
        return std::unexpected(table.error());
    }
    return readTranslationTable(*table);
}

bool isLanguageCode(std::string_view code) noexcept
{
    const std::vector<std::string_view> parts = codeParts(code);
    if (parts.empty() || parts.size() > 3 || parts.front().size() < 2 || parts.front().size() > 3 || !isLetters(parts.front()))
    {
        return false;
    }
    return std::all_of(parts.begin() + 1, parts.end(), [](std::string_view part) {
        return (part.size() == 2 && isLetters(part)) || (part.size() == 4 && isLetters(part)) || (part.size() == 3 && isDigits(part));
    });
}

std::string normalizeLanguage(std::string_view code)
{
    std::string result;
    for (const std::string_view part : codeParts(code))
    {
        if (!result.empty())
        {
            result += '_';
        }
        const bool first = result.empty();
        for (std::size_t index = 0; index < part.size(); ++index)
        {
            const auto character = static_cast<unsigned char>(part[index]);
            // The language in lowercase, a script with a capital first, a region in uppercase.
            const bool upper = !first && (part.size() == 2 || (part.size() == 4 && index == 0));
            result += static_cast<char>(upper ? std::toupper(character) : std::tolower(character));
        }
    }
    return result;
}

std::string_view baseLanguage(std::string_view code) noexcept
{
    return code.substr(0, code.find_first_of("_-"));
}

std::string languageName(std::string_view code)
{
    const std::string normalized = normalizeLanguage(code);
    if (normalized == "zh_Hans" || normalized == "zh_CN")
    {
        return "Chinese (Simplified)";
    }
    if (normalized == "zh_Hant" || normalized == "zh_TW")
    {
        return "Chinese (Traditional)";
    }
    const std::string_view base = baseLanguage(normalized);
    const LanguageNames* const names = findLanguage(base);
    std::string name = names != nullptr ? std::string(names->english) : base == "zh" ? "Chinese" : std::string(base);
    if (base.size() < normalized.size())
    {
        const std::string_view rest = std::string_view(normalized).substr(base.size() + 1);
        const auto region = std::ranges::find(regionNames, rest, &std::pair<std::string_view, std::string_view>::first);
        name += std::format(" ({})", region != regionNames.end() ? region->second : rest);
    }
    return name;
}

std::string nativeLanguageName(std::string_view code)
{
    const std::string normalized = normalizeLanguage(code);
    if (normalized == "zh_Hans" || normalized == "zh_CN")
    {
        return "简体中文";
    }
    if (normalized == "zh_Hant" || normalized == "zh_TW")
    {
        return "繁體中文";
    }
    const std::string_view base = baseLanguage(normalized);
    const LanguageNames* const names = findLanguage(base);
    std::string name = names != nullptr ? std::string(names->native) : base == "zh" ? "中文" : std::string(base);
    if (base.size() < normalized.size())
    {
        name += std::format(" ({})", std::string_view(normalized).substr(base.size() + 1));
    }
    return name;
}

std::span<const std::string_view> commonLanguages() noexcept
{
    return common;
}

std::vector<std::uint32_t> translationCharacters(const TranslationData& table)
{
    std::unordered_set<std::uint32_t> found;
    for (const std::string& key : table.keys)
    {
        collectCharacters(key, found);
    }
    for (const std::string& message : table.messages)
    {
        collectCharacters(message, found);
    }
    for (const std::string& language : table.languages)
    {
        collectCharacters(nativeLanguageName(language), found);
    }
    std::vector<std::uint32_t> characters(found.begin(), found.end());
    std::ranges::sort(characters);
    return characters;
}

} // namespace devex::asset
