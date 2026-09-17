#include "SdlWindow.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Platform.hpp>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace devex::platform {
namespace {

// Key values are USB HID usages, exactly like SDL scancodes, so conversions are plain casts.
static_assert(static_cast<int>(Key::A) == SDL_SCANCODE_A);
static_assert(static_cast<int>(Key::Digit0) == SDL_SCANCODE_0);
static_assert(static_cast<int>(Key::Escape) == SDL_SCANCODE_ESCAPE);
static_assert(static_cast<int>(Key::F12) == SDL_SCANCODE_F12);
static_assert(static_cast<int>(Key::Up) == SDL_SCANCODE_UP);
static_assert(static_cast<int>(Key::KeypadPeriod) == SDL_SCANCODE_KP_PERIOD);
static_assert(static_cast<int>(Key::NonUsBackslash) == SDL_SCANCODE_NONUSBACKSLASH);
static_assert(static_cast<int>(Key::RightSuper) == SDL_SCANCODE_RGUI);

std::atomic<bool> platformExists{false};

// Only one Platform exists at a time, so its live redraw callback can be process-wide.
std::function<void()> liveRedrawCallback;

// A file dialog waiting for its answer. SDL requires the filters to outlive the dialog.
struct PendingDialog
{
    std::vector<std::string> names;
    std::vector<std::string> patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string location;
    FileDialogCallback callback;
    std::optional<std::filesystem::path> chosen;
};

// Dialogs answer on any thread; their callbacks run on the polling thread.
std::mutex answeredDialogsMutex;
std::vector<std::unique_ptr<PendingDialog>> answeredDialogs;

void SDLCALL answerDialog(void* userData, const char* const* files, int /*filter*/)
{
    std::unique_ptr<PendingDialog> dialog(static_cast<PendingDialog*>(userData));
    if (files == nullptr)
    {
        DEVEX_LOG_WARNING("The file dialog failed: {}", SDL_GetError());
    }
    else if (files[0] != nullptr)
    {
        dialog->chosen = core::pathFromUtf8(files[0]);
    }
    const std::scoped_lock lock(answeredDialogsMutex);
    answeredDialogs.push_back(std::move(dialog));
}

bool SDLCALL watchLiveRedraw(void* /*userData*/, SDL_Event* event)
{
    // SDL sends live-resize exposures from the main thread, while pollEvents is blocked.
    if (event->type == SDL_EVENT_WINDOW_EXPOSED && event->window.data1 != 0 && liveRedrawCallback)
    {
        liveRedrawCallback();
    }
    return true;
}

[[nodiscard]] Key toKey(SDL_Scancode scancode) noexcept
{
    return static_cast<Key>(scancode);
}

[[nodiscard]] SDL_Scancode toScancode(Key key) noexcept
{
    return static_cast<SDL_Scancode>(key);
}

[[nodiscard]] std::optional<MouseButton> toMouseButton(Uint8 button) noexcept
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return std::nullopt;
    }
}

// Devices whose presses and motion are used by ImGui instead of gameplay.
struct InputCapture
{
    bool keyboard = false;
    bool mouse = false;
};

void dispatchEvent(const SDL_Event& event, Input& input, InputCapture capture,
                   const EventCallback& callback)
{
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        callback(QuitRequested{});
        break;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        callback(WindowCloseRequested{WindowId{event.window.windowID}});
        break;

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (SDL_Window* const window = SDL_GetWindowFromID(event.window.windowID))
        {
            callback(WindowResized{WindowId{event.window.windowID}, detail::windowSize(window),
                                   detail::windowPixelSize(window)});
        }
        break;

    case SDL_EVENT_WINDOW_MINIMIZED:
        callback(WindowMinimized{WindowId{event.window.windowID}});
        break;

    case SDL_EVENT_WINDOW_RESTORED:
        callback(WindowRestored{WindowId{event.window.windowID}});
        break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        callback(WindowFocusChanged{WindowId{event.window.windowID}, true});
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Key releases that happen in another window are never reported to this one.
        input.releaseAll();
        callback(WindowFocusChanged{WindowId{event.window.windowID}, false});
        break;

    case SDL_EVENT_KEY_DOWN: {
        const Key key = toKey(event.key.scancode);
        if (!event.key.repeat && !capture.keyboard)
        {
            input.setKeyDown(key, true);
        }
        callback(KeyPressed{key, event.key.repeat});
        break;
    }

    case SDL_EVENT_KEY_UP: {
        const Key key = toKey(event.key.scancode);
        input.setKeyDown(key, false);
        callback(KeyReleased{key});
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (const std::optional<MouseButton> button = toMouseButton(event.button.button))
        {
            const math::Vec2 position{event.button.x, event.button.y};
            if (event.button.down)
            {
                if (!capture.mouse)
                {
                    input.setMouseButtonDown(*button, true);
                }
                callback(MouseButtonPressed{*button, position});
            }
            else
            {
                input.setMouseButtonDown(*button, false);
                callback(MouseButtonReleased{*button, position});
            }
        }
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        const math::Vec2 position{event.motion.x, event.motion.y};
        const math::Vec2 delta{event.motion.xrel, event.motion.yrel};
        input.moveMouse(position, capture.mouse ? math::Vec2{0.0f} : delta);
        callback(MouseMoved{position, delta});
        break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        const math::Vec2 delta{event.wheel.x * direction, event.wheel.y * direction};
        if (!capture.mouse)
        {
            input.scrollMouse(delta);
        }
        callback(MouseWheelScrolled{delta});
        break;
    }

    case SDL_EVENT_DROP_FILE:
        callback(FileDropped{WindowId{event.drop.windowID},
                             event.drop.data != nullptr ? event.drop.data : ""});
        break;

    default:
        break;
    }
}

} // namespace

