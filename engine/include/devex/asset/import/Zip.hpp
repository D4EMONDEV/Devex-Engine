#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace devex::asset {

// A file of a ZIP: its path inside, with forward slashes, and its bytes.
struct DEVEX_API ZipEntry
{
    std::string path;
    std::vector<std::byte> bytes;
};

// Writes the files of a folder into a ZIP, stored as they are, under their paths from the folder: what itch.io takes for
// a game for the web, and what Export makes of an addon.
[[nodiscard]] DEVEX_API core::Result<void> writeZip(const std::filesystem::path& folder, const std::filesystem::path& zipFile);
// The files of a ZIP, stored or compressed with deflate, as most tools make them. Refuses what is encrypted, of 64 bits,
// larger than 1 GB once read, or holds a path that leaves its folder.
[[nodiscard]] DEVEX_API core::Result<std::vector<ZipEntry>> readZip(const std::filesystem::path& zipFile);
// Writes the files of a ZIP under a folder, made for them.
[[nodiscard]] DEVEX_API core::Result<void> extractZip(const std::filesystem::path& zipFile, const std::filesystem::path& folder);

} // namespace devex::asset
