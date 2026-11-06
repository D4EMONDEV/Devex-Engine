#include "GameArchive.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace devex::runtime::detail {
namespace {

constexpr std::size_t blockSize = 512;
constexpr std::size_t recordSize = 10240;
constexpr std::size_t nameSize = 100;
constexpr std::size_t prefixSize = 155;

// A number as tar writes them: in octal, ended by nothing else than the end of its field.
void writeOctal(char* field, std::size_t size, std::uint64_t value)
{
    std::snprintf(field, size, "%0*llo", static_cast<int>(size - 1), static_cast<unsigned long long>(value));
}

// A path that does not fit in the name goes partly in the prefix, cut at a slash.
[[nodiscard]] bool writeName(std::array<char, blockSize>& header, const std::string& path)
{
    if (path.size() <= nameSize)
    {
        std::memcpy(header.data(), path.data(), path.size());
        return true;
    }
    for (std::size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
    {
        if (slash <= prefixSize && path.size() - slash - 1 <= nameSize)
        {
            std::memcpy(header.data(), path.data() + slash + 1, path.size() - slash - 1);
            std::memcpy(header.data() + 345, path.data(), slash);
            return true;
        }
    }
    return false;
}

[[nodiscard]] core::Result<void> writeHeader(std::ofstream& out, const std::string& path, std::uint64_t size, bool directory,
                                             bool executable, std::time_t now)
{
    std::array<char, blockSize> header{};
    if (!writeName(header, path))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the path '{}' is too long for an archive", path);
    }
    writeOctal(header.data() + 100, 8, directory || executable ? 0755 : 0644);
    writeOctal(header.data() + 108, 8, 0);
    writeOctal(header.data() + 116, 8, 0);
    writeOctal(header.data() + 124, 12, size);
    writeOctal(header.data() + 136, 12, static_cast<std::uint64_t>(now));
    header[156] = directory ? '5' : '0';
    std::memcpy(header.data() + 257, "ustar", 6);
    std::memcpy(header.data() + 263, "00", 2);
    // Whoever extracts it owns the files; without these names, the tar of Windows complains about every file.
    std::memcpy(header.data() + 265, "root", 4);
    std::memcpy(header.data() + 297, "root", 4);
    // The sum counts its own field as spaces.
    std::memset(header.data() + 148, ' ', 8);
    std::uint32_t sum = 0;
    for (const char byte : header)
    {
        sum += static_cast<unsigned char>(byte);
    }
    std::snprintf(header.data() + 148, 8, "%06o", sum);
    header[155] = ' ';
    out.write(header.data(), blockSize);
    return {};
}

[[nodiscard]] bool isElfFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::array<char, 4> magic{};
    in.read(magic.data(), magic.size());
    return in.gcount() == 4 && std::memcmp(magic.data(), "\x7F" "ELF", 4) == 0;
}

} // namespace

core::Result<void> writeTar(const std::filesystem::path& folder, std::string_view root, const std::filesystem::path& tarFile)
{
    std::error_code error;
    std::vector<std::filesystem::path> entries;
    for (std::filesystem::recursive_directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error))
    {
        entries.push_back(entry->path());
    }
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "cannot read '{}': {}", core::toUtf8(folder), error.message());
    }
    std::ranges::sort(entries);
    std::filesystem::create_directories(tarFile.parent_path(), error);
    std::ofstream out(tarFile, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(tarFile));
    }
    const std::time_t now = std::time(nullptr);
    const std::string base(root);
    if (core::Result<void> written = writeHeader(out, base + "/", 0, true, false, now); !written)
    {
        return written;
    }
    std::vector<char> buffer(1024 * 1024);
    for (const std::filesystem::path& path : entries)
    {
        const std::u8string relative = path.lexically_relative(folder).generic_u8string();
        std::string name = base + "/" + std::string(relative.begin(), relative.end());
        if (std::filesystem::is_directory(path, error))
        {
            if (core::Result<void> written = writeHeader(out, name + "/", 0, true, false, now); !written)
            {
                return written;
            }
            continue;
        }
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error)
        {
            return core::makeError(core::ErrorCode::Io, "cannot read '{}': {}", core::toUtf8(path), error.message());
        }
        if (core::Result<void> written = writeHeader(out, name, size, false, isElfFile(path), now); !written)
        {
            return written;
        }
        std::ifstream in(path, std::ios::binary);
        std::uintmax_t left = size;
        while (left > 0 && in)
        {
            in.read(buffer.data(), static_cast<std::streamsize>(std::min<std::uintmax_t>(left, buffer.size())));
            const auto read = static_cast<std::uintmax_t>(in.gcount());
            out.write(buffer.data(), static_cast<std::streamsize>(read));
            left -= read;
        }
        if (left != 0)
        {
            return core::makeError(core::ErrorCode::Io, "cannot read '{}'", core::toUtf8(path));
        }
        const std::size_t padding = (blockSize - static_cast<std::size_t>(size % blockSize)) % blockSize;
        const std::array<char, blockSize> zeros{};
        out.write(zeros.data(), static_cast<std::streamsize>(padding));
    }
    // Two empty blocks end it, and tools expect whole records.
    const std::array<char, blockSize> zeros{};
    out.write(zeros.data(), blockSize);
    out.write(zeros.data(), blockSize);
    const auto length = static_cast<std::uintmax_t>(out.tellp());
    for (std::uintmax_t written = length; written % recordSize != 0; written += blockSize)
    {
        out.write(zeros.data(), blockSize);
    }
    out.close();
    if (!out)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(tarFile));
    }
    return {};
}

core::Result<std::filesystem::path> packGame(const std::filesystem::path& folder, std::string_view root,
                                             const std::filesystem::path& archive)
{
    std::filesystem::path tarFile = archive;
    tarFile.replace_extension();
    if (core::Result<void> written = writeTar(folder, root, tarFile); !written)
    {
        return std::unexpected(written.error());
    }
#ifdef _WIN32
    // By its full path: another tar on the PATH may not read archives as files, nor a C: in their names.
    const std::string program = platform::environmentVariable("SystemRoot") + "\\System32\\tar.exe";
#else
    const std::string program = "tar";
#endif
    std::error_code error;
    // Left to itself, the tar of Windows adds headers that the tar of Linux warns about, file after file.
    if (core::Result<void> packed = platform::runAndLog({program, "--format=gnutar", "-czf", core::toUtf8(archive), "@" + core::toUtf8(tarFile)},
                                                        tarFile.parent_path(), "cannot compress the archive");
        !packed)
    {
        std::filesystem::remove(archive, error);
        DEVEX_LOG_WARNING("The game stays an uncompressed archive: {}", packed.error());
        return tarFile;
    }
    std::filesystem::remove(tarFile, error);
    return archive;
}

} // namespace devex::runtime::detail
