#include "LspProtocol.hpp"
#include <devex/core/Path.hpp>
#include <devex/core/String.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>

namespace devex::tools::detail
{
namespace
{
std::pair<std::size_t, int> character(std::string_view text, std::size_t at)
{
    const auto lead = static_cast<unsigned char>(text[at]);
    const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    return {std::min(length, text.size() - at), length == 4 ? 2 : 1};
}
int hex(char ch)
{
    return ch >= '0' && ch <= '9'   ? ch - '0'
           : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
           : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                    : -1;
}
} // namespace

LspPosition lspPosition(std::string_view text, std::size_t byte)
{
    LspPosition position;
    byte = std::min(byte, text.size());
    for (std::size_t at = 0; at < byte;)
    {
        const auto [length, units] = character(text, at);
        if (at + length > byte)
            break;
        if (text[at] == '\n')
        {
            ++position.line;
            position.character = 0;
        }
        else
            position.character += units;
        at += length;
    }
    return position;
}

std::size_t lspOffset(std::string_view text, LspPosition position)
{
    std::size_t at = 0;
    for (int line = 0; line < position.line; ++line)
    {
        const auto end = text.find('\n', at);
        if (end == std::string_view::npos)
            return text.size();
        at = end + 1;
    }
    for (int column = 0; column < position.character && at < text.size() && text[at] != '\n';)
    {
        const auto [length, units] = character(text, at);
        if (column + units > position.character)
            break;
        at += length;
        column += units;
    }
    return at;
}

std::string lspUri(const std::filesystem::path& path)
{
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    std::string file = core::toUtf8(error ? path : absolute);
    std::ranges::replace(file, '\\', '/');
    std::string uri = file.starts_with("//")  ? "file:"
                      : file.starts_with('/') ? "file://"
                                              : "file:///";
    constexpr char digits[] = "0123456789ABCDEF";
    for (unsigned char ch : file)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            std::string_view("/-_.~:").find(static_cast<char>(ch)) != std::string_view::npos)
            uri += static_cast<char>(ch);
        else
        {
            uri += '%';
            uri += digits[ch >> 4];
            uri += digits[ch & 15];
        }
    }
    return uri;
}

std::optional<std::filesystem::path> lspPath(std::string_view uri)
{
    if (!uri.starts_with("file://"))
        return std::nullopt;
    uri.remove_prefix(7);
    std::string path;
    if (uri.starts_with("localhost/"))
        uri.remove_prefix(9);
    if (!uri.starts_with('/'))
        path = "//";
    for (std::size_t at = 0; at < uri.size(); ++at)
    {
        char ch = uri[at];
        if (ch == '%')
        {
            if (at + 2 >= uri.size() || hex(uri[at + 1]) < 0 || hex(uri[at + 2]) < 0)
                return std::nullopt;
            ch = static_cast<char>(hex(uri[at + 1]) * 16 + hex(uri[at + 2]));
            at += 2;
        }
        if (ch == '\0' || ch == '?' || ch == '#')
        {
            // Reserved characters are legal in a filename only when encoded.
            if (ch == '\0' || uri[at] == ch)
                return std::nullopt;
        }
        path += ch;
    }
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
        path.erase(0, 1);
#endif
    return core::pathFromUtf8(path);
}

std::string plainSnippet(std::string_view snippet)
{
    std::string text;
    for (std::size_t at = 0; at < snippet.size(); ++at)
    {
        if (snippet[at] == '\\' && at + 1 < snippet.size())
        {
            text += snippet[++at];
            continue;
        }
        if (snippet[at] != '$')
        {
            text += snippet[at];
            continue;
        }
        if (at + 1 < snippet.size() && std::isdigit(static_cast<unsigned char>(snippet[at + 1])))
        {
            while (at + 1 < snippet.size() &&
                   std::isdigit(static_cast<unsigned char>(snippet[at + 1])))
                ++at;
            continue;
        }
        if (at + 1 < snippet.size() && snippet[at + 1] == '{')
        {
            auto end = at + 2;
            int depth = 1;
            for (; end < snippet.size(); ++end)
            {
                if (snippet[end] == '\\' && end + 1 < snippet.size())
                {
                    ++end;
                    continue;
                }
                if (snippet[end] == '{')
                    ++depth;
                if (snippet[end] == '}' && --depth == 0)
                    break;
            }
            if (end == snippet.size())
                end = std::string_view::npos;
            if (end == std::string_view::npos)
            {
                text += '$';
                continue;
            }
            const auto body = snippet.substr(at + 2, end - at - 2);
            if (const auto colon = body.find(':'); colon != std::string_view::npos)
                text += plainSnippet(body.substr(colon + 1));
            else if (const auto choice = body.find('|'); choice != std::string_view::npos)
            {
                const auto value = body.substr(choice + 1);
                text += value.substr(0, value.find_first_of(",|"));
            }
            at = end;
        }
        else
            text += '$';
    }
    return text;
}

std::string lspFrame(std::string_view json)
{
    return std::format("Content-Length: {}\r\n\r\n{}", json.size(), json);
}

core::Result<void> LspFramer::append(std::string_view bytes)
{
    if (bytes.size() > maximumBytes - m_bytes.size())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The language server message exceeds 8 MiB");
    m_bytes += bytes;
    return {};
}

core::Result<std::optional<std::string>> LspFramer::take()
{
    const auto header = m_bytes.find("\r\n\r\n");
    if (header != std::string::npos && header > 8192)
        return core::makeError(core::ErrorCode::InvalidArgument, "Invalid language server header");
    if (header == std::string::npos)
    {
        if (m_bytes.size() > 8192)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Invalid language server header");
        return std::optional<std::string>{};
    }
    std::optional<std::size_t> length;
    for (std::size_t at = 0; at < header;)
    {
        const auto end = m_bytes.find("\r\n", at);
        const auto line = std::string_view(m_bytes).substr(at, end - at);
        const auto colon = line.find(':');
        std::string name(line.substr(0, colon));
        std::ranges::transform(name, name.begin(), [](unsigned char ch)
                               { return static_cast<char>(std::tolower(ch)); });
        if (name == "content-length" && colon != std::string_view::npos)
        {
            if (length)
                return core::makeError(core::ErrorCode::InvalidArgument,
                                       "Duplicate Content-Length");
            const auto digits = core::trimmed(line.substr(colon + 1));
            std::size_t size = 0;
            const auto [stop, error] =
                std::from_chars(digits.data(), digits.data() + digits.size(), size);
            if (error != std::errc{} || stop != digits.data() + digits.size() ||
                size > maximumBytes - 8192)
                return core::makeError(core::ErrorCode::InvalidArgument, "Invalid Content-Length");
            length = size;
        }
        at = end + 2;
    }
    if (!length)
        return core::makeError(core::ErrorCode::InvalidArgument, "Missing Content-Length");
    const auto end = header + 4 + *length;
    if (m_bytes.size() < end)
        return std::optional<std::string>{};
    std::string message = m_bytes.substr(header + 4, *length);
    m_bytes.erase(0, end);
    return std::optional(std::move(message));
}

} // namespace devex::tools::detail
