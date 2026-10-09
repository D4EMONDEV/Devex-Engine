#include "CodeHighlight.hpp"

#include <devex/asset/import/ShaderFile.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <string>
#include <vector>

namespace devex::tools::detail {
namespace {

constexpr std::array cppKeywords{
    "alignas", "alignof", "auto", "bool", "break", "case", "catch", "char", "class", "concept",
    "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await",
    "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast",
    "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr",
    "operator", "private", "protected", "public", "register", "reinterpret_cast", "requires",
    "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while",
};

constexpr std::array cppTypes{
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "size_t", "ptrdiff_t", "string", "string_view", "vector", "array", "span", "optional",
    "unique_ptr", "shared_ptr", "Vec2", "Vec3", "Vec4", "Quat", "Mat3", "Mat4", "Entity", "Scene",
    "Transform", "AssetId", "Duration", "SystemContext", "Result",
};

constexpr std::array csharpKeywords{
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked",
    "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else",
    "enum", "event", "explicit", "extern", "false", "finally", "fixed", "float", "for",
    "foreach", "get", "goto", "if", "implicit", "in", "int", "interface", "internal", "is",
    "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override",
    "params", "private", "protected", "public", "readonly", "record", "ref", "return", "sbyte",
    "sealed", "set", "short", "sizeof", "stackalloc", "static", "string", "struct", "switch",
    "this", "throw", "true", "try", "typeof", "uint", "ulong", "unsafe", "ushort", "using",
    "var", "virtual", "void", "while",
};

constexpr std::array csharpTypes{
    "Component", "Entity", "Scene", "Transform", "Vec2", "Vec3", "Vec4", "Quat", "AssetId",
    "Uuid", "MathF", "Physics", "Audio", "Animation", "Input", "Time", "Log", "Screen", "Game",
    "Prefabs", "Assets", "RayHit", "Contact", "SystemPhase",
};

constexpr std::array cmakeKeywords{
    "if", "elseif", "else", "endif", "foreach", "endforeach", "while", "endwhile", "function",
    "endfunction", "macro", "endmacro", "return", "break", "set", "unset", "list", "string",
    "PUBLIC", "PRIVATE", "INTERFACE", "REQUIRED", "NOT", "AND", "OR", "TRUE", "FALSE",
};

constexpr std::array shaderKeywords{
    "shader_type", "render_mode", "uniform", "instance", "varying", "if", "else", "for", "while", "do", "return", "break",
    "continue", "discard", "switch", "case", "default", "static", "const", "struct", "in", "out", "inout",
    "true", "false", "let", "var", "void", "import", "typealias", "interface", "extension", "public",
};

constexpr std::array shaderTypeNames{
    "bool", "int", "uint", "float", "half", "double", "float2", "float3", "float4", "int2", "int3", "int4", "uint2",
    "uint3", "uint4", "bool2", "bool3", "bool4", "half2", "half3", "half4", "float2x2", "float3x3", "float4x4",
    "sampler2D",
};

// The types of Slang and the built-ins of every kind of shader, which the editor colours as types.
[[nodiscard]] std::span<const char* const> shaderTypes()
{
    static const std::vector<const char*> names = [] {
        std::vector<const char*> all(shaderTypeNames.begin(), shaderTypeNames.end());
        for (const asset::ShaderKind kind : {asset::ShaderKind::Spatial, asset::ShaderKind::CanvasItem,
                                             asset::ShaderKind::Particles, asset::ShaderKind::Sky})
        {
            for (const std::string_view name : asset::shaderBuiltins(kind))
            {
                // The built-ins are literals, ended by a null character.
                if (std::ranges::none_of(all, [&](const char* known) { return name == known; }))
                {
                    all.push_back(name.data());
                }
            }
        }
        return all;
    }();
    return names;
}

[[nodiscard]] bool isIdentifierStart(char value) noexcept
{
    return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] bool isIdentifierPart(char value) noexcept
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] bool contains(std::span<const char* const> names, std::string_view word) noexcept
{
    return std::ranges::find(names, word) != names.end();
}

void add(std::vector<Token>& tokens, std::size_t begin, std::size_t end, TokenKind kind)
{
    if (end > begin && kind != TokenKind::Text)
    {
        tokens.push_back({static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(end), kind});
    }
}

// The languages that share C's comments, strings and numbers: C++ and C#.
void highlightCurly(std::string_view line, std::span<const char* const> keywords,
                    std::span<const char* const> types, bool csharp, HighlightState& state,
                    std::vector<Token>& tokens)
{
    std::size_t index = 0;
    while (index < line.size())
    {
        if (state.inBlockComment)
        {
            const std::size_t end = line.find("*/", index);
            const std::size_t stop = end == std::string_view::npos ? line.size() : end + 2;
            add(tokens, index, stop, TokenKind::Comment);
            state.inBlockComment = end == std::string_view::npos;
            index = stop;
            continue;
        }
        const char current = line[index];
        if (current == '/' && index + 1 < line.size() && line[index + 1] == '/')
        {
            add(tokens, index, line.size(), TokenKind::Comment);
            return;
        }
        if (current == '/' && index + 1 < line.size() && line[index + 1] == '*')
        {
            const std::size_t end = line.find("*/", index + 2);
            const std::size_t stop = end == std::string_view::npos ? line.size() : end + 2;
            add(tokens, index, stop, TokenKind::Comment);
            state.inBlockComment = end == std::string_view::npos;
            index = stop;
            continue;
        }
        if (current == '"' || current == '\'')
        {
            const std::size_t begin = index;
            const char quote = current;
            ++index;
            while (index < line.size() && line[index] != quote)
            {
                index += line[index] == '\\' ? 2 : 1;
            }
            index = std::min(index + 1, line.size());
            add(tokens, begin, index, TokenKind::String);
            continue;
        }
        if (current == '#' && !csharp)
        {
            // A preprocessor line, up to any trailing comment.
            const std::size_t comment = line.find("//", index);
            const std::size_t stop = comment == std::string_view::npos ? line.size() : comment;
            add(tokens, index, stop, TokenKind::Directive);
            index = stop;
            continue;
        }
        if (current == '[' && csharp && index + 1 < line.size() && isIdentifierStart(line[index + 1]))
        {
            // An attribute, such as [AssetType("audio")].
            const std::size_t stop = std::min(line.find(']', index) + 1, line.size());
            add(tokens, index, stop, TokenKind::Directive);
            index = stop;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(current)) != 0)
        {
            const std::size_t begin = index;
            while (index < line.size() &&
                   (isIdentifierPart(line[index]) || line[index] == '.' ||
                    ((line[index] == '+' || line[index] == '-') && (line[index - 1] == 'e' || line[index - 1] == 'E'))))
            {
                ++index;
            }
            add(tokens, begin, index, TokenKind::Number);
            continue;
        }
        if (isIdentifierStart(current))
        {
            const std::size_t begin = index;
            while (index < line.size() && isIdentifierPart(line[index]))
            {
                ++index;
            }
            const std::string_view word = line.substr(begin, index - begin);
            const TokenKind kind = contains(keywords, word)  ? TokenKind::Keyword
                                   : contains(types, word)   ? TokenKind::Type
                                                             : TokenKind::Text;
            add(tokens, begin, index, kind);
            continue;
        }
        if (std::ispunct(static_cast<unsigned char>(current)) != 0)
        {
            add(tokens, index, index + 1, TokenKind::Punctuation);
        }
        ++index;
    }
}

