#include <devex/asset/TranslationData.hpp>

#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <unordered_map>
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

namespace {

// A string of a PO file, between its quotes, with its escapes undone; none when it is not one.
[[nodiscard]] std::optional<std::string> poString(std::string_view quoted)
{
    if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"')
    {
        return std::nullopt;
    }
    std::string text;
    for (std::size_t index = 1; index + 1 < quoted.size(); ++index)
    {
        const char character = quoted[index];
        if (character != '\\' || index + 2 >= quoted.size())
        {
            text += character;
            continue;
        }
        const char escaped = quoted[++index];
        switch (escaped)
        {
        case 'n':
            text += '\n';
            break;
        case 't':
            text += '\t';
            break;
        case 'r':
            text += '\r';
            break;
        case '"':
        case '\\':
            text += escaped;
            break;
        default:
            text += '\\';
            text += escaped;
            break;
        }
    }
    return text;
}

[[nodiscard]] std::string poQuoted(std::string_view text)
{
    std::string quoted = "\"";
    for (const char character : text)
    {
        switch (character)
        {
        case '\n':
            quoted += "\\n";
            break;
        case '\t':
            quoted += "\\t";
            break;
        case '\r':
            quoted += "\\r";
            break;
        case '"':
            quoted += "\\\"";
            break;
        case '\\':
            quoted += "\\\\";
            break;
        default:
            quoted += character;
            break;
        }
    }
    return quoted + "\"";
}

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }
    return text;
}

struct PoEntry
{
    std::string context;
    std::string id;
    std::string translation;
    bool hasContext = false;
    bool hasId = false;
    bool fuzzy = false;
};

// What every PO file of Devex begins with: no key, and how its text is written.
[[nodiscard]] std::string poHeader(std::string_view language)
{
    std::string header = "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n";
    if (!language.empty())
    {
        header += std::format("\"Language: {}\\n\"\n", language);
    }
    return header + "\"X-Generator: Devex\\n\"\n";
}

[[nodiscard]] std::string poEntries(const TranslationData& table, std::optional<std::size_t> language, std::size_t sourceLanguage)
{
    std::string text;
    for (std::size_t key = 0; key < table.keys.size(); ++key)
    {
        const std::string& source = sourceLanguage < table.languages.size() ? table.message(key, sourceLanguage) : table.keys[key];
        text += std::format("\nmsgctxt {}\nmsgid {}\nmsgstr {}\n", poQuoted(table.keys[key]), poQuoted(source.empty() ? table.keys[key] : source),
                            poQuoted(language ? table.message(key, *language) : std::string{}));
    }
    return text;
}

} // namespace

core::Result<TranslationData> parseTranslationPo(std::string_view text, std::string_view fallbackLanguage)
{
    std::vector<PoEntry> entries;
    PoEntry entry;
    bool started = false;
    bool fuzzyNext = false;
    bool plurals = false;
    // Where a string on a line of its own goes, after the keyword that began it.
    std::string* field = nullptr;
    std::string ignored;
    const auto begin = [&] {
        if (started)
        {
            entries.push_back(std::move(entry));
        }
        entry = PoEntry{.fuzzy = std::exchange(fuzzyNext, false)};
        started = true;
    };
    std::size_t lineNumber = 0;
    while (!text.empty())
    {
        const std::size_t end = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++lineNumber;
        if (line.empty() || line.starts_with("#~"))
        {
            continue;
        }
        if (line.starts_with("#,"))
        {
            fuzzyNext = fuzzyNext || line.find("fuzzy") != std::string_view::npos;
            continue;
        }
        if (line.starts_with('#'))
        {
            continue;
        }
        const std::size_t space = line.find_first_of(" \t");
        const std::string_view keyword = line.starts_with('"') ? std::string_view{} : line.substr(0, space);
        const std::string_view rest = trimmed(keyword.empty() ? line : line.substr(std::min(space, line.size())));
        const std::optional<std::string> value = poString(rest);
        if (!value)
        {
            return core::makeError(core::ErrorCode::Parse, "line {} of the PO file holds no string between quotes", lineNumber);
        }
        if (keyword.empty())
        {
            if (field == nullptr)
            {
                return core::makeError(core::ErrorCode::Parse, "line {} of the PO file continues no keyword", lineNumber);
            }
            *field += *value;
            continue;
        }
        if (keyword == "msgctxt")
        {
            begin();
            entry.hasContext = true;
            field = &entry.context;
        }
        else if (keyword == "msgid")
        {
            if (!started || entry.hasId)
            {
                begin();
            }
            entry.hasId = true;
            field = &entry.id;
        }
        else if (keyword == "msgid_plural")
        {
            plurals = true;
            field = &ignored;
        }
        else if (keyword == "msgstr" || keyword == "msgstr[0]")
        {
            field = &entry.translation;
        }
        else if (keyword.starts_with("msgstr["))
        {
            field = &ignored;
        }
        else
        {
            return core::makeError(core::ErrorCode::Parse, "line {} of the PO file has an unknown keyword: {}", lineNumber, keyword);
        }
        field->clear();
        *field += *value;
    }
    if (started)
    {
        entries.push_back(std::move(entry));
    }
    if (plurals)
    {
        DEVEX_LOG_WARNING("Plural forms of the PO file are not read: each message keeps its first form");
    }

    // The language of the header, or the one the file is named after.
    std::string language;
    for (const PoEntry& found : entries)
    {
        if (!found.hasContext && found.id.empty())
        {
            std::string_view header = found.translation;
            while (!header.empty())
            {
                const std::size_t end = header.find('\n');
                const std::string_view line = trimmed(header.substr(0, end));
                header.remove_prefix(end == std::string_view::npos ? header.size() : end + 1);
                if (line.size() > 9 && std::ranges::equal(line.substr(0, 9), std::string_view("language:"),
                                                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
                {
                    language = std::string(trimmed(line.substr(9)));
                }
            }
        }
    }
    if (!isLanguageCode(language))
    {
        language = std::string(fallbackLanguage);
    }
    if (!isLanguageCode(language))
    {
        return core::makeError(core::ErrorCode::Parse,
                               "the PO file says no language: write Language: in its header, or name the file after it, as fr.po");
    }
    TranslationData table;
    table.languages.push_back(normalizeLanguage(language));
    std::unordered_map<std::string, std::size_t> seen;
    for (PoEntry& found : entries)
    {
        std::string key = found.hasContext ? std::move(found.context) : std::move(found.id);
        if (key.empty() || !seen.emplace(key, table.keys.size()).second)
        {
            continue;
        }
        table.keys.push_back(std::move(key));
        table.messages.push_back(found.fuzzy ? std::string{} : std::move(found.translation));
    }
    return table;
}

std::string writeTranslationPot(const TranslationData& table, std::size_t sourceLanguage)
{
    return poHeader({}) + poEntries(table, std::nullopt, sourceLanguage);
}

std::string writeTranslationPo(const TranslationData& table, std::size_t language, std::size_t sourceLanguage)
{
    return poHeader(language < table.languages.size() ? std::string_view(table.languages[language]) : std::string_view{}) +
           poEntries(table, language < table.languages.size() ? std::optional(language) : std::nullopt, sourceLanguage);
}

std::string languageOfPoFile(std::string_view fileName)
{
    std::string_view name = fileName;
    if (const std::size_t dot = name.rfind('.'); dot != std::string_view::npos)
    {
        name = name.substr(0, dot);
    }
    if (const std::size_t dot = name.rfind('.'); dot != std::string_view::npos)
    {
        name = name.substr(dot + 1);
    }
    return isLanguageCode(name) ? normalizeLanguage(name) : std::string{};
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
