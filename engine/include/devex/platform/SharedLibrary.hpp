#pragma once

#include <devex/core/Error.hpp>

#include <filesystem>
#include <string>

namespace devex::platform {

// A dynamic library loaded into the process, such as a game module, unloaded when destroyed.
class SharedLibrary
{
public:
    [[nodiscard]] static core::Result<SharedLibrary> load(const std::filesystem::path& path);

    SharedLibrary(SharedLibrary&& other) noexcept;
    SharedLibrary& operator=(SharedLibrary&& other) noexcept;
    ~SharedLibrary();

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    // The address of an exported function, or null.
    [[nodiscard]] void* function(const std::string& name) const;

    // True when the address belongs to the library's code or data, such as a function or a
    // variable it defines.
    [[nodiscard]] bool contains(const void* address) const noexcept;

private:
    SharedLibrary(std::filesystem::path path, void* handle) noexcept;
    void unload() noexcept;

    std::filesystem::path m_path;
    void* m_handle = nullptr;
};

} // namespace devex::platform