core::Result<Platform> Platform::create()
{
    if (platformExists.exchange(true))
    {
        return core::makeError(core::ErrorCode::InvalidState, "a Platform instance already exists");
    }

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        platformExists.store(false);
        return core::makeError(core::ErrorCode::Platform, "SDL_Init failed: {}", SDL_GetError());
    }
    if (!SDL_AddEventWatch(&watchLiveRedraw, nullptr))
    {
        SDL_Quit();
        platformExists.store(false);
        return core::makeError(core::ErrorCode::Platform, "cannot watch SDL events: {}",
                               SDL_GetError());
    }

    const int version = SDL_GetVersion();
    DEVEX_LOG_DEBUG("SDL {}.{}.{} initialized with the '{}' video driver",
                    SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
                    SDL_VERSIONNUM_MICRO(version), SDL_GetCurrentVideoDriver());

    Platform platform;
    platform.m_initialized = true;
    return platform;
}

Platform::Platform(Platform&& other) noexcept
    : m_initialized(std::exchange(other.m_initialized, false))
    , m_input(other.m_input)
{
}

Platform& Platform::operator=(Platform&& other) noexcept
{
    if (this != &other)
    {
        shutdown();
        m_initialized = std::exchange(other.m_initialized, false);
        m_input = other.m_input;
    }
    return *this;
}

Platform::~Platform()
{
    shutdown();
}

void Platform::shutdown() noexcept
{
    if (m_initialized)
    {
        shutdownImGui();
        SDL_RemoveEventWatch(&watchLiveRedraw, nullptr);
        liveRedrawCallback = nullptr;
        {
            // Answers never delivered must not call into an application that is shutting down.
            const std::scoped_lock lock(answeredDialogsMutex);
            answeredDialogs.clear();
        }
        SDL_Quit();
        platformExists.store(false);
        m_initialized = false;
    }
}

core::Result<Window> Platform::createWindow(const WindowConfig& config)
{
    DEVEX_ASSERT(m_initialized);

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.vulkan)
    {
        flags |= SDL_WINDOW_VULKAN;
    }
    if (config.hidden)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }

    SDL_Window* const window =
        SDL_CreateWindow(config.title.c_str(), static_cast<int>(config.width),
                         static_cast<int>(config.height), flags);
    if (window == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot create window '{}': {}",
                               config.title, SDL_GetError());
    }
    return Window(detail::toNativeWindow(window));
}

void Platform::pollEvents(const EventCallback& callback)
{
    DEVEX_ASSERT(m_initialized);

    m_input.beginFrame();
    const InputCapture capture{m_imguiCapturesKeyboard, m_imguiCapturesMouse};
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (m_imguiInitialized)
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }
        dispatchEvent(event, m_input, capture, callback);
    }

    std::vector<std::unique_ptr<PendingDialog>> answered;
    {
        const std::scoped_lock lock(answeredDialogsMutex);
        answered.swap(answeredDialogs);
    }
    for (const std::unique_ptr<PendingDialog>& dialog : answered)
    {
        if (dialog->callback)
        {
            dialog->callback(std::move(dialog->chosen));
        }
    }
}

