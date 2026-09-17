#pragma once

#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace devex::platform {

// One size of an application icon: square 8-bit RGBA pixels, row by row from the top-left corner.
struct IconImage
{
    std::uint32_t size = 0;
    std::vector<std::uint8_t> rgba;
    // The same pixels encoded as PNG, used instead of the pixels for the largest sizes when given.
    std::vector<std::byte> png;
};

// Replaces the icon that the file manager and the taskbar show for an executable file, which must
// not be running. Sizes from 16 to 256 pixels; Windows only.
[[nodiscard]] core::Result<void> setExecutableIcon(const std::filesystem::path& executable,
                                                   std::span<const IconImage> images);

} // namespace devex::platform
