#pragma once

#include <string_view>

namespace devex::core
{

// Returns a view without the requested leading/trailing characters, leaving the source intact.
// Spaces and tabs are the default; line-oriented readers can also discard a trailing CR.
[[nodiscard]] constexpr std::string_view trimmed(std::string_view text,
                                                 std::string_view leading = " \t",
                                                 std::string_view trailing = " \t") noexcept
{
    const std::size_t first = text.find_first_not_of(leading);
    if (first == std::string_view::npos)
        return {};
    text.remove_prefix(first);
    const std::size_t last = text.find_last_not_of(trailing);
    return last == std::string_view::npos ? std::string_view{} : text.substr(0, last + 1);
}

} // namespace devex::core
