#pragma once

#include <devex/core/Error.hpp>

#include <filesystem>
#include <string>

namespace devex::tools::detail {

// A UTF-8 file, with an LF editing buffer and its original bytes for conflict detection.
// Disk contents are only replaced after a successful save; reload also commits on success only.
class TextDocument
{
public:
    static constexpr std::size_t maximumBytes = 2 * 1024 * 1024;

    [[nodiscard]] static core::Result<TextDocument> open(const std::filesystem::path& path);
    [[nodiscard]] core::Result<void> save();
    [[nodiscard]] core::Result<void> reload();
    [[nodiscard]] bool modified() const noexcept { return text != m_savedText; }
    [[nodiscard]] bool crlf() const noexcept { return m_crlf; }
    [[nodiscard]] bool bom() const noexcept { return m_bom; }

    std::filesystem::path path;
    std::string text;
    std::string error;
    int line = 1;
    int column = 1;
    // Changing the widget ID on reload discards ImGui's previous edit/undo buffer.
    unsigned revision = 0;

private:
    std::string m_savedText;
    std::string m_diskText;
    bool m_crlf = false;
    bool m_bom = false;
};

[[nodiscard]] bool sameTextPath(const std::filesystem::path& a, const std::filesystem::path& b);

} // namespace devex::tools::detail
