#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/File.hpp>
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

TEST_CASE("The editor opens the project's scenes in tabs and renders its viewport without validation errors",
          "[tools][gpu]")
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
        // A project with two scenes: a level with a light, a camera and a cube, and a menu.
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
        devex::scene::Scene menu;
        static_cast<void>(menu.createEntity("Title"));
        REQUIRE(devex::scene::saveSceneFile(menu, project.assetsDirectory() / "scenes" / "menu.dvxscene"));
        // The editor left the project with both scenes open and the level on screen.
        REQUIRE(devex::core::writeTextFile(project.cacheDirectory() / "editor.dvx",
                                           "[editor format=2 active_scene=\"res://assets/scenes/level.dvxscene\"]\n"
                                           "[scene path=\"res://assets/scenes/menu.dvxscene\"]\n"
                                           "[scene path=\"res://assets/scenes/level.dvxscene\" x=0 y=1 z=0 yaw=-30 "
                                           "pitch=-20 distance=8 speed=6]\n"));

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
                                                         devex::tools::ToolsMode::Editor, root / "user.dvx");
        if (!editor)
        {
            FAIL(std::format("{}", editor.error()));
        }
        CHECK((*editor)->isVisible());
        (*editor)->setVisible(false);
        CHECK((*editor)->isVisible());

        int projectCodeChecks = 0;
        (*editor)->setProjectCodeStatusProvider([&](const devex::asset::Project& inspected) {
            CHECK(inspected.file == project.file);
            ++projectCodeChecks;
            return devex::tools::ProjectCodeStatus{.needsUpdate = true,
                                                   .message = "Game code was built for an older engine API."};
        });

        // Without a project, the project manager is shown.
        devex::scene::Scene scene;
        using devex::tools::PlayState;
        for (int frame = 0; frame < 2; ++frame)
        {
            platform->pollEvents([](const devex::platform::Event&) {});
            (*editor)->update(scene, std::chrono::milliseconds(16), PlayState::Editing);
            devex::render::RenderWorld& world = renderer->beginFrame();
            (*editor)->prepareRender(scene, world, PlayState::Editing);
            REQUIRE(renderer->endFrame());
            CHECK_FALSE((*editor)->takeRequests().openProject.has_value());
        }
        CHECK(projectCodeChecks == 0);
        (*editor)->setAssetDatabase(database->get());
        // The text panel participates in docking and renders alongside the scene, including Play.
        (*editor)->openTextFile(project.file);

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

        // Both scenes opened, the level on screen and the menu in a background tab, without unsaved changes.
        CHECK(scene.entityCount() == 3);
        std::size_t backgroundEntities = 0;
        std::size_t backgroundScenes = 0;
        (*editor)->forEachBackgroundScene([&](devex::scene::Scene& background) {
            ++backgroundScenes;
            backgroundEntities += background.entityCount();
        });
        CHECK(backgroundScenes == 1);
        CHECK(backgroundEntities == 1);
        CHECK((*editor)->confirmClose(scene));
        (*editor)->setAssetDatabase(nullptr);
        backgroundScenes = 0;
        (*editor)->forEachBackgroundScene([&](devex::scene::Scene&) { ++backgroundScenes; });
        CHECK(backgroundScenes == 0);

        // Returning home checks the project once, and the warning stays cached while drawing.
        for (int frame = 0; frame < 3; ++frame)
        {
            platform->pollEvents([](const devex::platform::Event&) {});
            (*editor)->update(scene, std::chrono::milliseconds(16), PlayState::Editing);
            static_cast<void>(renderer->beginFrame());
            REQUIRE(renderer->endFrame());
        }
        CHECK(projectCodeChecks == 1);
        // Replacing the provider invalidates the cached warning, for example after a rebuild.
        (*editor)->setProjectCodeStatusProvider([&](const devex::asset::Project& inspected) {
            CHECK(inspected.file == project.file);
            ++projectCodeChecks;
            return devex::tools::ProjectCodeStatus{};
        });
        platform->pollEvents([](const devex::platform::Event&) {});
        (*editor)->update(scene, std::chrono::milliseconds(16), PlayState::Editing);
        static_cast<void>(renderer->beginFrame());
        REQUIRE(renderer->endFrame());
        CHECK(projectCodeChecks == 2);

        // The open scenes are remembered for the next session.
        const devex::core::Result<std::string> settings = devex::core::readTextFile(project.cacheDirectory() / "editor.dvx");
        REQUIRE(settings.has_value());
        CHECK(settings->find("menu.dvxscene") != std::string::npos);
        CHECK(settings->find("active_scene=\"res://assets/scenes/level.dvxscene\"") != std::string::npos);
        // The user's settings keep the theme and the project.
        const devex::core::Result<std::string> user = devex::core::readTextFile(root / "user.dvx");
        REQUIRE(user.has_value());
        CHECK(user->find("[theme") != std::string::npos);
        CHECK(user->find("Editor test.dvxproj") != std::string::npos);
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
