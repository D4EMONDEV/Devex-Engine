#pragma once

#include <devex/core/Error.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace devex::core {

// Reads a whole file as bytes, without newline conversion.
[[nodiscard]] Result<std::string> readTextFile(const std::filesystem::path& path);

// Replaces the file contents, creating the parent directories when needed.
[[nodiscard]] Result<void> writeTextFile(const std::filesystem::path& path, std::string_view text);

} // namespace devex::core
