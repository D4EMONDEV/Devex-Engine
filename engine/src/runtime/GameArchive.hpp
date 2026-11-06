#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>

#include <filesystem>
#include <string_view>

namespace devex::runtime::detail {

// Writes a folder as a tar file, under one folder of that name. Linux programs and libraries, known by their first bytes,
// come out executable, which a folder written under Windows could not say.
[[nodiscard]] DEVEX_API core::Result<void> writeTar(const std::filesystem::path& folder, std::string_view root,
                                                    const std::filesystem::path& tarFile);

// Packs a folder into a .tar.gz with the tar of the system. Without one, it keeps the plain .tar, which extracts just the
// same. Returns the file it made.
[[nodiscard]] DEVEX_API core::Result<std::filesystem::path> packGame(const std::filesystem::path& folder, std::string_view root,
                                                                     const std::filesystem::path& archive);

} // namespace devex::runtime::detail
