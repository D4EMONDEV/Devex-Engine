#include <devex/core/MappedFile.hpp>
#include <devex/core/Path.hpp>

#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace devex::core {

Result<MappedFile> MappedFile::open(const std::filesystem::path& path)
{
    MappedFile mapped;
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return makeError(ErrorCode::NotFound, "cannot open '{}'", toUtf8(path));
    }
    mapped.m_file = file;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size))
    {
        return makeError(ErrorCode::Io, "cannot read the size of '{}'", toUtf8(path));
    }
    mapped.m_size = static_cast<std::size_t>(size.QuadPart);
    if (mapped.m_size == 0)
    {
        return mapped;
    }
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr)
    {
        return makeError(ErrorCode::Io, "cannot map '{}'", toUtf8(path));
    }
    mapped.m_mapping = mapping;
    const LPVOID view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr)
    {
        return makeError(ErrorCode::Io, "cannot map '{}'", toUtf8(path));
    }
    mapped.m_data = static_cast<const std::byte*>(view);
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
    {
        return makeError(ErrorCode::NotFound, "cannot open '{}'", toUtf8(path));
    }
    mapped.m_descriptor = descriptor;
    struct stat status{};
    if (fstat(descriptor, &status) != 0)
    {
        return makeError(ErrorCode::Io, "cannot read the size of '{}'", toUtf8(path));
    }
    mapped.m_size = static_cast<std::size_t>(status.st_size);
    if (mapped.m_size == 0)
    {
        return mapped;
    }
    void* const view = mmap(nullptr, mapped.m_size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (view == MAP_FAILED)
    {
        return makeError(ErrorCode::Io, "cannot map '{}'", toUtf8(path));
    }
    mapped.m_data = static_cast<const std::byte*>(view);
#endif
    return mapped;
}

#ifndef _WIN32
Result<MappedFile> MappedFile::openRegion(int descriptor, std::uint64_t offset, std::size_t size)
{
    MappedFile mapped;
    mapped.m_descriptor = descriptor;
    mapped.m_size = size;
    if (size == 0)
    {
        return mapped;
    }
    const auto page = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    const std::uint64_t start = offset - offset % page;
    mapped.m_pageOffset = static_cast<std::size_t>(offset - start);
    void* const view = mmap(nullptr, size + mapped.m_pageOffset, PROT_READ, MAP_PRIVATE, descriptor, static_cast<off_t>(start));
    if (view == MAP_FAILED)
    {
        return makeError(ErrorCode::Io, "cannot map {} bytes at {}", size, offset);
    }
    mapped.m_data = static_cast<const std::byte*>(view) + mapped.m_pageOffset;
    return mapped;
}
#endif

MappedFile::MappedFile(MappedFile&& other) noexcept
    : m_data(std::exchange(other.m_data, nullptr))
    , m_size(std::exchange(other.m_size, 0))
#ifdef _WIN32
    , m_file(std::exchange(other.m_file, nullptr))
    , m_mapping(std::exchange(other.m_mapping, nullptr))
#else
    , m_descriptor(std::exchange(other.m_descriptor, -1))
    , m_pageOffset(std::exchange(other.m_pageOffset, 0))
#endif
{
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept
{
    if (this != &other)
    {
        close();
        m_data = std::exchange(other.m_data, nullptr);
        m_size = std::exchange(other.m_size, 0);
#ifdef _WIN32
        m_file = std::exchange(other.m_file, nullptr);
        m_mapping = std::exchange(other.m_mapping, nullptr);
#else
        m_descriptor = std::exchange(other.m_descriptor, -1);
        m_pageOffset = std::exchange(other.m_pageOffset, 0);
#endif
    }
    return *this;
}

MappedFile::~MappedFile()
{
    close();
}

std::span<const std::byte> MappedFile::bytes() const noexcept
{
    return {m_data, m_data != nullptr ? m_size : 0};
}

void MappedFile::close() noexcept
{
#ifdef _WIN32
    if (m_data != nullptr)
    {
        UnmapViewOfFile(m_data);
    }
    if (m_mapping != nullptr)
    {
        CloseHandle(m_mapping);
    }
    if (m_file != nullptr)
    {
        CloseHandle(m_file);
    }
    m_mapping = nullptr;
    m_file = nullptr;
#else
    if (m_data != nullptr)
    {
        munmap(const_cast<std::byte*>(m_data - m_pageOffset), m_size + m_pageOffset);
    }
    m_pageOffset = 0;
    if (m_descriptor >= 0)
    {
        ::close(m_descriptor);
    }
    m_descriptor = -1;
#endif
    m_data = nullptr;
    m_size = 0;
}

} // namespace devex::core
