#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetSource.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Error.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Exporting a game: a folder that runs without the editor, the project or its sources.
//
//     <Game>.exe          devex-player, renamed after the game, with its icon
//     <Game>.dvxpak       the settings, scenes and cooked assets of the game (asset::PackageWriter)
//     Game.dll            the game module, built for the engine build
//     Game.Scripts.dll    the C# code of the game, with .NET in managed/ when the game uses C#
//     devex-engine.dll    and the other libraries of the engine build, with the C++ runtime when
//                         the engine build provides it (bin/redist)
//     shaders/            the compiled shaders; resources/ too for Debug builds, for the tools overlay
//     devex-export.txt    marks the folder as an export, which the next export may replace
namespace devex::runtime {

// A build of the engine that games can be exported with: bin/ holds devex-player and the libraries,
// cmake/ the package that game modules are built against.
struct DEVEX_API EngineBuild
{
    // The name of its folder, such as "x64-release".
    std::string name;
    // "Debug" or "Release".
    std::string configuration;
    std::filesystem::path directory;

    [[nodiscard]] std::filesystem::path binDirectory() const
    {
        return directory / "bin";
    }

    [[nodiscard]] std::filesystem::path cmakeDirectory() const
    {
        return directory / "cmake";
    }
};

// The engine builds found in the folder of the build that binDirectory belongs to and in the folders
// next to it, the running build first.
[[nodiscard]] DEVEX_API std::vector<EngineBuild> findEngineBuilds(const std::filesystem::path& binDirectory);

// The build of the configuration, preferring the running build.
[[nodiscard]] DEVEX_API std::optional<EngineBuild> findEngineBuild(std::span<const EngineBuild> builds, std::string_view configuration);

// The file name of the exported executable, from the name of the game, without its extension.
[[nodiscard]] DEVEX_API std::string executableName(std::string_view gameName);

// What an export is made for. The package of a game for Android has its BC textures in ASTC, which
// the GPUs of phones read.
enum class ExportPlatform : std::uint8_t
{
    // The other desktop system: Linux from Windows, and Windows from Linux. See prepareOtherDesktopExport.
    OtherDesktop,
    Desktop,
    Android,
};

// A build of the engine for Android, found next to the editor. android/ in its folder holds the Gradle
// project of the player, with the libraries of its ABI.
struct DEVEX_API AndroidBuild
{
    // "arm64-v8a" or "x86_64".
    std::string abi;
    EngineBuild engine;
};

[[nodiscard]] DEVEX_API std::vector<AndroidBuild> findAndroidBuilds(const std::filesystem::path& binDirectory);

// What building for Android takes, found where it usually is or through the environment.
struct DEVEX_API AndroidTools
{
    std::filesystem::path sdk;
    std::filesystem::path ndk;
    std::filesystem::path java;
    std::filesystem::path adb;

    // What is missing, for a game with C++ code or not; empty when everything is there.
    [[nodiscard]] std::string problem(bool needsNdk) const;
};

[[nodiscard]] DEVEX_API AndroidTools findAndroidTools();

// What an export needs from the asset database, gathered on the thread that owns it. The export
// itself can then run on another thread.
struct DEVEX_API ExportPlan
{
    struct DEVEX_API Asset
    {
        asset::AssetInfo info;
        // The res:// path of the source file of a main asset, empty for the other assets of a file.
        std::string path;
        std::filesystem::path artifact;
    };

