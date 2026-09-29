#pragma once

#include <devex/core/Export.hpp>

#include <string_view>

namespace devex::core {

[[nodiscard]] DEVEX_API std::string_view version() noexcept;

// The CMake configuration the engine was built with, such as "Debug", which game modules share.
[[nodiscard]] DEVEX_API std::string_view buildType() noexcept;

} // namespace devex::core
