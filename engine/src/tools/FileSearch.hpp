#pragma once

#include <devex/core/Export.hpp>
#include <string_view>

namespace devex::tools::detail
{

// Every whitespace-separated word must occur in the path/name, regardless of ASCII case.
// Words can appear in any order: "player cs" finds code/PlayerMovement.cs.
[[nodiscard]] DEVEX_API bool matchesFileSearch(std::string_view path,
                                               std::string_view query) noexcept;

} // namespace devex::tools::detail
