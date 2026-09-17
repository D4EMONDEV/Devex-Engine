#pragma once

#include <string_view>

namespace devex::core {

[[nodiscard]] std::string_view version() noexcept;

// The CMake configuration the engine was built with, such as "Debug", which game modules share.
[[nodiscard]] std::string_view buildType() noexcept;

} // namespace devex::core
