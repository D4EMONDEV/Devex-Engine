#pragma once

#include <devex/platform/Window.hpp>

#include <SDL3/SDL_video.h>

#include <algorithm>

namespace devex::platform::detail {

// Window stores the SDL window behind an opaque pointer so that public headers stay free of SDL.
[[nodiscard]] inline SDL_Window* toSdlWindow(NativeWindow* window) noexcept
{
    return reinterpret_cast<SDL_Window*>(window);
}

[[nodiscard]] inline NativeWindow* toNativeWindow(SDL_Window* window) noexcept
{
    return reinterpret_cast<NativeWindow*>(window);
}

[[nodiscard]] inline math::Extent2D toExtent(int width, int height) noexcept
{
    return {static_cast<std::uint32_t>(std::max(width, 0)),
            static_cast<std::uint32_t>(std::max(height, 0))};
}

[[nodiscard]] inline math::Extent2D windowSize(SDL_Window* window) noexcept
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    return toExtent(width, height);
}

[[nodiscard]] inline math::Extent2D windowPixelSize(SDL_Window* window) noexcept
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    return toExtent(width, height);
}

} // namespace devex::platform::detail
