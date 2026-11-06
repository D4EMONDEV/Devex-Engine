#include "SdlWindow.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <bit>
#include <string>
#include <string_view>
#include <utility>

namespace devex::platform {

Window::Window(detail::NativeWindow* native) noexcept
    : m_native(native)
{
}

Window::Window(Window&& other) noexcept
    : m_native(std::exchange(other.m_native, nullptr))
    , m_tookDisplay(other.m_tookDisplay)
{
}

Window& Window::operator=(Window&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_native = std::exchange(other.m_native, nullptr);
        m_tookDisplay = other.m_tookDisplay;
    }
    return *this;
}

Window::~Window()
{
    destroy();
}

void Window::destroy() noexcept
{
    if (m_native != nullptr)
    {
        SDL_DestroyWindow(detail::toSdlWindow(m_native));
        m_native = nullptr;
    }
}

WindowId Window::id() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return WindowId{SDL_GetWindowID(detail::toSdlWindow(m_native))};
}

math::Extent2D Window::size() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return detail::windowSize(detail::toSdlWindow(m_native));
}

math::Extent2D Window::pixelSize() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return detail::windowPixelSize(detail::toSdlWindow(m_native));
}

bool Window::isMinimized() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_MINIMIZED) != 0;
}

bool Window::isMaximized() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool Window::isHidden() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_HIDDEN) != 0;
}

float Window::displayScale() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    const float scale = SDL_GetWindowDisplayScale(detail::toSdlWindow(m_native));
    return scale > 0.0f ? scale : 1.0f;
}

void Window::setSize(math::Extent2D size)
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Window* const window = detail::toSdlWindow(m_native);
    SDL_RestoreWindow(window);
    SDL_SetWindowSize(window, static_cast<int>(size.width), static_cast<int>(size.height));
}

ScreenArea Window::usableArea() const
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Rect bounds{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(detail::toSdlWindow(m_native)), &bounds))
    {
        return {};
    }
    return {.x = bounds.x, .y = bounds.y, .width = static_cast<std::uint32_t>(bounds.w), .height = static_cast<std::uint32_t>(bounds.h)};
}

void Window::fitInto(const ScreenArea& area)
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Window* const window = detail::toSdlWindow(m_native);
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    // Not every system tells before the window is shown: without it, the frame goes a little over.
    static_cast<void>(SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right));
    SDL_RestoreWindow(window);
    SDL_SetWindowSize(window, std::max(static_cast<int>(area.width) - left - right, 1), std::max(static_cast<int>(area.height) - top - bottom, 1));
    SDL_SetWindowPosition(window, area.x + left, area.y + top);
    SDL_SyncWindow(window);
}

void Window::center()
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_SetWindowPosition(detail::toSdlWindow(m_native), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void Window::centerOver(const Window& other)
{
    DEVEX_ASSERT(m_native != nullptr);
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    SDL_Window* const window = detail::toSdlWindow(m_native);
    SDL_Window* const below = detail::toSdlWindow(other.m_native);
    if (!SDL_GetWindowPosition(below, &x, &y) || !SDL_GetWindowSize(below, &width, &height))
    {
        return;
    }
    int ownWidth = 0;
    int ownHeight = 0;
    SDL_GetWindowSize(window, &ownWidth, &ownHeight);
    SDL_SetWindowPosition(window, x + (width - ownWidth) / 2, y + (height - ownHeight) / 2);
}

void Window::setOwner(const Window& owner, bool modal)
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Window* const window = detail::toSdlWindow(m_native);
    if (!SDL_SetWindowParent(window, detail::toSdlWindow(owner.m_native)) || !SDL_SetWindowModal(window, modal))
    {
        DEVEX_LOG_WARNING("A window stays on its own: {}", SDL_GetError());
    }
#ifdef _WIN32
    if (const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)))
    {
        SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) & ~static_cast<LONG_PTR>(WS_MINIMIZEBOX));
    }
#endif
}

bool Window::hasKeyboardFocus() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool Window::hasMouseFocus() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

void Window::maximize()
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_MaximizeWindow(detail::toSdlWindow(m_native));
}

void Window::restore()
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_RestoreWindow(detail::toSdlWindow(m_native));
}

void Window::setFullscreen(bool fullscreen)
{
    setMode(fullscreen ? WindowMode::Borderless : WindowMode::Windowed);
}

void Window::setMode(WindowMode mode)
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Window* const window = detail::toSdlWindow(m_native);
    m_tookDisplay = mode == WindowMode::Fullscreen;
    if (mode == WindowMode::Windowed)
    {
        SDL_SetWindowFullscreen(window, false);
        return;
    }
    // No mode of its own is what SDL calls borderless; the display's own mode gives it the display, at the same resolution.
    const SDL_DisplayMode* const display = mode == WindowMode::Fullscreen ? SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window)) : nullptr;
    SDL_SetWindowFullscreenMode(window, display);
    SDL_SetWindowFullscreen(window, true);
}

