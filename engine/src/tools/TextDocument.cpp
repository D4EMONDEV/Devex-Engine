#include "TextDocument.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>

#include <fstream>
#include <span>
#include <string_view>

namespace devex::tools::detail {
namespace {

bool validUtf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0 || (first < 32 && first != '\t' && first != '\r' && first != '\n'))
            return false;
        if (first < 128)
            continue;
        const int count = first >= 0xC2 && first <= 0xDF ? 1 : first >= 0xE0 && first <= 0xEF ? 2 :
                          first >= 0xF0 && first <= 0xF4 ? 3 : -1;
        if (count < 0 || i + count > text.size())
            return false;
        unsigned codepoint = first & ((1u << (6 - count)) - 1);
        for (int byte = 0; byte < count; ++byte)
        {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((count == 1 && codepoint < 0x80) || (count == 2 && codepoint < 0x800) ||
            (count == 3 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            return false;
    }
    return true;
}

core::Result<std::string> readLimited(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return core::makeError(core::ErrorCode::Io, "Cannot read '{}'.", core::toUtf8(path));
    // Bound the read itself, even if another process grows the file while it is open.
    std::string bytes(TextDocument::maximumBytes + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    if (file.bad())
        return core::makeError(core::ErrorCode::Io, "Cannot read '{}'.", core::toUtf8(path));
    if (bytes.size() > TextDocument::maximumBytes)
        return core::makeError(core::ErrorCode::Unsupported, "The text editor supports files up to 2 MiB.");
    return bytes;
}

} // namespace

bool sameTextPath(const std::filesystem::path& a, const std::filesystem::path& b)
{
    if (a.empty() || b.empty())
        return false;
    std::error_code error;
    return a == b || std::filesystem::equivalent(a, b, error);
}

core::Result<TextDocument> TextDocument::open(const std::filesystem::path& path)
{
    auto bytes = readLimited(path);
    if (!bytes)
        return std::unexpected(bytes.error());
    if (!validUtf8(*bytes))
        return core::makeError(core::ErrorCode::Unsupported, "This is not a UTF-8 text file. Open it with an external editor.");
    TextDocument document;
    std::error_code error;
    document.path = std::filesystem::weakly_canonical(path, error);
    if (error)
        return core::makeError(core::ErrorCode::Io, "Cannot resolve '{}': {}", core::toUtf8(path), error.message());
    document.m_diskText = std::move(*bytes);
    document.m_bom = document.m_diskText.starts_with("\xEF\xBB\xBF");
    document.m_crlf = document.m_diskText.find("\r\n") != std::string::npos;
    for (std::size_t i = document.m_bom ? 3 : 0; i < document.m_diskText.size(); ++i)
    {
        const char ch = document.m_diskText[i];
        if (ch == '\r' && i + 1 < document.m_diskText.size() && document.m_diskText[i + 1] == '\n')
            continue;
        document.text += ch;
    }
    document.m_savedText = document.text;
    return document;
}

core::Result<void> TextDocument::save()
{
    if (!modified())
        return {};
    if (!validUtf8(text))
        return core::makeError(core::ErrorCode::Unsupported, "The document must contain UTF-8 text.");
    auto current = readLimited(path);
    if (!current)
        return std::unexpected(current.error());
    if (*current != m_diskText)
        return core::makeError(core::ErrorCode::InvalidState,
                              "The file changed on disk. Copy your changes, then Reload before saving.");
    std::string bytes = m_bom ? "\xEF\xBB\xBF" : "";
    for (const char ch : text)
    {
        if (ch == '\n' && m_crlf)
            bytes += '\r';
        bytes += ch;
    }
    if (bytes.size() > maximumBytes)
        return core::makeError(core::ErrorCode::Unsupported, "The text editor supports files up to 2 MiB.");
    if (auto saved = core::writeFileAtomically(path, std::as_bytes(std::span(bytes.data(), bytes.size()))); !saved)
        return saved;
    m_diskText = std::move(bytes);
    m_savedText = text;
    return {};
}

core::Result<void> TextDocument::reload()
{
    auto loaded = open(path);
    if (!loaded)
        return std::unexpected(loaded.error());
    loaded->revision = revision + 1;
    *this = std::move(*loaded);
    return {};
}

} // namespace devex::tools::detail
