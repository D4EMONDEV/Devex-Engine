#pragma once

#include <devex/core/Export.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace devex::runtime {

// What to tell someone whose editor just crashed, from what its log says went wrong.
[[nodiscard]] DEVEX_API std::string crashMessageText(std::string_view log, const std::filesystem::path& logFile);

// devex-editor --crash-message, which the crash handler starts: the process that crashed is in no state to open a window. Shows
// the message, with a button for the folder of the report, and returns once it is answered.
DEVEX_API void showCrashMessage(const std::filesystem::path& logFile);

} // namespace devex::runtime
