#include <devex/core/Path.hpp>
#include <devex/platform/SharedLibrary.hpp>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_loadso.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <utility>

namespace devex::platform {

core::Result<SharedLibrary> SharedLibrary::load(const std::filesystem::path& path)
{
    const std::string utf8 = core::toUtf8(path);
    SDL_SharedObject* const handle = SDL_LoadObject(utf8.c_str());
    if (handle == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot load '{}': {}", utf8, SDL_GetError());
    }
    return SharedLibrary(path, handle);
}

SharedLibrary::SharedLibrary(std::filesystem::path path, void* handle) noexcept
    : m_path(std::move(path))
    , m_handle(handle)
{
}

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept
    : m_path(std::move(other.m_path))
    , m_handle(std::exchange(other.m_handle, nullptr))
{
}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept
{
    if (this != &other)
    {
        unload();
        m_path = std::move(other.m_path);
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

SharedLibrary::~SharedLibrary()
{
    unload();
}

void SharedLibrary::unload() noexcept
{
    if (m_handle != nullptr)
    {
        SDL_UnloadObject(static_cast<SDL_SharedObject*>(std::exchange(m_handle, nullptr)));
    }
}

const std::filesystem::path& SharedLibrary::path() const noexcept
{
    return m_path;
}

void* SharedLibrary::function(const std::string& name) const
{
    if (m_handle == nullptr)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(SDL_LoadFunction(static_cast<SDL_SharedObject*>(m_handle), name.c_str()));
}

bool SharedLibrary::contains(const void* address) const noexcept
{
    if (m_handle == nullptr || address == nullptr)
    {
        return false;
    }
#ifdef _WIN32
    HMODULE module = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    return GetModuleHandleExW(flags, static_cast<LPCWSTR>(address), &module) != 0 &&
           module == static_cast<HMODULE>(m_handle);
#else
    // The library loaded from the file the address belongs to, if it is loaded: a library opened by
    // its short name, such as libc.so.6, has no path to compare with.
    Dl_info info{};
    if (dladdr(address, &info) == 0 || info.dli_fname == nullptr)
    {
        return false;
    }
    void* const found = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD);
    if (found == nullptr)
    {
        return false;
    }
    dlclose(found);
    return found == m_handle;
#endif
}

} // namespace devex::platform
