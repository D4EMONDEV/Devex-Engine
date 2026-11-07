#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

using namespace devex;
namespace fs = std::filesystem;

TEST_CASE("The export window follows the rows that come and go with a platform and a release key", "[tools][export][gpu]")
{
    const fs::path directory = fs::temp_directory_path() / ("devex-export-ui-" + core::Uuid::generate().toString());
    {
        auto project = asset::createProject(directory, "Export window test");
        REQUIRE(project);
        auto platform = platform::Platform::create();
        REQUIRE(platform);
        auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
        REQUIRE(window);
        auto renderer = render::Renderer::create(*platform, *window, {.validation = false});
        REQUIRE(renderer);

        core::JobSystem jobs{1};
        auto database = asset::AssetDatabase::open(*project, jobs);
        REQUIRE(database);
        auto tools = tools::ToolsOverlay::create(*platform, *window, *renderer, tools::ToolsMode::Editor, directory / "user.dvx");
        REQUIRE(tools);
        (*tools)->setAssetDatabase(database->get());
        (*tools)->showExportWindow();

        scene::Scene scene;
        // As the window changes them: the rows are made again at the next frame.
        const auto show = [&](const std::string& system, bool release) {
            asset::Project changed = (*database)->project();
            changed.exportSettings.platform = system;
            changed.exportSettings.android.release = release;
            REQUIRE((*database)->updateProject(changed));
            for (int frame = 0; frame < 3; ++frame)
            {
                platform->pollEvents([](const platform::Event&) {});
                (*tools)->update(scene, std::chrono::milliseconds(16), tools::PlayState::Editing);
                render::RenderWorld& world = renderer->beginFrame();
                (*tools)->prepareRender(scene, world, tools::PlayState::Editing);
                REQUIRE(renderer->endFrame());
            }
        };
        show("android", true);
        // The rows of the key go, and nothing reaches for them.
        show("android", false);
        show("android", true);
        // Nor from another platform, on the way back.
        show("desktop", true);
        show("android", false);
        show("desktop", false);
    }
    std::error_code error;
    fs::remove_all(directory, error);
}
