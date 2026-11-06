#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

inline constexpr std::string_view projectExtension = ".dvxproj";
inline constexpr std::string_view resourceScheme = "res://";

inline constexpr std::size_t physicsLayerCount = 16;
inline constexpr std::size_t audioGroupCount = 8;

// The physics of a project: gravity, and the collision layers that bodies belong to.
struct DEVEX_API PhysicsSettings
{
    math::Vec3 gravity{0.0f, -9.81f, 0.0f};
    // The name of each layer; layers with an empty name are unused, except the first one.
    std::array<std::string, physicsLayerCount> layerNames{"Default"};
    // Bit b of layerCollisions[a] is set when layer a collides with layer b.
    std::array<std::uint16_t, physicsLayerCount> layerCollisions = [] {
        std::array<std::uint16_t, physicsLayerCount> all{};
        all.fill(0xFFFF);
        return all;
    }();

    [[nodiscard]] bool collides(std::uint32_t a, std::uint32_t b) const noexcept;
    // Keeps the matrix symmetric.
    void setCollides(std::uint32_t a, std::uint32_t b, bool collide) noexcept;

    bool operator==(const PhysicsSettings&) const = default;
};

// The mixer of a project: the master volume, and the groups that sounds play in, such as Music, each
// with its volume, which game code changes (an options menu).
struct DEVEX_API AudioSettings
{
    float masterVolume = 1.0f;
    // Groups with an empty name are unused, except the first one.
    std::array<std::string, audioGroupCount> groupNames{"Effects", "Music", "Voice"};
    std::array<float, audioGroupCount> groupVolumes = [] {
        std::array<float, audioGroupCount> volumes{};
        volumes.fill(1.0f);
        return volumes;
    }();

    bool operator==(const AudioSettings&) const = default;
};

// How sprites cover each other: the sorting layers of the project, from the back to the front.
// Sprites name their layer; the blended surfaces and the particles draw in "Default".
struct DEVEX_API SortingSettings
{
    // "Default" is always among them.
    std::vector<std::string> layers{"Default"};

    // Where a layer draws, counted from "Default": negative behind it, positive in front of it. An
    // empty or unknown name draws in "Default".
    [[nodiscard]] std::int32_t rank(std::string_view layer) const noexcept;

    bool operator==(const SortingSettings&) const = default;
};

// How the window of the game starts, in the player and in exported games.
struct DEVEX_API WindowSettings
{
    // In window coordinates, which the system scales on high-density displays.
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    // Borderless, over the whole display.
    bool fullscreen = false;
    // Waits for the display refresh; without it, frames are presented as soon as they are ready.
    bool vsync = true;
    // 0 leaves the frame rate unlimited.
    std::uint32_t maxFrameRate = 0;
    // A texture of the project, the icon of the window and of the exported executable.
    AssetId icon;
    // On Linux, the exported game lists itself among the applications of the desktop when it
    // starts, where the dock and the window switcher take its name and icon.
    bool desktopEntry = true;
    // On Android, the Back button quits the game when no input action uses it, as in Godot.
    bool quitOnBack = true;

    bool operator==(const WindowSettings&) const = default;
};

// What an export of the game contains, and where it goes.
// How a game is exported for Android, kept with the project.
struct DEVEX_API AndroidExportSettings
{
    // The folder the APK or the AAB goes to, relative to the project or absolute.
    std::string output = "export/android";
    // The name Android knows the game by, such as "com.studio.game"; empty, it comes from the game's.
    std::string package;
    std::string versionName = "1.0";
    // Grows with every version the Play Store receives.
    std::int32_t versionCode = 1;
    // "landscape", "portrait" or "sensor".
    std::string orientation = "landscape";
    // The architectures: of phones, and of the emulator on a PC.
    bool arm64 = true;
    bool x86_64 = false;
    // "apk", to install directly, or "aab", for the Play Store.
    std::string format = "apk";
    // Signed with the key of the game rather than a debug key.
    bool release = false;
    // Installs and starts the APK on the connected device after exporting it.
    bool install = false;

    bool operator==(const AndroidExportSettings&) const = default;
};

// The key an Android game is signed with, in a file of the project's cache that version control does
// not see, the passwords in the clear.
struct DEVEX_API AndroidSigning
{
    std::filesystem::path keystore;
    std::string alias;
    std::string storePassword;
    std::string keyPassword;

    bool operator==(const AndroidSigning&) const = default;
};

// The desktop system an editor can export for besides its own, with that system's engine beside it: "linux" on Windows,
// "windows" on Linux, and empty on a system with no other.
[[nodiscard]] DEVEX_API std::string_view otherDesktopSystem() noexcept;

