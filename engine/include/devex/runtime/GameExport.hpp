#pragma once

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
struct EngineBuild
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
[[nodiscard]] std::vector<EngineBuild> findEngineBuilds(const std::filesystem::path& binDirectory);

// The build of the configuration, preferring the running build.
[[nodiscard]] std::optional<EngineBuild> findEngineBuild(std::span<const EngineBuild> builds, std::string_view configuration);

// The file name of the exported executable, from the name of the game, without its extension.
[[nodiscard]] std::string executableName(std::string_view gameName);

// What an export needs from the asset database, gathered on the thread that owns it. The export
// itself can then run on another thread.
struct ExportPlan
{
    struct Asset
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
};

// Fails when the project has no scene, when imports are still running or failed, or when an exported
// scene or folder does not exist.
[[nodiscard]] core::Result<ExportPlan> planExport(const asset::AssetDatabase& database, const EngineBuild& engine);

struct ExportProgress
{
    std::string step;
    // From 0 to 1.
    float fraction = 0.0f;
};

struct ExportResult
{
    std::filesystem::path output;
    std::filesystem::path executable;
    std::size_t assets = 0;
    std::uint64_t packageBytes = 0;
    double seconds = 0.0;
};

// Builds the game code for the engine build, gathers the scenes and every asset they refer to,
// directly or through other assets, into the package, then copies the engine. The output folder must
// be empty, missing, or a previous export, which is replaced. Progress is reported on the calling
// thread; setting cancel stops the export at its next step.
[[nodiscard]] core::Result<ExportResult> exportGame(const ExportPlan& plan,
                                                   const std::function<void(const ExportProgress&)>& progress = {},
                                                   const std::atomic<bool>* cancel = nullptr);

// The assets that a cooked asset refers to: the assets named in a scene (prefabs, meshes, materials,
// scenes...), the textures of a material, the materials of a mesh, the meshes of a model.
[[nodiscard]] core::Result<std::vector<asset::AssetId>> assetReferences(asset::AssetType type,
                                                                        std::span<const std::byte> artifact);

// devex-editor --export <project.dvxproj> [--output <folder>] [--configuration <Release|Debug>]
// Imports the assets of the project, exports the game as its export settings say, and prints the
// progress. Returns the exit code of the process.
[[nodiscard]] int exportFromCommandLine(std::span<const std::string_view> arguments,
                                        const std::filesystem::path& binDirectory);

} // namespace devex::runtime
