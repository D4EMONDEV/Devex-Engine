#include "tools/EditorDock.hpp"
#include "tools/FileSearch.hpp"
#include "tools/Theme.hpp"

#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

using devex::tools::detail::matchesFileSearch;

TEST_CASE("File search matches separate path and name words in any order", "[tools][filesystem]")
{
    CHECK(matchesFileSearch("res://code/PlayerMovement.cs", "player CS"));
    CHECK(matchesFileSearch("res://code/PlayerMovement.cs", " cs \tplayer  "));
    CHECK(matchesFileSearch("res://assets/imports/sheet.png/sheet_82", "sheet 82"));
    CHECK(matchesFileSearch("res://assets/scenes/Level.dvxscene", "scenes level"));
    CHECK_FALSE(matchesFileSearch("res://code/PlayerMovement.cs", "player cpp"));
    CHECK_FALSE(matchesFileSearch("res://code/PlayerMovement.cs", "enemy"));
    CHECK(matchesFileSearch("res://code/PlayerMovement.cs", " \t\n "));
    CHECK(matchesFileSearch("res://code/PlayerMovement.cs", ""));
    CHECK_FALSE(matchesFileSearch("", "player"));
}

TEST_CASE("FileSystem searches collapsed code folders and opens only an activated result",
          "[tools][filesystem][gpu]")
{
    using namespace devex;
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("devex-search-" + core::Uuid::generate().toString());
        ~Directory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } directory;
    auto project = asset::createProject(directory.path, "Search UI");
    REQUIRE(project);
    std::filesystem::create_directories(project->codeDirectory() / "Gameplay");
    REQUIRE(
        core::writeTextFile(project->codeDirectory() / "Gameplay/Needle.cs", "class Needle {}\n"));
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window =
        platform->createWindow({.width = 1000, .height = 800, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    auto editor = tools::ToolsOverlay::create(
        *platform, *window, *renderer, tools::ToolsMode::Editor, directory.path / "settings.dvx");
    REQUIRE(editor);
    (*editor)->setAssetDatabase(database->get());
    scene::Scene scene;
    const auto frame = [&](math::Vec2 mouse = {}, bool left = false, std::string_view typed = {},
                           platform::Key key = platform::Key::Unknown)
    {
        platform->pollEvents([](const auto&) {});
        auto& input = const_cast<platform::Input&>(platform->toolsInput());
        input.moveMouse(mouse, {});
        input.setMouseButtonDown(platform::MouseButton::Left, left);
        input.addTypedText(typed);
        for (auto candidate :
             {platform::Key::Enter, platform::Key::Down, platform::Key::End, platform::Key::Escape})
            input.setKeyDown(candidate, key == candidate);
        (*editor)->update(scene, std::chrono::milliseconds(16), tools::PlayState::Editing);
        auto& world = renderer->beginFrame();
        (*editor)->prepareRender(scene, world, tools::PlayState::Editing);
        bool script = false;
        for (const auto& surface : world.uiSurfaces)
            script |= surface.id == 20 && !surface.draws.empty();
        REQUIRE(renderer->endFrame());
        return script;
    };
    CHECK_FALSE(frame());
    frame();
    const auto metrics = tools::detail::deriveThemeMetrics({}, window->displayScale());
    const float scale = tools::detail::effectiveInterfaceScale({}, window->displayScale());
    const auto dock = tools::detail::placeDock(
        tools::detail::DockLayout::defaults(true), [](std::string_view) { return true; },
        {0.0f, metrics.menuBarHeight}, {1000.0f, 800.0f - metrics.statusBarHeight}, metrics.dockGap,
        metrics.tabHeight);
    const auto& files =
        dock.contents[static_cast<std::size_t>(tools::detail::DockSlot::LeftBottom)];
    const math::Vec2 search{files.min.x + metrics.panelPadding + 70.0f * scale,
                            files.min.y + metrics.panelPadding + 14.0f * scale};
    frame(search, true);
    frame(search);
    REQUIRE((*editor)->wantsTextInput());
    frame(search, false, "needle CS");
    frame();
    CHECK_FALSE(frame({}, false, {}, platform::Key::Enter)); // Browse the results.
    frame();
    CHECK_FALSE(frame({}, false, {}, platform::Key::Down)); // Selection is not activation.
    frame();
    CHECK_FALSE(frame({}, false, {}, platform::Key::End));
    frame();
    frame({}, false, {}, platform::Key::Enter);
    CHECK(frame()); // Activating the sole result opens the nested C# script in Devex Script.
}