WindowMode Window::mode() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    if ((SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_FULLSCREEN) == 0)
    {
        return WindowMode::Windowed;
    }
    return m_tookDisplay ? WindowMode::Fullscreen : WindowMode::Borderless;
}

bool Window::isFullscreen() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::setTitleBarColors([[maybe_unused]] bool dark, [[maybe_unused]] math::Vec3 caption)
{
    DEVEX_ASSERT(m_native != nullptr);
#ifdef _WIN32
    const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(detail::toSdlWindow(m_native)),
                                                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr)
    {
        return;
    }
    // Attributes unknown to older versions of Windows are ignored there.
    const BOOL useDark = dark ? TRUE : FALSE;
    static_cast<void>(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark)));
    const auto channel = [](float value) {
        return static_cast<BYTE>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const COLORREF color = RGB(channel(caption.r), channel(caption.g), channel(caption.b));
    static_cast<void>(DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &color, sizeof(color)));
#endif
}

void Window::setIcon(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height)
{
    DEVEX_ASSERT(m_native != nullptr);
    DEVEX_ASSERT(rgba.size() == std::size_t{width} * height * 4);
    SDL_Surface* const surface = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
                                                       SDL_PIXELFORMAT_ABGR8888, const_cast<std::uint8_t*>(rgba.data()),
                                                       static_cast<int>(width * 4));
    if (surface == nullptr)
    {
        DEVEX_LOG_WARNING("Cannot set the window icon: {}", SDL_GetError());
        return;
    }
    if (!SDL_SetWindowIcon(detail::toSdlWindow(m_native), surface))
    {
        // Wayland compositors without the protocol of window icons, as GNOME, show the icon of the
        // program's desktop entry instead (registerDesktopEntry); windows without a screen have none,
        // and Android shows the icon of the APK.
        const char* const driver = SDL_GetCurrentVideoDriver();
        const std::string_view name = driver != nullptr ? driver : "";
        if (name == "wayland" || name == "offscreen" || name == "dummy" || name == "android")
        {
            DEVEX_LOG_TRACE("The window has the icon of the desktop entry: {}", SDL_GetError());
        }
        else
        {
            DEVEX_LOG_WARNING("Cannot set the window icon: {}", SDL_GetError());
        }
    }
    SDL_DestroySurface(surface);
}

void Window::setTitle(std::string_view title)
{
    DEVEX_ASSERT(m_native != nullptr);
    const std::string terminatedTitle(title);
    SDL_SetWindowTitle(detail::toSdlWindow(m_native), terminatedTitle.c_str());
}

void Window::setMouseMode(MouseMode mode)
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Window* const window = detail::toSdlWindow(m_native);
    const bool confined = mode == MouseMode::Confined || mode == MouseMode::ConfinedHidden;
    const bool hidden = mode == MouseMode::Hidden || mode == MouseMode::ConfinedHidden;
    // While captured, SDL hides the cursor by itself and brings it back after.
    if (!SDL_SetWindowRelativeMouseMode(window, mode == MouseMode::Captured) ||
        !SDL_SetWindowMouseGrab(window, confined) || !(hidden ? SDL_HideCursor() : SDL_ShowCursor()))
    {
        DEVEX_LOG_WARNING("Cannot change what the mouse does: {}", SDL_GetError());
    }
}

MouseMode Window::mouseMode() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Window* const window = detail::toSdlWindow(m_native);
    if (SDL_GetWindowRelativeMouseMode(window))
    {
        return MouseMode::Captured;
    }
    // What was asked: SDL_GetWindowMouseGrab says false while the window does not have the focus.
    const bool confined = (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_GRABBED) != 0;
    if (SDL_CursorVisible())
    {
        return confined ? MouseMode::Confined : MouseMode::Visible;
    }
    return confined ? MouseMode::ConfinedHidden : MouseMode::Hidden;
}

void Window::setMouseCaptured(bool captured)
{
    setMouseMode(captured ? MouseMode::Captured : MouseMode::Visible);
}

bool Window::isMouseCaptured() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return SDL_GetWindowRelativeMouseMode(detail::toSdlWindow(m_native));
}

core::Result<std::uint64_t> Window::createVulkanSurface(void* vulkanInstance) const
{
    DEVEX_ASSERT(m_native != nullptr);
    DEVEX_ASSERT(vulkanInstance != nullptr);

    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(detail::toSdlWindow(m_native),
                                  static_cast<VkInstance>(vulkanInstance), nullptr, &surface))
    {
        return core::makeError(core::ErrorCode::Platform, "cannot create a Vulkan surface: {}",
                               SDL_GetError());
    }
    return std::bit_cast<std::uint64_t>(surface);
}

} // namespace devex::platform