    asset::Project project;
    EngineBuild engine;
    // Absolute.
    std::filesystem::path output;
    // Every imported asset of the project.
    std::vector<Asset> assets;
    // The scenes the game starts from: the startup scene first, then the exported scenes.
    std::vector<asset::AssetId> scenes;
    // The assets of the folders that are always exported.
    std::vector<asset::AssetId> includedAssets;
    // The source image of the icon of the game.
    std::optional<std::filesystem::path> icon;
    // The project has C# code: its assembly and the .NET runtime go with the game.
    bool managed = false;
    // Writes the package only, without building the code or copying the engine, as tests do.
    bool packageOnly = false;
    ExportPlatform platform = ExportPlatform::Desktop;
    // The archive the game ends up in, in which case the output folder is only where it is put together: a folder written
    // under Windows cannot say that a Linux program is executable.
    std::filesystem::path archive;
    // For Android: the builds of the chosen architectures, and the key of a release.
    std::vector<AndroidBuild> androidBuilds;
    asset::AndroidSigning signing;
    // Whether a package for Android has its BC textures in ASTC.
    bool androidConvertsTextures = true;
};

// Fails when the project has no scene, when imports are still running or failed, or when an exported
// scene or folder does not exist.
[[nodiscard]] DEVEX_API core::Result<ExportPlan> planExport(const asset::AssetDatabase& database, const EngineBuild& engine);

struct DEVEX_API ExportProgress
{
    std::string step;
    // From 0 to 1.
    float fraction = 0.0f;
};

struct DEVEX_API ExportResult
{
    std::filesystem::path output;
    // Empty when the game cannot run here, as with one for the other desktop system in an archive.
    std::filesystem::path executable;
    std::filesystem::path archive;
    std::size_t assets = 0;
    std::uint64_t packageBytes = 0;
    double seconds = 0.0;
};

// Builds the game code for the engine build, gathers the scenes and every asset they refer to,
// directly or through other assets, into the package, then copies the engine. The output folder must
// be empty, missing, or a previous export, which is replaced. Progress is reported on the calling
// thread; setting cancel stops the export at its next step.
[[nodiscard]] DEVEX_API core::Result<ExportResult> exportGame(const ExportPlan& plan,
                                                             const std::function<void(const ExportProgress&)>& progress = {},
                                                             const std::atomic<bool>* cancel = nullptr);

// The assets that a cooked asset refers to: the assets named in a scene (prefabs, meshes, materials,
// scenes...), the textures of a material, the materials of a mesh, the meshes of a model.
[[nodiscard]] DEVEX_API core::Result<std::vector<asset::AssetId>> assetReferences(asset::AssetType type,
                                                                                  std::span<const std::byte> artifact);

// devex-editor --export <project.dvxproj> [--output <folder>] [--configuration <Release|Debug>] [--platform <system>]
// The system is desktop, the one of the editor, android, or the other desktop system by its name, windows or linux.
// Imports the assets of the project, exports the game as its export settings say, and prints the
// progress. Returns the exit code of the process.
[[nodiscard]] DEVEX_API int exportFromCommandLine(std::span<const std::string_view> arguments,
                                                  const std::filesystem::path& binDirectory);

// The engine of the other desktop system, in platforms/ next to the bin/ of the editor, where the installer puts it.
// Fails when it is not there, or is not the version of this editor, saying so.
[[nodiscard]] DEVEX_API core::Result<EngineBuild> findOtherDesktopEngine(const std::filesystem::path& binDirectory);
// Fills a plan for an export to the other desktop system: to the folder given, or else the one of the project settings.
// Fails when the game has C++ code that does something, which only builds on the system it runs on. A module a new project
// starts with registers nothing, and is left out.
[[nodiscard]] DEVEX_API core::Result<void> prepareOtherDesktopExport(ExportPlan& plan, const std::filesystem::path& binDirectory,
                                                                     const std::optional<std::filesystem::path>& output = std::nullopt);

// Fills a plan for an export to Android from the settings of its project.
[[nodiscard]] DEVEX_API core::Result<void> prepareAndroidExport(ExportPlan& plan, const std::filesystem::path& binDirectory);
// The settings the Gradle project reads, as a properties file.
[[nodiscard]] DEVEX_API std::string androidGradleProperties(const ExportPlan& plan, bool hasIcon);
// Makes the APK or the AAB of a plan prepared for Android; exportGame comes here for one.
[[nodiscard]] DEVEX_API core::Result<ExportResult> exportAndroid(const ExportPlan& plan, const std::function<void(const ExportProgress&)>& progress,
                                                                 const std::atomic<bool>* cancel);

} // namespace devex::runtime