core::Result<void> Platform::initializeImGui(Window& window)
{
    DEVEX_ASSERT(m_initialized && !m_imguiInitialized);
    DEVEX_ASSERT_MSG(ImGui::GetCurrentContext() != nullptr, "create an ImGui context first");
    if (!ImGui_ImplSDL3_InitForVulkan(detail::toSdlWindow(window.m_native)))
    {
        return core::makeError(core::ErrorCode::Platform, "cannot initialize ImGui for SDL3");
    }
    m_imguiInitialized = true;
    return {};
}

void Platform::shutdownImGui() noexcept
{
    if (m_imguiInitialized)
    {
        ImGui_ImplSDL3_Shutdown();
        m_imguiInitialized = false;
        m_imguiCapturesKeyboard = false;
        m_imguiCapturesMouse = false;
    }
}

void Platform::beginImGuiFrame()
{
    DEVEX_ASSERT(m_imguiInitialized);
    ImGui_ImplSDL3_NewFrame();
}

void Platform::setImGuiInputCapture(bool keyboard, bool mouse) noexcept
{
    m_imguiCapturesKeyboard = keyboard;
    m_imguiCapturesMouse = mouse;
}

const Input& Platform::input() const noexcept
{
    return m_input;
}

std::filesystem::path Platform::baseDirectory() const
{
    const char* const basePath = SDL_GetBasePath();
    return basePath != nullptr ? core::pathFromUtf8(basePath) : std::filesystem::current_path();
}

core::Result<std::filesystem::path> Platform::userDataDirectory(std::string_view application) const
{
    const std::string name(application);
    char* const path = SDL_GetPrefPath("Devex", name.c_str());
    if (path == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "no user data directory: {}", SDL_GetError());
    }
    std::filesystem::path directory = core::pathFromUtf8(path);
    SDL_free(path);
    return directory;
}

void Platform::showFileDialog(const Window& parent, const FileDialog& dialog, FileDialogCallback callback)
{
    DEVEX_ASSERT(m_initialized);
    auto pending = std::make_unique<PendingDialog>();
    for (const FileFilter& filter : dialog.filters)
    {
        pending->names.push_back(filter.name);
        pending->patterns.push_back(filter.extensions);
    }
    for (std::size_t index = 0; index < pending->names.size(); ++index)
    {
        pending->filters.push_back({pending->names[index].c_str(), pending->patterns[index].c_str()});
    }
    pending->location = core::toUtf8(dialog.defaultLocation);
    pending->callback = std::move(callback);

    SDL_Window* const window = detail::toSdlWindow(parent.m_native);
    const char* const location = pending->location.empty() ? nullptr : pending->location.c_str();
    const auto filterCount = static_cast<int>(pending->filters.size());
    const SDL_DialogFileFilter* const filters = pending->filters.empty() ? nullptr : pending->filters.data();
    // Owned by answerDialog from here on.
    PendingDialog* const userData = pending.release();
    switch (dialog.type)
    {
    case FileDialogType::OpenFile:
        SDL_ShowOpenFileDialog(&answerDialog, userData, window, filters, filterCount, location, false);
        break;
    case FileDialogType::SaveFile:
        SDL_ShowSaveFileDialog(&answerDialog, userData, window, filters, filterCount, location);
        break;
    case FileDialogType::OpenFolder:
        SDL_ShowOpenFolderDialog(&answerDialog, userData, window, location, false);
        break;
    }
}

std::string Platform::keyName(Key key) const
{
    DEVEX_ASSERT(m_initialized);
    return SDL_GetScancodeName(toScancode(key));
}

std::string Platform::keyLabel(Key key) const
{
    DEVEX_ASSERT(m_initialized);
    const SDL_Keycode keycode = SDL_GetKeyFromScancode(toScancode(key), SDL_KMOD_NONE, false);
    return SDL_GetKeyName(keycode);
}

core::Result<std::span<const char* const>> Platform::vulkanInstanceExtensions() const
{
    DEVEX_ASSERT(m_initialized);
    Uint32 count = 0;
    const char* const* const extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (extensions == nullptr)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "Vulkan is unavailable for SDL windows: {}", SDL_GetError());
    }
    return std::span<const char* const>(extensions, count);
}

void Platform::setLiveRedrawCallback(std::function<void()> callback)
{
    DEVEX_ASSERT(m_initialized);
    liveRedrawCallback = std::move(callback);
}

void sleepPrecise(std::chrono::nanoseconds duration)
{
    if (duration.count() > 0)
    {
        SDL_DelayPrecise(static_cast<Uint64>(duration.count()));
    }
}

} // namespace devex::platform
