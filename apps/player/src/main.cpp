#include <devex/asset/Package.hpp>
#include <devex/core/Log.hpp>
#include <devex/runtime/Application.hpp>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>

namespace {

// Plays a game: a project during its development, or the package of an exported game. It opens
// the startup scene, with the game code.
class Player final : public devex::runtime::Application
{
public:
    devex::core::Result<void> onStartup() override
    {
        const devex::asset::AssetSource* const source = assetSource();
        if (source == nullptr)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "there is no game to play");
        }
        const devex::asset::Project& settings = source->project();
        const std::optional<devex::asset::AssetId> startup = startupScene(*source);
        if (!startup)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "{} has no scene to play", settings.name);
        }
        if (devex::core::Result<void> loaded = loadScene(*startup); !loaded)
        {
            return loaded;
        }
        DEVEX_LOG_INFO("Playing {}", settings.name);
        return {};
    }

private:
    // The startup scene of the game, or its first scene.
    [[nodiscard]] static std::optional<devex::asset::AssetId> startupScene(const devex::asset::AssetSource& source)
    {
        if (const std::string& startup = source.project().startupScene; !startup.empty())
        {
            return source.findByPath(startup);
        }
        const std::vector<devex::asset::AssetInfo> scenes = source.assets(devex::asset::AssetType::Scene);
        return scenes.empty() ? std::nullopt : std::optional(scenes.front().id);
    }
};

} // namespace

// devex-player <project.dvxproj>: plays a project.
// devex-player --package <game.dvxpak>: plays an exported game.
// Without arguments, plays the package named like the executable, next to it: Game.exe plays Game.dvxpak.
int main(int argc, char** argv)
{
    devex::runtime::ApplicationConfig config{
        .title = "Devex Player",
        .loadGameCode = true,
        .useProjectWindowSettings = true,
    };
    // Arguments come in the system code page, which std::filesystem::path converts.
    if (argc >= 3 && std::string_view(argv[1]) == "--package")
    {
        config.package = std::filesystem::path(argv[2]);
    }
    else if (argc >= 2 && !std::string_view(argv[1]).starts_with("--"))
    {
        config.project = std::filesystem::path(argv[1]);
    }
    else if (argc == 1)
    {
        std::filesystem::path package = std::filesystem::path(argv[0]).stem();
        package += devex::asset::packageExtension;
        config.package = package;
    }
    else
    {
        std::fputs("usage: devex-player [<project.dvxproj> | --package <game.dvxpak>]\n", stderr);
        return 2;
    }
    return devex::runtime::run<Player>(config);
}
