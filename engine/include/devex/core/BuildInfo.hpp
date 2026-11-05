#pragma once

#include <devex/core/Export.hpp>

#include <string_view>

namespace devex::core {

[[nodiscard]] DEVEX_API std::string_view version() noexcept;

// Negative when a version such as "1.2.3" comes before another, positive when after, 0 when they are the same. A
// missing part counts as 0, and what is not a version comes before every version.
[[nodiscard]] DEVEX_API int compareVersions(std::string_view left, std::string_view right) noexcept;

// The CMake configuration the engine was built with, such as "Debug", which game modules share.
[[nodiscard]] DEVEX_API std::string_view buildType() noexcept;

} // namespace devex::core