struct DEVEX_API ExportSettings
{
    // The system the Export window exports for: "desktop" for the one the editor runs on, "android", or the
    // other desktop system by its name.
    std::string platform = "desktop";
    AndroidExportSettings android;
    // Scenes exported with the startup scene, as res:// paths. The scenes, prefabs and other assets
    // that exported scenes refer to are exported with them.
    std::vector<std::string> scenes;
    // res:// folders whose assets are always exported, for those that code loads by identifier.
    std::vector<std::string> includeFolders;
    // The folder the game is exported to, relative to the project or absolute: by default, named
    // after the system the game is exported for, which is the one the editor runs on.
#if defined(_WIN32)
    std::string output = "export/windows";
#elif defined(__APPLE__)
    std::string output = "export/macos";
#else
    std::string output = "export/linux";
#endif
    // The folder a game for the other desktop system is exported to.
#if defined(_WIN32)
    std::string otherOutput = "export/linux";
#else
    std::string otherOutput = "export/windows";
#endif
    // The configuration of the engine build the game is exported with: "Release" or "Debug".
    std::string configuration = "Release";
    // Windows: a terminal opens with the game and shows its log, which is kept in a file either way.
    bool showTerminal = false;

    bool operator==(const ExportSettings&) const = default;

    [[nodiscard]] bool targetsOtherDesktop() const noexcept
    {
        return !otherDesktopSystem().empty() && platform == otherDesktopSystem();
    }
};

// The value an input action gives: a button is pressed or not, an axis goes from -1 to 1 (a wheel,
// a throttle), a vector gives a direction of length 1 at most (moving, looking).
enum class InputActionKind : std::uint8_t
{
    Button,
    Axis,
    Vector,
};

// Which way a binding pushes an axis or a vector action, or rather its positive end for a
// gamepad axis. Buttons ignore it, and a stick gives a whole vector. Up is positive, as on a map.
enum class InputDirection : std::uint8_t
{
    Positive,
    Negative,
    Up,
    Down,
    Left,
    Right,
};

[[nodiscard]] DEVEX_API std::string_view toString(InputActionKind kind) noexcept;
[[nodiscard]] DEVEX_API std::optional<InputActionKind> parseInputActionKind(std::string_view text) noexcept;
[[nodiscard]] DEVEX_API std::string_view toString(InputDirection direction) noexcept;
[[nodiscard]] DEVEX_API std::optional<InputDirection> parseInputDirection(std::string_view text) noexcept;

// What the player presses or moves for an action.
struct DEVEX_API InputBinding
{
    // A key, a mouse button, a gamepad button, axis or stick, as platform::parseInputSource reads
    // it: "key:Space", "mouse:Left", "pad:South", "axis:LeftTrigger", "stick:Left".
    std::string input;
    InputDirection direction = InputDirection::Positive;

    bool operator==(const InputBinding&) const = default;
};

// Something the player does, such as "Jump" or "Move", that game code reads instead of keys.
struct DEVEX_API InputAction
{
    std::string name;
    InputActionKind kind = InputActionKind::Button;
    // The context the action belongs to; empty for an action that is always read.
    std::string context;
    // Below this, an axis or the length of a vector reads zero; the rest of the range is spread
    // back from zero to one. Gamepads already ignore small moves of their sticks.
    float deadZone = 0.0f;
    std::vector<InputBinding> bindings;

    bool operator==(const InputAction&) const = default;
};

// A group of actions that game code turns on and off, such as the actions of the game while a menu
// is open.
struct DEVEX_API InputContext
{
    std::string name;
    bool activeAtStart = true;

    bool operator==(const InputContext&) const = default;
};

// The actions of a game and what plays them, which the player may change for themselves.
struct DEVEX_API InputSettings
{
    std::vector<InputContext> contexts{InputContext{.name = "Gameplay"}};
    std::vector<InputAction> actions;

    bool operator==(const InputSettings&) const = default;
};

// The languages of a game: the one that shows where a translation is missing, and the one the
// editor plays it in.
struct DEVEX_API LocalizationSettings
{
    // What shows where the language of the player has no message, as a code such as "en".
    std::string fallbackLanguage = "en";
    // The language the game plays in from the editor, whatever the player or the system chose; empty
    // to choose as the exported game does.
    std::string testLanguage;

    bool operator==(const LocalizationSettings&) const = default;
};

enum class ContentRoot : std::uint8_t
{
    Assets,
    Code,
};

// An ordered autoload: instantiated once per play session, before the current level starts.
// Its entities and attached scripts survive level changes.
struct DEVEX_API GlobalScene
{
    std::string name;
    std::string path;
    bool enabled = true;

    bool operator==(const GlobalScene&) const = default;
};

