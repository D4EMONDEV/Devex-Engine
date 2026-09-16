#pragma once

#include <chrono>

namespace devex::core {

// Time span handed to gameplay code, in seconds.
using Duration = std::chrono::duration<double>;

} // namespace devex::core
