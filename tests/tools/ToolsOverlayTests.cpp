#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AnimatorFile.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/CurveFile.hpp>
#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/asset/import/TilesetFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <set>
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

        std::set<std::uint32_t> surfaces;
        for (int frame = 0; frame < 6; ++frame)
        {
            if (frame == 2)
            {
                // The first menu of the tools: Edit.
                (*overlay)->openMenu(devex::tools::EditorMenu::Scene);
            }
            platform->pollEvents([](const devex::platform::Event&) {});
            (*overlay)->update(scene, std::chrono::milliseconds(16));
            devex::render::RenderWorld& world = renderer->beginFrame();
            (*overlay)->prepareRender(scene, world, devex::tools::PlayState::Editing);
            for (const devex::render::UiSurface& surface : world.uiSurfaces)
            {
                if (!surface.draws.empty())
                {
                    surfaces.insert(surface.id);
                }
            }
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
        }
        CHECK(renderer->stats().gpuMemoryBudget > 0);
        // The menu bar of the tools is the one of the editor, with the two menus they have, which
        // open in the layer over the window.
        CHECK(surfaces.contains(14));
        CHECK(surfaces.contains(15));
        CHECK_FALSE(surfaces.contains(16));
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

        // What the pages of the inspector show: a texture cut into sprites, a sound, a curve, an
        // animator, and sprite frames and a tileset of those sprites.
        const std::filesystem::path art = project.assetsDirectory() / "art";
        std::filesystem::create_directories(art);
        const std::filesystem::path data(DEVEX_TEST_DATA_DIRECTORY);
        std::filesystem::copy_file(data / "checker.png", art / "checker.png");
        std::filesystem::copy_file(data / "audio" / "tone.wav", art / "tone.wav");
        REQUIRE(devex::core::writeTextFile(art / "ease.dvxcurve", devex::asset::writeCurveFile(devex::asset::linearCurve())));
        devex::asset::AnimatorData states;
        states.entry = "Idle";
        states.parameters.push_back({.name = "Speed"});
        states.states.push_back({.name = "Idle"});
        states.states.push_back({.name = "Run", .blend = devex::asset::AnimatorBlend::Linear, .motions = {{}, {.threshold = 1.0f}},
                                 .parameter = "Speed"});
        states.transitions.push_back(
            {.from = "Idle", .to = "Run", .conditions = {{.parameter = "Speed", .test = devex::asset::AnimatorTest::Greater, .value = 0.1f}}});
        REQUIRE(devex::core::writeTextFile(art / "states.dvxanimator", devex::asset::writeAnimatorFile(states)));
        const auto settleAssets = [&] {
            (*database)->refresh();
            (*database)->waitForImports();
            static_cast<void>((*database)->update());
        };
        settleAssets();
        const std::optional<devex::asset::AssetId> checker = (*database)->findByPath("res://assets/art/checker.png");
        REQUIRE(checker.has_value());
        const std::array<devex::serialization::TextProperty, 3> grid{devex::serialization::TextProperty{"sprite_mode", std::string("grid")},
                                                                     devex::serialization::TextProperty{"columns", std::int64_t{2}},
                                                                     devex::serialization::TextProperty{"rows", std::int64_t{2}}};
        REQUIRE((*database)->setImportOptions(*checker, grid).has_value());
        settleAssets();
        std::vector<devex::asset::AssetId> sprites;
        for (const devex::asset::AssetId id : (*database)->sourceOf(*checker)->assets)
        {
            if ((*database)->find(id)->type == devex::asset::AssetType::Sprite)
            {
                sprites.push_back(id);
            }
        }
        REQUIRE_FALSE(sprites.empty());
        devex::asset::SpriteFramesData frames;
        frames.animations.push_back({.name = "spin", .fps = 8.0f, .loop = true, .frames = sprites});
        REQUIRE(devex::core::writeTextFile(art / "spin.dvxframes", devex::asset::writeSpriteFramesFile(frames)));
        devex::asset::TilesetData tiles;
        for (std::size_t index = 0; index < sprites.size(); ++index)
        {
            tiles.tiles.push_back({.id = static_cast<std::uint32_t>(index + 1), .sprite = sprites[index]});
        }
        tiles.tiles.front().frames = sprites;
        tiles.tiles.front().fps = 4.0f;
        REQUIRE(devex::core::writeTextFile(art / "tiles.dvxtileset", devex::asset::writeTilesetFile(tiles)));
        settleAssets();

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
            // The project manager is made with the interface of the engine, drawn into an image
            // of its own that ImGui shows.
            REQUIRE(world.uiSurfaces.size() == 1);
            CHECK_FALSE(world.uiSurfaces.front().draws.empty());
            CHECK(world.uiSurfaces.front().size.width > 0);
            REQUIRE(renderer->endFrame());
            CHECK_FALSE((*editor)->takeRequests().openProject.has_value());
        }
        CHECK(projectCodeChecks == 0);
        (*editor)->setAssetDatabase(database->get());
        // The text panel participates in docking and renders alongside the scene, including Play.
        (*editor)->openTextFile(project.file);

        std::set<std::uint32_t> surfaces;
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
            for (const devex::render::UiSurface& surface : world.uiSurfaces)
            {
                surfaces.insert(surface.id);
            }
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

        // FileSystem and Output are made with the interface of the engine as well, each in its image; the
        // project manager is gone.
        CHECK(surfaces.contains(2));
        CHECK(surfaces.contains(3));
        // The scene tree too.
        CHECK(surfaces.contains(5));
        CHECK_FALSE(surfaces.contains(1));
        // The inspector, which says to choose something while nothing is selected.
        CHECK(surfaces.contains(6));
        // The monitors of Statistics.
        CHECK(surfaces.contains(12));
        // The frame of the editor: its menu bar, its status bar, and the tabs of the scenes over the
        // toolbar of the view. Nothing opened a menu or rested on a button: the layer over them is
        // not drawn.
        CHECK(surfaces.contains(14));
        CHECK(surfaces.contains(16));
        CHECK(surfaces.contains(17));
        CHECK_FALSE(surfaces.contains(15));
        // While the game plays, the view is framed by an overlay of that interface over its image.
        CHECK(surfaces.contains(21));

        // The inspector of entities is made with the interface of the engine as well: an entity that
        // carries every component of the engine shows a row for each of their fields, and several
        // entities the components they share.
        const auto inspectorFrames = [&](int count, std::uint32_t shown = 6) {
            bool drawn = false;
            for (int frame = 0; frame < count; ++frame)
            {
                platform->pollEvents([](const devex::platform::Event&) {});
                (*editor)->update(scene, std::chrono::milliseconds(16), PlayState::Editing);
                scene.updateTransforms();
                devex::render::RenderWorld& world = renderer->beginFrame();
                (*editor)->prepareRender(scene, world, PlayState::Editing);
                for (const devex::render::UiSurface& surface : world.uiSurfaces)
                {
                    drawn |= surface.id == shown && !surface.draws.empty();
                }
                const devex::core::Result<void> presented = renderer->endFrame();
                if (!presented)
                {
                    FAIL(std::format("inspector frame {}: {}", frame, presented.error()));
                }
            }
            return drawn;
        };
        const devex::scene::Entity everything = scene.createEntity("Everything");
        for (const devex::scene::ComponentType& type : devex::scene::componentRegistry().types())
        {
            static_cast<void>(type.emplace(scene, everything));
        }
        const std::array<devex::core::Uuid, 1> alone{scene.uuid(everything)};
        (*editor)->select(alone);
        CHECK(inspectorFrames(3));
        std::vector<devex::core::Uuid> level;
        for (devex::scene::Entity top = scene.firstRoot(); top.isValid(); top = scene.nextSibling(top))
        {
            if (top != everything)
            {
                level.push_back(scene.uuid(top));
            }
        }
        (*editor)->select(level);
        CHECK(inspectorFrames(3));
        (*editor)->select({});
        scene.destroyEntity(everything);
        static_cast<void>(inspectorFrames(1));

        // A page for each kind of asset, and for the others their name and their file.
        for (const char* path : {"res://assets/art/checker.png", "res://assets/art/tone.wav", "res://assets/art/ease.dvxcurve",
                                 "res://assets/art/states.dvxanimator", "res://assets/art/spin.dvxframes", "res://assets/art/tiles.dvxtileset",
                                 "res://assets/scenes/menu.dvxscene"})
        {
            const std::optional<devex::asset::AssetId> id = (*database)->findByPath(path);
            REQUIRE(id.has_value());
            INFO(path);
            (*editor)->selectAsset(*id);
            CHECK(inspectorFrames(3));
        }
        (*editor)->selectAsset({});

        // The windows of settings, the export, the debugging window and the dialogs are made with the
        // interface of the engine as well, each in its image; Project Settings makes every section.
        using devex::tools::EditorWindow;
        for (const auto& [opened, surface] : std::array<std::pair<EditorWindow, std::uint32_t>, 9>{{
                 {EditorWindow::EditorSettings, 7},
                 {EditorWindow::ProjectSettings, 8},
                 {EditorWindow::Export, 9},
                 {EditorWindow::Debugging, 10},
                 {EditorWindow::NewScript, 11},
                 {EditorWindow::About, 11},
                 {EditorWindow::Profiler, 13},
                 {EditorWindow::Animation, 18},
                 {EditorWindow::Animator, 19},
             }})
        {
            INFO(static_cast<int>(opened));
            (*editor)->openWindow(opened);
            CHECK(inspectorFrames(3, surface));
        }

        // The Animator panel shows the graph of the controller chosen in FileSystem, its states as nodes
        // and its transitions as lines of that interface.
        const std::optional<devex::asset::AssetId> controller = (*database)->findByPath("res://assets/art/states.dvxanimator");
        REQUIRE(controller.has_value());
        (*editor)->selectAsset(*controller);
        CHECK(inspectorFrames(3, 19));
        (*editor)->selectAsset({});

        // The Script screen shows the file it is given in an area of text of that interface.
        (*editor)->openTextFile(project.file);
        CHECK(inspectorFrames(3, 20));

        // The menus of the menu bar are menus of that interface, in a layer over the whole window that
        // is drawn while one is open.
        using devex::tools::EditorMenu;
        for (const EditorMenu title : {EditorMenu::Scene, EditorMenu::Edit, EditorMenu::Project, EditorMenu::Editor, EditorMenu::Help})
        {
            INFO(static_cast<int>(title));
            (*editor)->openMenu(title);
            CHECK(inspectorFrames(3, 15));
        }
        (*editor)->closeMenu();
        CHECK_FALSE(inspectorFrames(2, 15));

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