[[nodiscard]] DEVEX_API core::Result<void> validateGlobals(std::span<const GlobalScene> globals);

// Enabling an addon's editor tools is personal to this project. Its content remains available.
struct DEVEX_API AddonSetting
{
    std::string id;
    bool enabled = false;
    bool operator==(const AddonSetting&) const = default;
};

// A game project: a .dvxproj file whose directory holds the assets/ folder, the code/ folder of its
// game module when it has one, and the .devex/ cache of imported data and builds, which is never
// versioned.
struct DEVEX_API Project
{
    // The version of the engine that last saved it; empty when that was before projects said.
    std::string engineVersion;
    std::string name;
    // Absolute directory of the project file.
    std::filesystem::path root;
    // Absolute path of the .dvxproj file.
    std::filesystem::path file;
    // The res:// path of the scene the player opens; empty for the first scene of the project.
    std::string startupScene;
    PhysicsSettings physics;
    AudioSettings audio;
    SortingSettings sorting;
    WindowSettings window;
    ExportSettings exportSettings;
    InputSettings input;
    LocalizationSettings localization;
    std::vector<GlobalScene> globals;
    std::vector<AddonSetting> addons;

    [[nodiscard]] std::filesystem::path assetsDirectory() const
    {
        return root / "assets";
    }

    [[nodiscard]] std::filesystem::path cacheDirectory() const
    {
        return root / ".devex";
    }

    // The sources of the game module, built with CMake.
    [[nodiscard]] std::filesystem::path codeDirectory() const
    {
        return root / "code";
    }

    // "res://assets/models/crate.glb" for a file inside the project, empty otherwise.
    [[nodiscard]] std::string resourcePath(const std::filesystem::path& path) const;
    // The absolute path of a res:// path, or nothing when the text is not one.
    [[nodiscard]] std::optional<std::filesystem::path> absolutePath(std::string_view resource) const;
    // Validates a new file in an existing res:// folder below the required content root.
    // Rejects occupied names, ignored folders and paths escaping through directory links.
    [[nodiscard]] core::Result<std::filesystem::path> newFilePath(std::string_view folder,
                                                                  std::string_view fileName, ContentRoot content) const;
};

// Reads "[project format=1 name="My game" startup_scene="res://assets/scenes/Main.dvxscene"]" from
// the .dvxproj file, followed by optional [physics], [physics_layer], [audio], [audio_group],
// [sorting_layer], [window], [export], [export_scene], [export_folder], [input_context],
// [input_action] and [localization] sections.
// The file name a game is exported under, from its name, without an extension.
[[nodiscard]] DEVEX_API std::string gameFileName(std::string_view name);
// Lowercase words joined by dots, each starting with a letter, at least two.
[[nodiscard]] DEVEX_API bool validAndroidPackage(std::string_view package);
// "com.devex.<game>", from the name of a game.
[[nodiscard]] DEVEX_API std::string androidPackageFromName(std::string_view name);
// The key of the game, empty when none was given.
[[nodiscard]] DEVEX_API AndroidSigning readAndroidSigning(const Project& project);
[[nodiscard]] DEVEX_API core::Result<void> writeAndroidSigning(const Project& project, const AndroidSigning& signing);

[[nodiscard]] DEVEX_API core::Result<Project> loadProject(const std::filesystem::path& projectFile);

// How a project stands against this engine.
enum class ProjectAge : std::uint8_t
{
    Current,
    // Files of older versions read, and take the current format when they are saved.
    Older,
    // What this engine does not know of its files is lost when they are saved.
    Newer,
};
[[nodiscard]] DEVEX_API ProjectAge projectAge(const Project& project) noexcept;
// Copies the files that are made by hand to .devex/backups/<version>, and says where: what is baked comes back.
[[nodiscard]] DEVEX_API core::Result<std::filesystem::path> backUpProject(const Project& project);
// An older project becomes current: its files are copied, and its project file says this version.
[[nodiscard]] DEVEX_API core::Result<std::filesystem::path> upgradeProject(Project& project);
// The same from the text of a project file, as exported games keep it; projectFile gives the
// project its root.
[[nodiscard]] DEVEX_API core::Result<Project> parseProject(std::string_view text, const std::filesystem::path& projectFile);

// Writes the project's settings to its .dvxproj file. Sections are only written when they differ
// from the defaults.
[[nodiscard]] DEVEX_API core::Result<void> saveProject(const Project& project);
[[nodiscard]] DEVEX_API std::string writeProjectText(const Project& project);

// Writes a .dvxproj file into the directory and creates its assets/ and empty code/ folders.
[[nodiscard]] DEVEX_API core::Result<Project> createProject(const std::filesystem::path& directory,
                                                            std::string_view name);

} // namespace devex::asset
