#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

TEST_CASE("The tools overlay renders over the scene without validation errors", "[tools][gpu]")
{
    std::vector<std::string> errors;
    const devex::core::LogSinkId sink =
        devex::core::addLogSink([&errors](const devex::core::LogRecord& record) {
            if (record.level >= devex::core::LogLevel::Error)
            {
                errors.emplace_back(record.message);
            }
        });
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow(
            {.title = "Devex tools tests", .width = 800, .height = 600, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        const std::filesystem::path settings =
            std::filesystem::temp_directory_path() / "devex-tools-test.ini";
        std::filesystem::remove(settings);
        auto overlay = devex::tools::ToolsOverlay::create(*platform, *window, *renderer, settings);
        if (!overlay)
        {
            FAIL(std::format("{}", overlay.error()));
        }

        devex::scene::Scene scene;
        const devex::scene::Entity entity = scene.createEntity("Inspected");
        scene.add<devex::scene::Transform>(entity);
        (*overlay)->setVisible(true);

        for (int frame = 0; frame < 5; ++frame)
        {
            platform->pollEvents([](const devex::platform::Event&) {});
            (*overlay)->update(scene, std::chrono::milliseconds(16));
            static_cast<void>(renderer->beginFrame());
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
        }
        CHECK(renderer->stats().gpuMemoryBudget > 0);
    }
    devex::core::removeLogSink(sink);

    for (const std::string& error : errors)
    {
        UNSCOPED_INFO(error);
    }
    CHECK(errors.empty());
}

TEST_CASE("The editor opens the project's scene and renders its viewport without validation errors", "[tools][gpu]")
{
    std::vector<std::string> errors;
    const devex::core::LogSinkId sink =
        devex::core::addLogSink([&errors](const devex::core::LogRecord& record) {
            if (record.level >= devex::core::LogLevel::Error)
            {
                errors.emplace_back(record.message);
            }
        });
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-editor-" + devex::core::Uuid::generate().toString());
    {
        // A project with one scene: a light, a camera and a cube.
        const devex::asset::Project project = *devex::asset::createProject(root, "Editor test");
        devex::scene::Scene authored;
        const devex::scene::Entity cube = authored.createEntity("Cube");
        authored.add<devex::scene::Transform>(cube);
        authored.add<devex::scene::MeshRenderer>(cube, devex::scene::MeshRenderer{.mesh = devex::asset::builtin::cubeMesh});
        const devex::scene::Entity light = authored.createEntity("Lamp");
        authored.add<devex::scene::Transform>(light, devex::scene::Transform{.position = {1.0f, 2.0f, 1.0f}});
        authored.add<devex::scene::PointLight>(light);
        const devex::scene::Entity camera = authored.createEntity("Camera");
        authored.add<devex::scene::Transform>(camera, devex::scene::Transform{.position = {0.0f, 1.0f, 5.0f}});
        authored.add<devex::scene::Camera>(camera);
        REQUIRE(devex::scene::saveSceneFile(authored, project.assetsDirectory() / "scenes" / "level.dvxscene"));

        devex::core::JobSystem jobs(1);
        auto database = devex::asset::AssetDatabase::open(project, jobs, {.watchFiles = false});
        REQUIRE(database.has_value());

        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }
        auto cubeMesh = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cubeMesh.has_value());

        auto editor = devex::tools::ToolsOverlay::create(*platform, *window, *renderer, root / "editor.ini",
                                                         devex::tools::ToolsMode::Editor, root / "recent.dvx");
        if (!editor)
        {
            FAIL(std::format("{}", editor.error()));
        }
        CHECK((*editor)->isVisible());
        (*editor)->setVisible(false);
        CHECK((*editor)->isVisible());
        (*editor)->setAssetDatabase(database->get());

        devex::scene::Scene scene;
        using devex::tools::PlayState;
        for (int frame = 0; frame < 8; ++frame)
        {
            // Two frames play, one is paused, then editing resumes.
            const PlayState playState = frame >= 3 && frame < 5 ? PlayState::Playing
                                        : frame == 5            ? PlayState::Paused
                                                                : PlayState::Editing;
            platform->pollEvents([](const devex::platform::Event&) {});
            (*editor)->update(scene, std::chrono::milliseconds(16), playState);
            scene.updateTransforms();

            devex::render::RenderWorld& world = renderer->beginFrame();
            for ([[maybe_unused]] auto [entity, transform, mesh] :
                 scene.view<devex::scene::WorldTransform, devex::scene::MeshRenderer>())
            {
                world.meshes.push_back({.mesh = *cubeMesh, .transform = transform.matrix, .objectId = entity.index + 1});
            }
            (*editor)->prepareRender(scene, world, playState);
            CHECK(world.viewport.width > 0);
            if (playState == PlayState::Editing)
            {
                // The grid and the icon of the light.
                CHECK_FALSE(world.sceneLines.empty());
                CHECK_FALSE(world.overlayLines.empty());
            }
            else
            {
                CHECK(world.sceneLines.empty());
            }
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            const devex::tools::EditorRequests requests = (*editor)->takeRequests();
            CHECK_FALSE(requests.quit);
        }

        // The project's only scene opened, without unsaved changes.
        CHECK(scene.entityCount() == 3);
        CHECK((*editor)->confirmClose());
        (*editor)->setAssetDatabase(nullptr);
        editor->reset();
        renderer->destroyMesh(*cubeMesh);
    }
    devex::core::removeLogSink(sink);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    for (const std::string& error : errors)
    {
        UNSCOPED_INFO(error);
    }
    CHECK(errors.empty());
}
