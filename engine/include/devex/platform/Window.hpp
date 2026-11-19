#pragma once

#include <devex/platform/WindowMode.hpp>
#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace devex::platform {

enum class WindowId : std::uint32_t
{
};

// What a WebGPU surface is made from: the window of the system, or in a browser its canvas.
struct DEVEX_API NativeWindow
{
    enum class Kind : std::uint8_t
    {
        None,
        Win32,
        X11,
        Wayland,
        // The canvas SDL draws into, "#canvas".
        Canvas,
        // The ANativeWindow of the app, which Android takes away while the app is in the background and gives again,
        // another, when it comes back.
        Android,
    };
    Kind kind = Kind::None;
    // Win32: the HINSTANCE; X11: the Display; Wayland: the wl_display.
    void* display = nullptr;
    // Win32: the HWND; Wayland: the wl_surface; Android: the ANativeWindow.
    void* window = nullptr;
    // X11: the Window.
    std::uint64_t x11Window = 0;
};

struct DEVEX_API WindowConfig
{
    std::string title = "Devex";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    // Required to create a Vulkan surface for the window.
    bool vulkan = false;
    // Required to draw into the window with OpenGL. A window of Android serves Vulkan or OpenGL, not both.
    bool openGl = false;
    bool hidden = false;
    // Borderless over the whole display, at the resolution of the desktop.
    bool fullscreen = false;
    // Without a frame or a title bar, at its size.
    bool borderless = false;
};

namespace detail {
struct NativeWindow;
} // namespace detail

// A part of a display, in the coordinates of windows.
struct DEVEX_API ScreenArea
{
    int x = 0;
    int y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// An operating system window, created by Platform::createWindow. It must be destroyed before the
// Platform that created it.
// What the mouse does over a window.
enum class MouseMode : std::uint8_t
{
    // The cursor shows and goes where it wants.
    Visible,
    // No cursor over the window, as in a game played with the keyboard. The mouse still moves and clicks.
    Hidden,
    // No cursor, and motion without bounds, as first-person cameras need.
    Captured,
    // The cursor shows and stays inside the window.
    Confined,
    // No cursor, and the mouse stays inside the window.
    ConfinedHidden,
};

class DEVEX_API Window
{
public:
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] WindowId id() const noexcept;
    // Size in window coordinates, which differ from pixels on high-density displays.
    [[nodiscard]] math::Extent2D size() const noexcept;
    // Size of the drawable area in pixels, as used by a swapchain.
    [[nodiscard]] math::Extent2D pixelSize() const noexcept;
    [[nodiscard]] bool isMinimized() const noexcept;
    [[nodiscard]] bool isMaximized() const noexcept;
    [[nodiscard]] bool isHidden() const noexcept;
    // Content scale of the display the window is on: 1.5 when the system scales it to 150 %.
    [[nodiscard]] float displayScale() const noexcept;

    void setTitle(std::string_view title);
    // Resizes the window, in window coordinates, and puts it back to its normal state first.
    void setSize(math::Extent2D size);
    // Centers the window on its display.
    void center();
    // In the middle of another window.
    void centerOver(const Window& other);
    // Stays above another window, as a dialog does. Modal, the other waits until this one closes.
    // On Windows, it then has no button to minimize, as a window that depends on another has nowhere to go.
    void setOwner(const Window& owner, bool modal);
    [[nodiscard]] bool hasKeyboardFocus() const noexcept;
    [[nodiscard]] bool hasMouseFocus() const noexcept;
    // What the display it is on leaves to windows, without the task bars and the like.
    [[nodiscard]] ScreenArea usableArea() const;
    // Fills a part of a display, its frame included.
    void fitInto(const ScreenArea& area);
    void maximize();
    // Leaves the maximized or minimized state.
    void restore();
    // Borderless over the whole display, at the resolution of the desktop.
    void setFullscreen(bool fullscreen);
    // Over the whole display, borderless or not.
    [[nodiscard]] bool isFullscreen() const noexcept;
    void setMode(WindowMode mode);
    [[nodiscard]] WindowMode mode() const noexcept;
    // Matches the title bar drawn by the system to the application's colors, where the system
    // allows it (Windows): dark or light, and a caption color given in sRGB from 0 to 1.
    void setTitleBarColors(bool dark, math::Vec3 caption);
    // The icon of the window and its taskbar button, from 8-bit RGBA pixels.
    void setIcon(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);

    void setMouseMode(MouseMode mode);
    [[nodiscard]] MouseMode mouseMode() const noexcept;
    // Captured, or the cursor as it normally is.
    void setMouseCaptured(bool captured);
    [[nodiscard]] bool isMouseCaptured() const noexcept;

    // Creates a VkSurfaceKHR for this window on the given VkInstance and returns its handle. The
    // window must have been created with WindowConfig::vulkan, and the caller owns the surface.
    [[nodiscard]] core::Result<std::uint64_t> createVulkanSurface(void* vulkanInstance) const;

    // The window as the system knows it, for a WebGPU surface; Kind::None where it cannot tell.
    [[nodiscard]] NativeWindow native() const noexcept;

    // An OpenGL context that draws into this window, current on the calling thread: OpenGL ES 3.0 when `es`, as phones and
    // browsers have, OpenGL 3.3 core otherwise; with the debug output when `debug`. The window must have been created with
    // WindowConfig::openGl, and the caller destroys the context with destroyGlContext.
    [[nodiscard]] core::Result<void*> createGlContext(bool es, bool debug) const;
    void destroyGlContext(void* context) const noexcept;
    // How many times the display refreshes before what OpenGL drew shows: 1 waits for it, 0 never, -1 only when on
    // time. Answers whether the system took it.
    bool setGlSwapInterval(int interval) const noexcept;
    // Shows what OpenGL drew into the window.
    void swapGl() const noexcept;
    // The address of a function of OpenGL in the current context, by its full name; null without one.
    [[nodiscard]] static void* glFunction(const char* name) noexcept;

private:
    friend class Platform;

    explicit Window(detail::NativeWindow* native) noexcept;
    void destroy() noexcept;

    detail::NativeWindow* m_native = nullptr;
    // What SDL only knows once the window shows: whether full screen is the display to itself.
    bool m_tookDisplay = false;
};

} // namespace devex::platform
