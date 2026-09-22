#include <devex/core/Path.hpp>
#include <devex/platform/Executable.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <array>
#include <cstring>
#include <fstream>

namespace devex::platform {

namespace {

// The subsystem field of a Portable Executable: where it is, and what it holds.
constexpr std::uint16_t windowsGuiSubsystem = 2;
constexpr std::uint16_t windowsConsoleSubsystem = 3;

// Where the subsystem sits in the file: after the MS-DOS stub, the "PE" signature, the file header,
// and 68 bytes into the optional header, for 32 and 64-bit images alike.
[[nodiscard]] core::Result<std::streamoff> subsystemOffset(std::istream& file, const std::filesystem::path& path)
{
    std::array<char, 64> dos{};
    file.read(dos.data(), dos.size());
    if (!file || dos[0] != 'M' || dos[1] != 'Z')
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' is not a Windows executable", core::toUtf8(path));
    }
    std::uint32_t peOffset = 0;
    std::memcpy(&peOffset, dos.data() + 0x3C, sizeof(peOffset));
    std::array<char, 4 + 20 + 2> headers{};
    file.seekg(peOffset);
    file.read(headers.data(), headers.size());
    if (!file || headers[0] != 'P' || headers[1] != 'E' || headers[2] != '\0' || headers[3] != '\0')
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' has no PE header", core::toUtf8(path));
    }
    std::uint16_t magic = 0;
    std::memcpy(&magic, headers.data() + 24, sizeof(magic));
    if (magic != 0x10B && magic != 0x20B)
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' has an unknown optional header", core::toUtf8(path));
    }
    return static_cast<std::streamoff>(peOffset) + 24 + 68;
}

} // namespace

core::Result<bool> opensConsole(const std::filesystem::path& executable)
{
    std::ifstream file(executable, std::ios::binary);
    if (!file)
    {
        return core::makeError(core::ErrorCode::NotFound, "cannot read '{}'", core::toUtf8(executable));
    }
    const core::Result<std::streamoff> offset = subsystemOffset(file, executable);
    if (!offset)
    {
        return std::unexpected(offset.error());
    }
    std::uint16_t subsystem = 0;
    file.seekg(*offset);
    file.read(reinterpret_cast<char*>(&subsystem), sizeof(subsystem));
    if (!file)
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' is cut short", core::toUtf8(executable));
    }
    return subsystem == windowsConsoleSubsystem;
}

core::Result<void> setWindowedApplication(const std::filesystem::path& executable)
{
    std::fstream file(executable, std::ios::binary | std::ios::in | std::ios::out);
    if (!file)
    {
        return core::makeError(core::ErrorCode::Io, "cannot open '{}'", core::toUtf8(executable));
    }
    const core::Result<std::streamoff> offset = subsystemOffset(file, executable);
    if (!offset)
    {
        return std::unexpected(offset.error());
    }
    file.seekp(*offset);
    file.write(reinterpret_cast<const char*>(&windowsGuiSubsystem), sizeof(windowsGuiSubsystem));
    if (!file)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(executable));
    }
    return {};
}

#ifdef _WIN32
namespace {

// The layouts of icon resources, as .ico files store them but with resource identifiers.
#pragma pack(push, 2)
struct IconDirectory
{
    WORD reserved = 0;
    // 1 for icons.
    WORD type = 1;
    WORD count = 0;
};

struct IconDirectoryEntry
{
    // 0 stands for 256.
    BYTE width = 0;
    BYTE height = 0;
    BYTE colorCount = 0;
    BYTE reserved = 0;
    WORD planes = 1;
    WORD bitCount = 32;
    DWORD bytes = 0;
    WORD id = 0;
};
#pragma pack(pop)

void append(std::vector<std::byte>& bytes, const auto& value)
{
    const auto* const data = static_cast<const std::byte*>(static_cast<const void*>(&value));
    bytes.insert(bytes.end(), data, data + sizeof(value));
}

// A bitmap icon: a header, the BGRA pixels bottom-up, and an empty 1-bit mask, since alpha is used.
[[nodiscard]] std::vector<std::byte> bitmapIcon(const IconImage& image)
{
    const LONG size = static_cast<LONG>(image.size);
    std::vector<std::byte> bytes;
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = size;
    header.biHeight = size * 2;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    append(bytes, header);
    for (LONG row = size - 1; row >= 0; --row)
    {
        for (LONG column = 0; column < size; ++column)
        {
            const std::size_t pixel = (static_cast<std::size_t>(row) * image.size + static_cast<std::size_t>(column)) * 4;
            bytes.push_back(std::byte{image.rgba[pixel + 2]});
            bytes.push_back(std::byte{image.rgba[pixel + 1]});
            bytes.push_back(std::byte{image.rgba[pixel]});
            bytes.push_back(std::byte{image.rgba[pixel + 3]});
        }
    }
    const std::size_t maskRow = ((image.size + 31) / 32) * 4;
    bytes.resize(bytes.size() + maskRow * image.size, std::byte{0});
    return bytes;
}

} // namespace

core::Result<void> setExecutableIcon(const std::filesystem::path& executable, std::span<const IconImage> images)
{
    if (images.empty() || images.size() > 16)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "an icon needs from 1 to 16 sizes");
    }
    for (const IconImage& image : images)
    {
        if (image.size == 0 || image.size > 256 || image.rgba.size() != std::size_t{image.size} * image.size * 4)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "an icon image of {} pixels is invalid", image.size);
        }
    }

    const HANDLE update = BeginUpdateResourceW(executable.c_str(), FALSE);
    if (update == nullptr)
    {
        return core::makeError(core::ErrorCode::Io, "cannot open the resources of '{}' (error {})", core::toUtf8(executable),
                               GetLastError());
    }
    // RT_ICON and RT_GROUP_ICON, whose macros are narrow strings without UNICODE.
    const WORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    std::vector<std::byte> group;
    append(group, IconDirectory{.count = static_cast<WORD>(images.size())});
    bool written = true;
    WORD id = 1;
    for (const IconImage& image : images)
    {
        std::vector<std::byte> data = image.png.empty() ? bitmapIcon(image) : image.png;
        written = written && UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(id), language, data.data(),
                                             static_cast<DWORD>(data.size())) != FALSE;
        const BYTE side = image.size >= 256 ? 0 : static_cast<BYTE>(image.size);
        append(group, IconDirectoryEntry{.width = side, .height = side, .bytes = static_cast<DWORD>(data.size()), .id = id});
        ++id;
    }
    written = written && UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), language, group.data(),
                                         static_cast<DWORD>(group.size())) != FALSE;
    const DWORD error = written ? 0 : GetLastError();
    if (EndUpdateResourceW(update, written ? FALSE : TRUE) == FALSE || !written)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write the icon of '{}' (error {})", core::toUtf8(executable),
                               written ? GetLastError() : error);
    }
    return {};
}
#else
core::Result<void> setExecutableIcon(const std::filesystem::path& executable, std::span<const IconImage> /*images*/)
{
    return core::makeError(core::ErrorCode::Unsupported, "icons of executables are only set on Windows ('{}')",
                           core::toUtf8(executable));
}
#endif

} // namespace devex::platform
