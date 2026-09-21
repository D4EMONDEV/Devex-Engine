#include "CodeCompletion.hpp"

#include <devex/reflection/Reflection.hpp>
#include <devex/scene/ComponentRegistry.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace devex::tools::detail {
namespace {

// The words every file of a language may use, beside the names of the engine.
constexpr std::array cppWords{
    "auto",   "const",     "constexpr", "class",   "struct", "enum",      "namespace", "template",
    "return", "if",        "else",      "for",     "while",  "switch",    "case",      "break",
    "continue", "nullptr", "true",      "false",   "static", "inline",    "virtual",   "override",
    "public", "private",   "protected", "include", "float",  "double",    "bool",      "void",
};

constexpr std::array csharpWords{
    "var",    "class",  "struct",  "enum",    "interface", "namespace", "using",   "public",
    "private", "protected", "internal", "static", "readonly", "override", "virtual", "return",
    "if",     "else",   "foreach", "for",     "while",     "switch",    "case",    "break",
    "continue", "new",  "null",    "true",    "false",     "float",     "int",     "bool",
    "string", "void",   "ref",     "out",     "this",      "base",      "delegate", "event",
};

// What a C# or C++ game file reaches for most often, beside the components of the project.
constexpr std::array csharpApi{
    "Component", "Entity",  "Scene",     "Transform", "Vec2",    "Vec3",    "Vec4",    "Quat",
    "AssetId",   "Physics", "Audio",     "Animation", "Input",   "Time",    "Log",     "Screen",
    "Game",      "Prefabs", "Assets",    "RayHit",    "Contact", "Update",  "Start",   "OnCollisionEnter",
    "OnTriggerEnter", "OnTriggerExit", "GameSystem", "AssetType", "AudioGroup", "PhysicsLayer", "Angle", "Color",
};

constexpr std::array cppApi{
    "SystemContext", "SystemPhase",  "Entity",       "Scene",          "Transform",
    "WorldTransform", "MeshRenderer", "RigidBody",   "AssetId",        "Duration",
    "DEVEX_REFLECT", "DEVEX_DECLARE_REFLECTION",     "DEVEX_LOG_INFO", "DEVEX_LOG_WARNING",
    "DEVEX_LOG_ERROR", "devex::math", "devex::scene", "devex::runtime",
};

[[nodiscard]] bool isIdentifierPart(char value) noexcept
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] char lowered(char value) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](char left, char right) { return lowered(left) == lowered(right); });
}

void addWords(std::vector<CompletionItem>& items, std::span<const char* const> words, const char* detail)
{
    for (const char* const word : words)
    {
        items.push_back({word, detail});
    }
}

} // namespace

std::vector<CompletionItem> engineNames(CodeLanguage language)
{
    std::vector<CompletionItem> items;
    if (language != CodeLanguage::Cpp && language != CodeLanguage::CSharp)
    {
        return items;
    }
    addWords(items, language == CodeLanguage::CSharp ? std::span<const char* const>(csharpApi)
                                                     : std::span<const char* const>(cppApi),
             "type");

    // The components of the project and of the engine, with the fields the inspector shows.
    std::unordered_set<std::string> seen;
    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        const std::string name(type.name());
        if (seen.insert(name).second)
        {
            items.push_back({name, "component"});
        }
        if (type.type == nullptr)
        {
            continue;
        }
        for (const reflection::FieldInfo& field : type.type->fields)
        {
            if (seen.insert(field.name).second)
            {
                items.push_back({field.name, "field"});
            }
        }
    }
    return items;
}

std::string_view identifierBefore(std::string_view text, std::size_t position)
{
    const std::size_t end = std::min(position, text.size());
    std::size_t begin = end;
    while (begin > 0 && isIdentifierPart(text[begin - 1]))
    {
        --begin;
    }
    // A name never starts with a digit: 3d is not the beginning of an identifier.
    if (begin < end && std::isdigit(static_cast<unsigned char>(text[begin])) != 0)
    {
        return {};
    }
    return text.substr(begin, end - begin);
}

std::vector<CompletionItem> completionsFor(std::string_view prefix, CodeLanguage language,
                                           std::string_view text, std::span<const CompletionItem> names,
                                           std::size_t cursor, std::size_t limit)
{
    std::vector<CompletionItem> candidates;
    if (prefix.empty())
    {
        return candidates;
    }
    candidates.assign(names.begin(), names.end());
    if (language == CodeLanguage::Cpp)
    {
        addWords(candidates, cppWords, "keyword");
    }
    else if (language == CodeLanguage::CSharp)
    {
        addWords(candidates, csharpWords, "keyword");
    }

    // The identifiers of the file itself: what the author already wrote nearby.
    for (std::size_t index = 0; index < text.size();)
    {
        if (!isIdentifierPart(text[index]) || std::isdigit(static_cast<unsigned char>(text[index])) != 0)
        {
            ++index;
            continue;
        }
        const std::size_t begin = index;
        while (index < text.size() && isIdentifierPart(text[index]))
        {
            ++index;
        }
        const std::string_view word = text.substr(begin, index - begin);
        // The word the cursor stands in is what is being typed, not a completion of it.
        const bool atCursor = cursor >= begin && cursor <= index;
        if (word.size() > 2 && !atCursor && startsWithIgnoringCase(word, prefix))
        {
            candidates.push_back({std::string(word), "in file"});
        }
    }

    std::vector<CompletionItem> matches;
    std::unordered_set<std::string> seen;
    // What is being typed is not a completion of itself.
    seen.insert(std::string(prefix));
    for (CompletionItem& candidate : candidates)
    {
        if (startsWithIgnoringCase(candidate.text, prefix) && seen.insert(candidate.text).second)
        {
            matches.push_back(std::move(candidate));
        }
    }

    // Names that match the case of the prefix first, then the shortest, then in order.
    std::ranges::stable_sort(matches, [prefix](const CompletionItem& left, const CompletionItem& right) {
        const bool leftExact = left.text.starts_with(prefix);
        const bool rightExact = right.text.starts_with(prefix);
        if (leftExact != rightExact)
        {
            return leftExact;
        }
        if (left.text.size() != right.text.size())
        {
            return left.text.size() < right.text.size();
        }
        return left.text < right.text;
    });
    if (matches.size() > limit)
    {
        matches.resize(limit);
    }
    return matches;
}

} // namespace devex::tools::detail
