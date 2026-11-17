#include <devex/asset/import/Zip.hpp>

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
// The decoder of deflate that stb_image has for PNG, compiled in StbImage.cpp.
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace devex::asset {

namespace {

// What a ZIP may hold once read: more is refused rather than filling the memory.
constexpr std::uint64_t readLimit = 1024ULL * 1024ULL * 1024ULL;

// The CRC-32 of ZIP, of IEEE 802.3.
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t index = 0; index < values.size(); ++index)
        {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit)
            {
                value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1) : value >> 1;
            }
            values[index] = value;
        }
        return values;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes)
    {
        crc = table[(crc ^ std::to_integer<std::uint32_t>(byte)) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

void put16(std::string& out, std::uint32_t value)
{
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8) & 0xFFU));
}

void put32(std::string& out, std::uint32_t value)
{
    put16(out, value & 0xFFFFU);
    put16(out, value >> 16);
}

[[nodiscard]] std::uint32_t read16(std::span<const std::byte> bytes, std::size_t at) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[at]) | (std::to_integer<std::uint32_t>(bytes[at + 1]) << 8);
}

[[nodiscard]] std::uint32_t read32(std::span<const std::byte> bytes, std::size_t at) noexcept
{
    return read16(bytes, at) | (read16(bytes, at + 2) << 16);
}

// A path inside the ZIP that stays inside the folder it is extracted to: relative, without "..", with forward slashes.
[[nodiscard]] bool safePath(std::string& path)
{
    std::ranges::replace(path, '\\', '/');
    if (path.empty() || path.front() == '/' || (path.size() > 1 && path[1] == ':'))
    {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t end = std::min(path.find('/', start), path.size());
        if (path.compare(start, end - start, "..") == 0)
        {
            return false;
        }
        start = end + 1;
    }
    return true;
}

} // namespace

core::Result<void> writeZip(const std::filesystem::path& folder, const std::filesystem::path& zipFile)
{
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error))
    {
        if (entry->is_regular_file(error))
        {
            files.push_back(entry->path());
        }
    }
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "cannot read '{}': {}", core::toUtf8(folder), error.message());
    }
    std::ranges::sort(files);
    std::filesystem::create_directories(zipFile.parent_path(), error);
    std::ofstream out(zipFile, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(zipFile));
    }
    // Every entry stored as it is, dated 1 January 1980, the first day ZIP knows: its own header, then its bytes, and
    // the directory at the end that names them all.
    std::string directory;
    std::uint32_t offset = 0;
    for (const std::filesystem::path& file : files)
    {
        // Names use forward slashes, whatever the system.
        std::string name = core::toUtf8(file.lexically_relative(folder));
        std::ranges::replace(name, '\\', '/');
        std::ifstream in(file, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() > 0xFFFFFFFFULL || offset > 0xFFFFFFFFU - bytes.size() - name.size() - 64)
        {
            return core::makeError(core::ErrorCode::Unsupported, "'{}' is too large for a ZIP without its 64-bit extension", name);
        }
        const std::uint32_t crc = crc32(std::as_bytes(std::span(bytes)));
        const auto size = static_cast<std::uint32_t>(bytes.size());
        const auto header = [&](std::string& into, bool central) {
            put32(into, central ? 0x02014B50U : 0x04034B50U);
            if (central)
            {
                put16(into, 20);
            }
            put16(into, 20);
            // UTF-8 names.
            put16(into, 0x0800);
            put16(into, 0);
            put16(into, 0);
            put16(into, (1 << 5) | 1);
            put32(into, crc);
            put32(into, size);
            put32(into, size);
            put16(into, static_cast<std::uint32_t>(name.size()));
            put16(into, 0);
            if (central)
            {
                put16(into, 0);
                put16(into, 0);
                put16(into, 0);
                put32(into, 0);
                put32(into, offset);
            }
            into += name;
        };
        std::string local;
        header(local, false);
        header(directory, true);
        out.write(local.data(), static_cast<std::streamsize>(local.size()));
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        offset += static_cast<std::uint32_t>(local.size()) + size;
    }
    std::string end;
    put32(end, 0x06054B50U);
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<std::uint32_t>(files.size()));
    put16(end, static_cast<std::uint32_t>(files.size()));
    put32(end, static_cast<std::uint32_t>(directory.size()));
    put32(end, offset);
    put16(end, 0);
    out.write(directory.data(), static_cast<std::streamsize>(directory.size()));
    out.write(end.data(), static_cast<std::streamsize>(end.size()));
    if (!out)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(zipFile));
    }
    return {};
}

