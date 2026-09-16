#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// The engine exchanges paths as UTF-8 strings. These conversions avoid the narrow string
// functions of std::filesystem, which use the ANSI code page on Windows.
namespace devex::core {

[[nodiscard]] inline std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace devex::core