void highlightCMake(std::string_view line, std::vector<Token>& tokens)
{
    std::size_t index = 0;
    while (index < line.size())
    {
        const char current = line[index];
        if (current == '#')
        {
            add(tokens, index, line.size(), TokenKind::Comment);
            return;
        }
        if (current == '"')
        {
            const std::size_t begin = index++;
            while (index < line.size() && line[index] != '"')
            {
                index += line[index] == '\\' ? 2 : 1;
            }
            index = std::min(index + 1, line.size());
            add(tokens, begin, index, TokenKind::String);
            continue;
        }
        if (current == '$' && index + 1 < line.size() && line[index + 1] == '{')
        {
            const std::size_t stop = std::min(line.find('}', index) + 1, line.size());
            add(tokens, index, stop, TokenKind::Type);
            index = stop;
            continue;
        }
        if (isIdentifierStart(current))
        {
            const std::size_t begin = index;
            while (index < line.size() && isIdentifierPart(line[index]))
            {
                ++index;
            }
            const std::string_view word = line.substr(begin, index - begin);
            // A name followed by a parenthesis is a command; the known words are keywords.
            const bool command = index < line.size() && line[index] == '(';
            add(tokens, begin, index,
                command ? TokenKind::Directive
                        : contains(cmakeKeywords, word) ? TokenKind::Keyword : TokenKind::Text);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(current)) != 0)
        {
            const std::size_t begin = index;
            while (index < line.size() && (isIdentifierPart(line[index]) || line[index] == '.'))
            {
                ++index;
            }
            add(tokens, begin, index, TokenKind::Number);
            continue;
        }
        if (std::ispunct(static_cast<unsigned char>(current)) != 0)
        {
            add(tokens, index, index + 1, TokenKind::Punctuation);
        }
        ++index;
    }
}

