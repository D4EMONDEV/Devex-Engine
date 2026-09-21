#pragma once

#include "CodeHighlight.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::tools::detail {

// A name the editor offers while typing.
struct CompletionItem
{
    std::string text;
    // Where it comes from, shown beside the name: "keyword", "type", "component", "field" or
    // "in file".
    std::string detail;

    bool operator==(const CompletionItem&) const = default;
};

// The names the engine knows for a language: its components and their fields, taken from the
// reflection registry, plus the types of its API.
[[nodiscard]] std::vector<CompletionItem> engineNames(CodeLanguage language);

// The identifier that ends at a position, which a completion replaces. Empty when the character
// before the position is not part of an identifier.
[[nodiscard]] std::string_view identifierBefore(std::string_view text, std::size_t position);

// What can be typed after a prefix: the names given, the keywords of the language and the other
// identifiers of the file, closest first, without repeats. An empty prefix offers nothing, and the
// word being typed at the cursor is not offered as a completion of itself.
[[nodiscard]] std::vector<CompletionItem> completionsFor(std::string_view prefix, CodeLanguage language,
                                                         std::string_view text,
                                                         std::span<const CompletionItem> names,
                                                         std::size_t cursor = std::string_view::npos,
                                                         std::size_t limit = 12);

} // namespace devex::tools::detail
