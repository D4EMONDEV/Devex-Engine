#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Key.hpp>
#include <devex/platform/Window.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::platform {

using EventCallback = std::function<void(const Event& event)>;

enum class FileDialogType : std::uint8_t
{
    OpenFile,
    SaveFile,
    OpenFolder,
};

struct DEVEX_API FileFilter
{
    // Shown to the user, such as "Scenes".
    std::string name;
    // Extensions without dots, separated by semicolons: "png;jpg".
    std::string extensions;
};

struct DEVEX_API FileDialog
{
    FileDialogType type = FileDialogType::OpenFile;
    // Ignored by folder dialogs.
    std::vector<FileFilter> filters;
    // A folder, or a file name for save dialogs; empty lets the system choose.
    std::filesystem::path defaultLocation;
};

// Receives the chosen path, or nothing when the dialog was cancelled or failed.
using FileDialogCallback = std::function<void(std::optional<std::filesystem::path> chosen)>;

// Owns the operating system layer: windows, events and input. Only one instance may exist at a
// time, and it must outlive every window it creates.
// The shapes of the mouse pointer the system draws.
enum class Cursor : std::uint8_t
{
    Arrow,
    Text,
    ResizeHorizontal,
    ResizeVertical,
    // From the top left corner to the bottom right one, and from the bottom left to the top right.
    ResizeDiagonalDown,
    ResizeDiagonalUp,
    Move,
    Hand,
    NotAllowed,
};

class DEVEX_API Platform
{
public:
    [[nodiscard]] static core::Result<Platform> create();

    Platform(Platform&& other) noexcept;
    Platform& operator=(Platform&& other) noexcept;
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    [[nodiscard]] core::Result<Window> createWindow(const WindowConfig& config);

    // Starts a new input frame, then updates the input state and forwards every pending event.
    void pollEvents(const EventCallback& callback);

    [[nodiscard]] const Input& input() const noexcept;
    // Everything the devices did this frame, whatever the tools keep from the game: what the tools
    // of the editor read, the game reading input().
    [[nodiscard]] const Input& toolsInput() const noexcept;

    // Directory containing the executable, where engine data such as shaders is deployed.
    [[nodiscard]] std::filesystem::path baseDirectory() const;
    // A writable directory for the settings of an application of the user, created if needed:
    // %APPDATA%/Devex/<application> on Windows.
    [[nodiscard]] core::Result<std::filesystem::path> userDataDirectory(std::string_view application) const;

    // Shows a native file dialog without blocking, modal to the window. The callback runs during a
    // later pollEvents, on the thread that polls events.
    void showFileDialog(const Window& parent, const FileDialog& dialog, FileDialogCallback callback);

    // Opens a folder in the system's file manager, or a file with its default application.
    [[nodiscard]] core::Result<void> openPath(const std::filesystem::path& path) const;

    // Turns typing on for a window: until it is turned off, what the user types arrives in
    // Input::typedText, and the system may show its input method over the window.
    void setTextInput(const Window& window, bool active);
    [[nodiscard]] bool isTextInputActive() const noexcept;
    // Where the text being typed stands in a window, in its coordinates: the input method of the
    // system opens next to it.
    void setTextInputArea(const Window& window, math::Vec2 min, math::Vec2 max);

    // The shape of the mouse pointer, until it is set again.
    void setCursor(Cursor cursor);

    // The clipboard of the system, as UTF-8 text.
    [[nodiscard]] std::string clipboardText() const;
    void setClipboardText(std::string_view text);

    // Name of the key position, independent of the layout ("W").
    [[nodiscard]] std::string keyName(Key key) const;
    // Label of the key under the current keyboard layout ("Z" for Key::W on AZERTY).
    [[nodiscard]] std::string keyLabel(Key key) const;

    // Instance extensions needed to create window surfaces. Available once a window has been
    // created with WindowConfig::vulkan; the strings live as long as the Platform.
    [[nodiscard]] core::Result<std::span<const char* const>> vulkanInstanceExtensions() const;

    // Registers the function called when a window must be redrawn while the operating system
    // blocks pollEvents, such as during a live resize on Windows. An empty function unregisters.
    void setLiveRedrawCallback(std::function<void()> callback);

    // Connects Dear ImGui to the window: pollEvents then forwards every event to ImGui. An ImGui
    // context must be current, and shutdownImGui must run before it is destroyed.
    [[nodiscard]] core::Result<void> initializeImGui(Window& window);
    void shutdownImGui() noexcept;
    // Updates ImGui's display size, time and mouse state for a new ImGui frame.
    void beginImGuiFrame();
    // While ImGui uses a device, presses and motion on it no longer reach Input. Releases always
    // do, so that no key stays down.
    void setImGuiInputCapture(bool keyboard, bool mouse) noexcept;

private:
    Platform() = default;
    void shutdown() noexcept;

    bool m_initialized = false;
    bool m_textInputActive = false;
    bool m_imguiInitialized = false;
    bool m_imguiCapturesKeyboard = false;
    bool m_imguiCapturesMouse = false;
    Cursor m_cursor = Cursor::Arrow;
    Input m_input;
    Input m_toolsInput;
};

// Sleeps for the given duration with sub-millisecond precision.
DEVEX_API void sleepPrecise(std::chrono::nanoseconds duration);

// Directory containing the executable, available without a Platform, as for command-line tools.
[[nodiscard]] DEVEX_API std::filesystem::path executableDirectory();

// A folder of the user that can always be written to, for the logs and the saves of an
// application, created when missing: %APPDATA%\<organization>\<application> on Windows. An empty
// organization leaves the application alone in the folder of the system. Available without a
// Platform, since the log opens before it.
[[nodiscard]] DEVEX_API core::Result<std::filesystem::path> userDataDirectory(std::string_view organization,
                                                                              std::string_view application);
// The same folder without making it, for reading what may not be there.
[[nodiscard]] DEVEX_API core::Result<std::filesystem::path> userDataLocation(std::string_view organization,
                                                                             std::string_view application);

// Whether this process was built or made a windowed application, which has no console to print
// to: an exported game started from the file manager.
[[nodiscard]] DEVEX_API bool isWindowedApplication() noexcept;
// Shows an error in a box of the system, for a program with nowhere else to say it. Works before
// the platform starts, and when it failed to.
DEVEX_API void showErrorMessage(std::string_view title, std::string_view message);

} // namespace devex::platform
