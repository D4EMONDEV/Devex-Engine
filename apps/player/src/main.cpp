#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <cstdio>
#include <filesystem>
#include <optional>

namespace {

// Plays a project outside the editor: its startup scene, with its game code.
class Player final : public devex::runtime::Application
{
public:
    devex::core::Result<void> onStartup() override
    {
        const devex::asset::AssetDatabase* const database = assetDatabase();
        const devex::asset::Project& project = database->project();
        const std::optional<std::filesystem::path> path = startupScene(*database);
        if (!path)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "{} has no scene to play", project.name);
        }
        devex::core::Result<devex::scene::Scene> loaded = devex::scene::loadSceneFile(*path);
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }
        scene() = std::move(*loaded);
        window().setTitle(project.name);
        DEVEX_LOG_INFO("Playing {} ({})", project.name, project.resourcePath(*path));
        return {};
    }

private:
    // The startup scene of the project, or its first scene.
    [[nodiscard]] static std::optional<std::filesystem::path> startupScene(const devex::asset::AssetDatabase& database)
    {
        const devex::asset::Project& project = database.project();
        if (!project.startupScene.empty())
        {
            return project.absolutePath(project.startupScene);
        }
        for (const devex::asset::SourceFile& source : database.sources())
        {
            if (source.importer == "scene")
            {
                return project.absolutePath(source.path);
            }
        }
        return std::nullopt;
    }
};

} // namespace

// devex-player <project.dvxproj>
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fputs("usage: devex-player <project.dvxproj>\n", stderr);
        return 2;
    }
    return devex::runtime::run<Player>({
        .title = "Devex Player",
        .width = 1280,
        .height = 720,
        .loadGameCode = true,
        // Arguments come in the system code page, which std::filesystem::path converts.
        .project = std::filesystem::path(argv[1]),
    });
}
