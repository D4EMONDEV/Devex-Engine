#pragma once

#include <devex/core/Export.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace devex::runtime {

// What a bug report needs to know about a machine, in words: the engine, the machine and its GPUs as given, the tools
// that build game code, and what the log in this folder last complained about. Nothing leaves the machine by itself:
// the user reads it and sends it, or not.
[[nodiscard]] DEVEX_API std::string systemReport(std::string_view machine, std::string_view graphics,
                                                 const std::filesystem::path& logs);

// devex-editor --report [file]
// Writes the report, beside the logs of the editor unless told where, and prints it. It opens no visible window and
// asks for no renderer, for the machines that cannot make one. Returns the exit code of the process.
[[nodiscard]] DEVEX_API int reportFromCommandLine(std::span<const std::string_view> arguments);

} // namespace devex::runtime
