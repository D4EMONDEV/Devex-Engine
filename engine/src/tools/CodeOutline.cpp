#include "CodeOutline.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace devex::tools::detail {
namespace {

[[nodiscard]] bool isIdentifierPart(char value) noexcept
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] std::string_view trimmed(std::string_view line) noexcept
{
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
    {
        line.remove_prefix(1);
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
    {
        line.remove_suffix(1);
    }
    return line;
}

// The word before a position, such as the name in front of a parenthesis.
[[nodiscard]] std::string_view wordBefore(std::string_view line, std::size_t position) noexcept
{
    std::size_t end = position;
    while (end > 0 && !isIdentifierPart(line[end - 1]))
    {
        --end;
    }
    std::size_t begin = end;
    while (begin > 0 && isIdentifierPart(line[begin - 1]))
    {
        --begin;
    }
    return line.substr(begin, end - begin);
}

// The word after a keyword, such as the name of a class.
[[nodiscard]] std::string_view wordAfter(std::string_view line, std::string_view keyword) noexcept
{
    const std::size_t found = line.find(keyword);
    if (found == std::string_view::npos)
    {
        return {};
    }
    std::size_t begin = found + keyword.size();
    while (begin < line.size() && !isIdentifierPart(line[begin]))
    {
        ++begin;
    }
    std::size_t end = begin;
    while (end < line.size() && isIdentifierPart(line[end]))
    {
        ++end;
    }
    return line.substr(begin, end - begin);
}

[[nodiscard]] bool startsWithWord(std::string_view line, std::string_view word) noexcept
{
    return line.starts_with(word) &&
           (line.size() == word.size() || !isIdentifierPart(line[word.size()]));
}

// Whether a line declares something rather than calling it: a call ends with a semicolon, and a
// declaration is followed by its body.
[[nodiscard]] bool looksLikeDeclaration(std::string_view line) noexcept
{
    return !line.ends_with(';') && !line.starts_with("return") && !line.starts_with("if") &&
           !line.starts_with("for") && !line.starts_with("while") && !line.starts_with("switch") &&
           !line.starts_with("catch") && !line.starts_with("else");
}

void readCurly(std::string_view text, bool csharp, std::vector<CodeSymbol>& symbols)
{
    constexpr std::array typeWords{"class", "struct", "enum", "interface", "record", "namespace"};
    int number = 0;
    bool inBlockComment = false;
    for (std::size_t index = 0; index <= text.size();)
    {
        const std::size_t stop = text.find('\n', index);
        const std::string_view raw = text.substr(index, (stop == std::string_view::npos ? text.size() : stop) - index);
        const std::string_view line = trimmed(raw);
        ++number;

        // Comments hold no declarations, and a block comment runs until it closes.
        if (inBlockComment)
        {
            inBlockComment = line.find("*/") == std::string_view::npos;
        }
        else if (line.starts_with("//") || line.starts_with("*"))
        {
            // Nothing to read.
        }
        else if (line.starts_with("/*"))
        {
            inBlockComment = line.find("*/") == std::string_view::npos;
        }
        else
        {
            for (const char* const word : typeWords)
            {
                if (startsWithWord(line, word) || line.find(std::string(" ") + word + " ") != std::string_view::npos)
                {
                    const std::string_view name = wordAfter(line, word);
                    if (!name.empty() && line.find('(') == std::string_view::npos)
                    {
                        symbols.push_back({std::string(name), number, true});
                    }
                    break;
                }
            }
            // A function: a name followed by its parameters, outside a statement.
            const std::size_t parenthesis = line.find('(');
            if (parenthesis != std::string_view::npos && looksLikeDeclaration(line))
            {
                const std::string_view name = wordBefore(line, parenthesis);
                const bool declared = csharp ? line.find(' ') != std::string_view::npos
                                             : line.find(' ') != std::string_view::npos || line.find("::") != std::string_view::npos;
                if (!name.empty() && declared && !startsWithWord(line, "using"))
                {
                    symbols.push_back({std::string(name), number, false});
                }
            }
        }

        if (stop == std::string_view::npos)
        {
            break;
        }
        index = stop + 1;
    }
}

void readCMake(std::string_view text, std::vector<CodeSymbol>& symbols)
{
    int number = 0;
    for (std::size_t index = 0; index <= text.size();)
    {
        const std::size_t stop = text.find('\n', index);
        const std::string_view line =
            trimmed(text.substr(index, (stop == std::string_view::npos ? text.size() : stop) - index));
        ++number;
        for (const char* const word : {"function", "macro"})
        {
            if (startsWithWord(line, word))
            {
                const std::string_view name = wordAfter(line, word);
                if (!name.empty())
                {
                    symbols.push_back({std::string(name), number, false});
                }
            }
        }
        if (startsWithWord(line, "add_library") || startsWithWord(line, "add_executable"))
        {
            const std::string_view name = wordAfter(line, "(");
            if (!name.empty())
            {
                symbols.push_back({std::string(name), number, true});
            }
        }
        if (stop == std::string_view::npos)
        {
            break;
        }
        index = stop + 1;
    }
}

// The sections of a .dvx* file: [entity ...], [component type="..."] and the like.
void readDevex(std::string_view text, std::vector<CodeSymbol>& symbols)
{
    int number = 0;
    for (std::size_t index = 0; index <= text.size();)
    {
        const std::size_t stop = text.find('\n', index);
        const std::string_view line =
            trimmed(text.substr(index, (stop == std::string_view::npos ? text.size() : stop) - index));
        ++number;
        if (line.starts_with('[') && line.size() > 1)
        {
            // The name of the section, and the name it carries when it has one.
            const std::string_view section = wordAfter(line, "[");
            const std::size_t named = line.find("name=\"");
            std::string label(section);
            if (named != std::string_view::npos)
            {
                const std::size_t begin = named + 6;
                const std::size_t end = line.find('"', begin);
                if (end != std::string_view::npos)
                {
                    label = std::string(line.substr(begin, end - begin));
                }
            }
            if (!label.empty())
            {
                symbols.push_back({std::move(label), number, section == "entity" || section == "scene"});
            }
        }
        if (stop == std::string_view::npos)
        {
            break;
        }
        index = stop + 1;
    }
}

} // namespace

std::vector<CodeSymbol> outlineOf(std::string_view text, CodeLanguage language)
{
    std::vector<CodeSymbol> symbols;
    switch (language)
    {
    case CodeLanguage::Cpp:
        readCurly(text, false, symbols);
        break;
    case CodeLanguage::CSharp:
        readCurly(text, true, symbols);
        break;
    case CodeLanguage::CMake:
        readCMake(text, symbols);
        break;
    case CodeLanguage::DevexText:
        readDevex(text, symbols);
        break;
    case CodeLanguage::Json:
    case CodeLanguage::PlainText:
        break;
    }
    return symbols;
}

} // namespace devex::tools::detail