// JSON, and the .dvx* text format: sections in brackets, keys, strings, numbers and calls.
void highlightData(std::string_view line, bool devex, std::vector<Token>& tokens)
{
    std::size_t index = 0;
    while (index < line.size())
    {
        const char current = line[index];
        if (devex && current == '#')
        {
            add(tokens, index, line.size(), TokenKind::Comment);
            return;
        }
        if (current == '"')
        {
            const std::size_t begin = index++;
            while (index < line.size() && line[index] != '"')
            {
                index += line[index] == '\\' ? 2 : 1;
            }
            index = std::min(index + 1, line.size());
            add(tokens, begin, index, TokenKind::String);
            continue;
        }
        if (devex && current == '[')
        {
            const std::size_t begin = index + 1;
            std::size_t stop = begin;
            while (stop < line.size() && isIdentifierPart(line[stop]))
            {
                ++stop;
            }
            add(tokens, index, index + 1, TokenKind::Punctuation);
            add(tokens, begin, stop, TokenKind::Type);
            index = stop;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(current)) != 0 ||
            (current == '-' && index + 1 < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[index + 1])) != 0))
        {
            const std::size_t begin = index++;
            while (index < line.size() && (isIdentifierPart(line[index]) || line[index] == '.'))
            {
                ++index;
            }
            add(tokens, begin, index, TokenKind::Number);
            continue;
        }
        if (isIdentifierStart(current))
        {
            const std::size_t begin = index;
            while (index < line.size() && isIdentifierPart(line[index]))
            {
                ++index;
            }
            const std::string_view word = line.substr(begin, index - begin);
            const bool call = index < line.size() && line[index] == '(';
            const bool literal = word == "true" || word == "false" || word == "null";
            add(tokens, begin, index,
                call ? TokenKind::Directive : literal ? TokenKind::Keyword : TokenKind::Text);
            continue;
        }
        if (std::ispunct(static_cast<unsigned char>(current)) != 0)
        {
            add(tokens, index, index + 1, TokenKind::Punctuation);
        }
        ++index;
    }
}

} // namespace

CodeLanguage languageOf(const std::filesystem::path& path)
{
    const std::string name = core::toUtf8(path.filename());
    std::string extension = core::toUtf8(path.extension());
    std::ranges::transform(extension, extension.begin(),
                           [](char value) { return static_cast<char>(std::tolower(static_cast<unsigned char>(value))); });
    if (extension == ".cpp" || extension == ".hpp" || extension == ".h" || extension == ".c" ||
        extension == ".cc" || extension == ".inl" || extension == ".slang")
    {
        return CodeLanguage::Cpp;
    }
    if (extension == ".cs")
    {
        return CodeLanguage::CSharp;
    }
    if (extension == ".cmake" || name == "CMakeLists.txt")
    {
        return CodeLanguage::CMake;
    }
    if (extension == ".json" || extension == ".csproj")
    {
        return CodeLanguage::Json;
    }
    if (extension == ".dvxshader")
    {
        return CodeLanguage::Shader;
    }
    if (extension.starts_with(".dvx"))
    {
        return CodeLanguage::DevexText;
    }
    return CodeLanguage::PlainText;
}

std::string_view toString(CodeLanguage language) noexcept
{
    switch (language)
    {
    case CodeLanguage::Cpp:
        return "C++";
    case CodeLanguage::CSharp:
        return "C#";
    case CodeLanguage::CMake:
        return "CMake";
    case CodeLanguage::Json:
        return "JSON";
    case CodeLanguage::DevexText:
        return "Devex";
    case CodeLanguage::Shader:
        return "Shader";
    case CodeLanguage::PlainText:
        break;
    }
    return "Text";
}

std::vector<Token> highlightLine(std::string_view line, CodeLanguage language, HighlightState& state)
{
    std::vector<Token> tokens;
    switch (language)
    {
    case CodeLanguage::Cpp:
        highlightCurly(line, cppKeywords, cppTypes, false, state, tokens);
        break;
    case CodeLanguage::CSharp:
        highlightCurly(line, csharpKeywords, csharpTypes, true, state, tokens);
        break;
    case CodeLanguage::CMake:
        highlightCMake(line, tokens);
        break;
    case CodeLanguage::Json:
        highlightData(line, false, tokens);
        break;
    case CodeLanguage::DevexText:
        highlightData(line, true, tokens);
        break;
    case CodeLanguage::Shader:
        highlightCurly(line, shaderKeywords, shaderTypes(), false, state, tokens);
        break;
    case CodeLanguage::PlainText:
        break;
    }
    return tokens;
}

std::string_view lineCommentOf(CodeLanguage language) noexcept
{
    switch (language)
    {
    case CodeLanguage::Cpp:
    case CodeLanguage::CSharp:
    case CodeLanguage::Shader:
        return "//";
    case CodeLanguage::CMake:
    case CodeLanguage::DevexText:
        return "#";
    case CodeLanguage::Json:
    case CodeLanguage::PlainText:
        break;
    }
    return "";
}

} // namespace devex::tools::detail
