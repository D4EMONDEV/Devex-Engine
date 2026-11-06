#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace devex::platform {

// A resource of a Windows executable: a type and a name, each a number unless it has text.
struct DEVEX_API ExecutableResource
{
    std::uint32_t type = 0;
    std::u16string typeName;
    std::uint32_t id = 0;
    std::u16string name;
    std::uint32_t language = 0;
    std::uint32_t codePage = 0;
    std::vector<std::byte> data;
};
[[nodiscard]] DEVEX_API core::Result<std::vector<ExecutableResource>> readExecutableResources(const std::filesystem::path& executable);

// One size of an application icon: square 8-bit RGBA pixels, row by row from the top-left corner.
struct DEVEX_API IconImage
{
    std::uint32_t size = 0;
    std::vector<std::uint8_t> rgba;
    // The same pixels encoded as PNG, used instead of the pixels for the largest sizes when given.
    std::vector<std::byte> png;
};

// Replaces the icon that the file manager and the taskbar show for a Windows executable, which must
// not be running. Sizes from 16 to 256 pixels. It works from any system, since it reads and writes bytes.
[[nodiscard]] DEVEX_API core::Result<void> setExecutableIcon(const std::filesystem::path& executable,
                                                             std::span<const IconImage> images);

// Whether a Windows executable opens a console window when it is started from the file manager.
[[nodiscard]] DEVEX_API core::Result<bool> opensConsole(const std::filesystem::path& executable);
// Makes a Windows executable a windowed application: started from the file manager, it opens no
// console window, and what it prints goes nowhere, so it must keep a log file. Only the header of
// the file changes; it works from any system, since it reads and writes bytes.
[[nodiscard]] DEVEX_API core::Result<void> setWindowedApplication(const std::filesystem::path& executable);

} // namespace devex::platform
