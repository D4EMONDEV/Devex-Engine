#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace devex::tools::detail
{

struct LspPosition
{
    int line = 0;
    int character = 0;
    bool operator==(const LspPosition&) const = default;
};

// LSP positions count UTF-16 code units; Devex's editor offsets count UTF-8 bytes.
[[nodiscard]] DEVEX_API LspPosition lspPosition(std::string_view text, std::size_t byte);
[[nodiscard]] DEVEX_API std::size_t lspOffset(std::string_view text, LspPosition position);
[[nodiscard]] DEVEX_API std::string lspUri(const std::filesystem::path& path);
[[nodiscard]] DEVEX_API std::optional<std::filesystem::path> lspPath(std::string_view uri);
[[nodiscard]] DEVEX_API std::string plainSnippet(std::string_view snippet);
[[nodiscard]] DEVEX_API std::string lspFrame(std::string_view json);

// Reads complete Content-Length frames even when headers, multibyte characters or bodies are split.
class DEVEX_API LspFramer
{
  public:
    static constexpr std::size_t maximumBytes = 8 * 1024 * 1024;
    [[nodiscard]] core::Result<void> append(std::string_view bytes);
    [[nodiscard]] core::Result<std::optional<std::string>> take();

  private:
    std::string m_bytes;
};

} // namespace devex::tools::detail
