#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/core/MappedFile.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Key.hpp>
#include <devex/platform/Window.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
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
    // True when there was any, or when a file dialog answered.
    bool pollEvents(const EventCallback& callback);
    // Waits until an event comes, a file dialog answers or wake is called, at most `timeout`,
    // without taking the event: the next pollEvents forwards it. False when the time ran out. The
    // editor rests on it while nothing moves.
    bool waitEvents(std::chrono::milliseconds timeout);
    // Ends a waitEvents as an event would. Safe from any thread.
    static void wake();

    [[nodiscard]] const Input& input() const noexcept;
    // Everything the devices did this frame, whatever the tools keep from the game: what the tools
    // of the editor read, the game reading input().
    [[nodiscard]] const Input& toolsInput() const noexcept;
    // Gives a window other than the main one devices of its own: what happens over it reaches neither
    // input() nor toolsInput(), nor the callback of pollEvents, only windowInput().
    void trackWindowInput(WindowId window);
    void untrackWindowInput(WindowId window);
    [[nodiscard]] const Input& windowInput(WindowId window) const;
    // Whether someone asked to close a tracked window since the last call.
    [[nodiscard]] bool takeWindowCloseRequest(WindowId window);

    // The system, the processor and memory, the displays and the drivers in use, in words, for a bug report.
    [[nodiscard]] std::string describeMachine() const;

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
    // Opens a web page in the browser of the user.
    [[nodiscard]] core::Result<void> openUrl(const std::string& url) const;

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

    // The languages the user prefers, the first first, as codes such as "fr_FR": what a game shows
    // before the player chooses one.
    [[nodiscard]] std::vector<std::string> preferredLanguages() const;

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

    // While the tools use a device, presses and motion on it no longer reach Input; toolsInput
    // still sees them. Releases always do, so that no key stays down.
    void setToolsInputCapture(bool keyboard, bool mouse) noexcept;

private:
    Platform() = default;
    void shutdown() noexcept;

    bool m_initialized = false;
    bool m_textInputActive = false;
    // The window that takes what is typed, while it is on.
    void* m_textInputWindow = nullptr;
    bool m_toolsCaptureKeyboard = false;
    bool m_toolsCaptureMouse = false;
    Cursor m_cursor = Cursor::Arrow;
    Input m_input;
    Input m_toolsInput;
    struct TrackedWindow
    {
        Input input;
        bool closeRequested = false;
    };
    std::map<WindowId, TrackedWindow> m_trackedWindows;
};

// Sleeps for the given duration with sub-millisecond precision.
DEVEX_API void sleepPrecise(std::chrono::nanoseconds duration);

// Directory containing the executable, available without a Platform, as for command-line tools.
// On Android, the folder where the activity extracted the files of the engine.
[[nodiscard]] DEVEX_API std::filesystem::path executableDirectory();

#if defined(__ANDROID__)
// Maps a file of the APK's assets, which must be stored uncompressed, such as the package of the game.
[[nodiscard]] DEVEX_API core::Result<core::MappedFile> mapAndroidAsset(const std::filesystem::path& name);
#endif

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
// Started from a terminal, a windowed program writes there, unless it prints to a file or a pipe. Windows only: elsewhere,
// a program has its terminal already.
DEVEX_API void useParentTerminal();
// Whether a windowed program shows a terminal beside it, with what it prints. Without one, the console programs it starts
// open no window of their own either. A program that has a terminal from the start keeps it. Windows only.
DEVEX_API void showTerminal(bool shown);
// Whether the terminal of this program goes away with it, and its last message too: it was opened for this program alone.
[[nodiscard]] DEVEX_API bool terminalClosesWithProgram() noexcept;
// Shows an error in a box of the system, for a program with nowhere else to say it. Works before
// the platform starts, and when it failed to.
DEVEX_API void showErrorMessage(std::string_view title, std::string_view message);

} // namespace devex::platform
