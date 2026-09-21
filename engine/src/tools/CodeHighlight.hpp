#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace devex::tools::detail {

// What the text editor colors a run of characters as.
enum class TokenKind : std::uint8_t
{
    Text,
    Keyword,
    // Built-in and engine types, and the names of sections in Devex files.
    Type,
    Comment,
    String,
    Number,
    // #include and the like in C++, attributes in C#, functions in CMake.
    Directive,
    Punctuation,
};

// A run of one kind inside one line, as byte offsets into that line.
struct Token
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    TokenKind kind = TokenKind::Text;
};

// The languages the editor knows. Anything else is left uncolored.
enum class CodeLanguage : std::uint8_t
{
    PlainText,
    Cpp,
    CSharp,
    CMake,
    Json,
    // Scenes, materials, projects and import settings: the .dvx* text format.
    DevexText,
};

// The language of a file, from its name and extension.
[[nodiscard]] CodeLanguage languageOf(const std::filesystem::path& path);

[[nodiscard]] std::string_view toString(CodeLanguage language) noexcept;

// What a line leaves behind for the next one: a block comment or a raw string that goes on.
struct HighlightState
{
    bool inBlockComment = false;

    bool operator==(const HighlightState&) const = default;
};

// The tokens of one line, in order, without the runs that carry no color. The state is read and
// updated, so lines must be highlighted in order.
[[nodiscard]] std::vector<Token> highlightLine(std::string_view line, CodeLanguage language,
                                               HighlightState& state);

// The text that starts a line comment, empty when the language has none: what Ctrl+/ inserts.
[[nodiscard]] std::string_view lineCommentOf(CodeLanguage language) noexcept;

} // namespace devex::tools::detail
