#pragma once

#include <devex/core/Error.hpp>

#include <cstddef>
#include <filesystem>
#include <span>

namespace devex::core {

// A file mapped read-only into memory: its bytes are read from disk as they are touched, and the
// system shares them between processes that map the same file.
class MappedFile
{
public:
    // An empty file maps to no bytes.
    [[nodiscard]] static Result<MappedFile> open(const std::filesystem::path& path);

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
#endif
};

} // namespace devex::core
