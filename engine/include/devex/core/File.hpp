#pragma once

#include <devex/core/Error.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::core {

// Reads a whole file as bytes, without newline conversion.
[[nodiscard]] Result<std::string> readTextFile(const std::filesystem::path& path);

// Replaces the file contents, creating the parent directories when needed.
[[nodiscard]] Result<void> writeTextFile(const std::filesystem::path& path, std::string_view text);

[[nodiscard]] Result<std::vector<std::byte>> readBinaryFile(const std::filesystem::path& path);

// Writes the bytes to a temporary file next to the destination, then renames it over the
// destination: readers see either the previous contents or the new ones, never a partial file.
[[nodiscard]] Result<void> writeFileAtomically(const std::filesystem::path& path,
                                               std::span<const std::byte> bytes);

} // namespace devex::core
