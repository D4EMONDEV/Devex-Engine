#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>

TEST_CASE("Settings controls occupy the top of the window at desktop and compact sizes",
          "[tools][settings][gpu]")
{
    using namespace devex;
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("devex-settings-layout-" + core::Uuid::generate().toString());
        ~Directory()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } directory;
    auto project = asset::createProject(directory.path, "Settings");
    REQUIRE(project);
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    for (const auto size : {math::Extent2D{800, 600}, math::Extent2D{1600, 1000}})
    {
        CAPTURE(size.width, size.height);
        auto window = platform->createWindow(
            {.width = size.width, .height = size.height, .vulkan = true, .hidden = true});
        REQUIRE(window);
        auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer);
        auto overlay = tools::ToolsOverlay::create(
            *platform, *window, *renderer, tools::ToolsMode::Editor, directory.path / "User.dvx");
        REQUIRE(overlay);
        (*overlay)->setAssetDatabase(database->get());
        (*overlay)->setVisible(true);
        scene::Scene scene;
        for (const auto page :
             {tools::EditorWindow::ProjectSettings, tools::EditorWindow::EditorSettings})
        {
            (*overlay)->openWindow(page);
            const auto surfaceId = page == tools::EditorWindow::ProjectSettings ? 8u : 7u;
            bool checked = false;
            for (int frame = 0; frame < 4; ++frame)
            {
                platform->pollEvents([](const auto&) {});
                (*overlay)->update(scene, std::chrono::milliseconds(16));
                auto& world = renderer->beginFrame();
                (*overlay)->prepareRender(scene, world, tools::PlayState::Editing);
                if (frame == 3)
                    for (const auto& surface : world.uiSurfaces)
                    {
                        if (surface.id != surfaceId)
                            continue;
                        math::Vec2 minimum{1.0e9f};
                        float last = 0;
                        for (const auto& draw : surface.draws)
                            if (draw.kind == render::UiDrawKind::Text ||
                                draw.kind == render::UiDrawKind::Glyph)
                                for (std::uint32_t i = 0; i < draw.indexCount; ++i)
                                {
                                    const auto position =
                                        surface.vertices[surface.indices[draw.firstIndex + i]]
                                            .position;
                                    minimum = math::min(minimum, position);
                                    last = std::max(last, position.y);
                                }
                        CHECK(minimum.x < static_cast<float>(surface.size.width) * 0.1f);
                        CHECK(minimum.y < static_cast<float>(surface.size.height) * 0.2f);
                        CHECK(last - minimum.y > static_cast<float>(surface.size.height) * 0.6f);
                        checked = true;
                    }
                REQUIRE(renderer->endFrame());
            }
            CHECK(checked);
        }
    }
}
