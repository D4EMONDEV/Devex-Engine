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

// Whether a Windows executable opens a console window when it is started from the file manager.
[[nodiscard]] core::Result<bool> opensConsole(const std::filesystem::path& executable);
// Makes a Windows executable a windowed application: started from the file manager, it opens no
// console window, and what it prints goes nowhere, so it must keep a log file. Only the header of
// the file changes; it works from any system, since it reads and writes bytes.
[[nodiscard]] core::Result<void> setWindowedApplication(const std::filesystem::path& executable);

} // namespace devex::platform
