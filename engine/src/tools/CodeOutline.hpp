#pragma once

#include "CodeHighlight.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace devex::tools::detail {

// A name the outline of a file lists: a type, a function, or a section of a Devex file.
struct CodeSymbol
{
    std::string name;
    // Counting from one, as the editor numbers its lines.
    int line = 1;
    // A type, a class or a section, rather than a function: shown with its own icon.
    bool type = false;

    bool operator==(const CodeSymbol&) const = default;
};

// The types and functions a file declares, in the order they appear. The reading is by shape
// rather than by grammar: it follows indentation, keywords and parentheses, which is enough to
// jump around a file and wrong only on unusual formatting.
[[nodiscard]] std::vector<CodeSymbol> outlineOf(std::string_view text, CodeLanguage language);

} // namespace devex::tools::detail
