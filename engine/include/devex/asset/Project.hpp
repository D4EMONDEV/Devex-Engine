#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

inline constexpr std::string_view projectExtension = ".dvxproj";
inline constexpr std::string_view resourceScheme = "res://";

inline constexpr std::size_t physicsLayerCount = 16;
inline constexpr std::size_t audioGroupCount = 8;

// The physics of a project: gravity, and the collision layers that bodies belong to.
struct PhysicsSettings
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
struct AudioSettings
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

// How the window of the game starts, in the player and in exported games.
struct WindowSettings
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

    bool operator==(const WindowSettings&) const = default;
};

// What an export of the game contains, and where it goes.
struct ExportSettings
{
    // Scenes exported with the startup scene, as res:// paths. The scenes, prefabs and other assets
    // that exported scenes refer to are exported with them.
    std::vector<std::string> scenes;
    // res:// folders whose assets are always exported, for those that code loads by identifier.
    std::vector<std::string> includeFolders;
    // The folder the game is exported to, relative to the project or absolute.
    std::string output = "export/windows";
    // The configuration of the engine build the game is exported with: "Release" or "Debug".
    std::string configuration = "Release";

    bool operator==(const ExportSettings&) const = default;
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

[[nodiscard]] std::string_view toString(InputActionKind kind) noexcept;
[[nodiscard]] std::optional<InputActionKind> parseInputActionKind(std::string_view text) noexcept;
[[nodiscard]] std::string_view toString(InputDirection direction) noexcept;
[[nodiscard]] std::optional<InputDirection> parseInputDirection(std::string_view text) noexcept;

// What the player presses or moves for an action.
struct InputBinding
{
    // A key, a mouse button, a gamepad button, axis or stick, as platform::parseInputSource reads
    // it: "key:Space", "mouse:Left", "pad:South", "axis:LeftTrigger", "stick:Left".
    std::string input;
    InputDirection direction = InputDirection::Positive;

    bool operator==(const InputBinding&) const = default;
};

// Something the player does, such as "Jump" or "Move", that game code reads instead of keys.
struct InputAction
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
struct InputContext
{
    std::string name;
    bool activeAtStart = true;

    bool operator==(const InputContext&) const = default;
};

// The actions of a game and what plays them, which the player may change for themselves.
struct InputSettings
{
    std::vector<InputContext> contexts{InputContext{.name = "Gameplay"}};
    std::vector<InputAction> actions;

    bool operator==(const InputSettings&) const = default;
};

// A game project: a .dvxproj file whose directory holds the assets/ folder, the code/ folder of its
// game module when it has one, and the .devex/ cache of imported data and builds, which is never
// versioned.
struct Project
{
    std::string name;
    // Absolute directory of the project file.
    std::filesystem::path root;
    // Absolute path of the .dvxproj file.
    std::filesystem::path file;
    // The res:// path of the scene the player opens; empty for the first scene of the project.
    std::string startupScene;
    PhysicsSettings physics;
    AudioSettings audio;
    WindowSettings window;
    ExportSettings exportSettings;
    InputSettings input;

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
};

// Reads "[project format=1 name="My game" startup_scene="res://assets/scenes/Main.dvxscene"]" from
// the .dvxproj file, followed by optional [physics], [physics_layer], [audio], [audio_group],
// [window], [export], [export_scene], [export_folder], [input_context] and [input_action] sections.
[[nodiscard]] core::Result<Project> loadProject(const std::filesystem::path& projectFile);
// The same from the text of a project file, as exported games keep it; projectFile gives the
// project its root.
[[nodiscard]] core::Result<Project> parseProject(std::string_view text, const std::filesystem::path& projectFile);

// Writes the project's settings to its .dvxproj file. Sections are only written when they differ
// from the defaults.
[[nodiscard]] core::Result<void> saveProject(const Project& project);
[[nodiscard]] std::string writeProjectText(const Project& project);

// Writes a .dvxproj file into the directory and creates its assets/ and empty code/ folders.
[[nodiscard]] core::Result<Project> createProject(const std::filesystem::path& directory,
                                                  std::string_view name);

} // namespace devex::asset