core::Result<std::vector<ZipEntry>> readZip(const std::filesystem::path& zipFile)
{
    const core::Result<std::vector<std::byte>> read = core::readBinaryFile(zipFile);
    if (!read)
    {
        return std::unexpected(read.error());
    }
    const std::span<const std::byte> bytes = *read;
    const auto damaged = [&] {
        return core::makeError(core::ErrorCode::Parse, "{} is not a ZIP, or a damaged one", core::toUtf8(zipFile.filename()));
    };
    // The end of its directory, before a comment of up to 65535 bytes.
    constexpr std::size_t endSize = 22;
    if (bytes.size() < endSize)
    {
        return damaged();
    }
    std::size_t end = bytes.size() - endSize;
    const std::size_t earliest = bytes.size() > endSize + 0xFFFF ? bytes.size() - endSize - 0xFFFF : 0;
    while (read32(bytes, end) != 0x06054B50U)
    {
        if (end == earliest)
        {
            return damaged();
        }
        --end;
    }
    const std::uint32_t count = read16(bytes, end + 10);
    const std::uint32_t directorySize = read32(bytes, end + 12);
    const std::uint32_t directoryStart = read32(bytes, end + 16);
    if (count == 0xFFFF || directoryStart == 0xFFFFFFFFU)
    {
        return core::makeError(core::ErrorCode::Unsupported, "{} uses the 64-bit extension of ZIP, which Devex does not read",
                               core::toUtf8(zipFile.filename()));
    }
    if (static_cast<std::uint64_t>(directoryStart) + directorySize > end)
    {
        return damaged();
    }

    std::vector<ZipEntry> entries;
    std::uint64_t total = 0;
    std::size_t at = directoryStart;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (at + 46 > end || read32(bytes, at) != 0x02014B50U)
        {
            return damaged();
        }
        const std::uint32_t flags = read16(bytes, at + 8);
        const std::uint32_t method = read16(bytes, at + 10);
        const std::uint32_t crc = read32(bytes, at + 16);
        const std::uint32_t packed = read32(bytes, at + 20);
        const std::uint32_t size = read32(bytes, at + 24);
        const std::size_t nameLength = read16(bytes, at + 28);
        const std::size_t extraLength = read16(bytes, at + 30);
        const std::size_t commentLength = read16(bytes, at + 32);
        const std::size_t local = read32(bytes, at + 42);
        if (at + 46 + nameLength > end)
        {
            return damaged();
        }
        std::string path(reinterpret_cast<const char*>(bytes.data() + at + 46), nameLength);
        at += 46 + nameLength + extraLength + commentLength;
        if (path.ends_with('/') || path.ends_with('\\'))
        {
            continue;
        }
        if (!safePath(path))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "{} holds a path that leaves its folder: {}",
                                   core::toUtf8(zipFile.filename()), path);
        }
        if ((flags & 1U) != 0)
        {
            return core::makeError(core::ErrorCode::Unsupported, "{} is encrypted", core::toUtf8(zipFile.filename()));
        }
        total += size;
        if (total > readLimit)
        {
            return core::makeError(core::ErrorCode::Unsupported, "{} holds more than 1 GB", core::toUtf8(zipFile.filename()));
        }
        if (local + 30 > bytes.size() || read32(bytes, local) != 0x04034B50U)
        {
            return damaged();
        }
        const std::size_t data = local + 30 + read16(bytes, local + 26) + read16(bytes, local + 28);
        if (data + packed > bytes.size())
        {
            return damaged();
        }
        ZipEntry entry{.path = std::move(path), .bytes = std::vector<std::byte>(size)};
        if (method == 0 && packed == size)
        {
            std::ranges::copy(bytes.subspan(data, packed), entry.bytes.begin());
        }
        else if (method == 8 && size == 0)
        {
            // An empty file: nothing to inflate.
        }
        else if (method == 8)
        {
            const int inflated = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(entry.bytes.data()), static_cast<int>(size),
                                                                  reinterpret_cast<const char*>(bytes.data() + data), static_cast<int>(packed));
            if (inflated != static_cast<int>(size))
            {
                return damaged();
            }
        }
        else
        {
            return core::makeError(core::ErrorCode::Unsupported, "{} is compressed with a method Devex does not read: {}",
                                   core::toUtf8(zipFile.filename()), method);
        }
        if (crc32(entry.bytes) != crc)
        {
            return damaged();
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

core::Result<void> extractZip(const std::filesystem::path& zipFile, const std::filesystem::path& folder)
{
    core::Result<std::vector<ZipEntry>> entries = readZip(zipFile);
    if (!entries)
    {
        return std::unexpected(entries.error());
    }
    for (const ZipEntry& entry : *entries)
    {
        const std::filesystem::path file = folder / core::pathFromUtf8(entry.path);
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (core::Result<void> written = core::writeFileAtomically(file, entry.bytes); !written)
        {
            return written;
        }
    }
    return {};
}

} // namespace devex::asset
