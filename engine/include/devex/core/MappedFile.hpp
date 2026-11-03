#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>

#include <cstddef>
#include <filesystem>
#include <span>

namespace devex::core {

// A file mapped read-only into memory: its bytes are read from disk as they are touched, and the
// system shares them between processes that map the same file.
class DEVEX_API MappedFile
{
public:
    // An empty file maps to no bytes.
    [[nodiscard]] static Result<MappedFile> open(const std::filesystem::path& path);
#ifndef _WIN32
    // Maps `size` bytes from `offset` of an open file, whose descriptor the mapping then owns: on
    // Android, a file stored uncompressed inside the APK.
    [[nodiscard]] static Result<MappedFile> openRegion(int descriptor, std::uint64_t offset, std::size_t size);
#endif

    MappedFile() noexcept = default;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    void close() noexcept;

    const std::byte* m_data = nullptr;
    std::size_t m_size = 0;
#ifdef _WIN32
    void* m_file = nullptr;
    void* m_mapping = nullptr;
#else
    int m_descriptor = -1;
    // Where the bytes start in the mapped pages, which begin on a page boundary.
    std::size_t m_pageOffset = 0;
#endif
};

} // namespace devex::core
