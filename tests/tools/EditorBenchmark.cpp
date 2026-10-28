// Where the time of an editor frame goes, on a copy of the sandbox project with its camera selected:
// the zones of the profiler, a frame's worth each. Hidden, run on demand:
//     devex_tools_tests "Editor frame benchmark"
//     devex_tools_tests "Script screen benchmark"
//     devex_tools_tests "Animator panel benchmark"
// And how the editor looks, in pictures of its window written to DEVEX_PICTURES (the temporary
// directory without it):
//     devex_tools_tests "Editor pictures"
#include <devex/asset/Project.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

using EditorSetup = std::function<void(devex::tools::ToolsOverlay&, const std::filesystem::path&, const devex::asset::AssetDatabase&)>;

// The editor running, for what is done with it once its project is loaded.
struct EditorRun
{
    devex::tools::ToolsOverlay& editor;
    devex::render::Renderer& renderer;
    devex::platform::Platform& platform;
    devex::scene::Scene& scene;
    // One frame of the editor: its update and its drawing.
    std::function<void()> frame;
};

// Runs the editor on a copy of the sandbox in a hidden window of a size, lets `setup` open what is
// wanted once the project is loaded and its camera selected, then hands it to `run` a few frames later.
void runEditor(const EditorSetup& setup, const std::function<void(EditorRun&)>& run, devex::math::Extent2D size = {2560, 1369})
{
    const std::filesystem::path source = std::filesystem::path(DEVEX_TEST_DATA_DIRECTORY) / ".." / ".." / "samples" / "sandbox";
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "devex-bench-sandbox";
    std::filesystem::remove_all(root);
    std::filesystem::copy(source, root, std::filesystem::copy_options::recursive);
    auto project = devex::asset::loadProject(root / "Sandbox.dvxproj");
    REQUIRE(project.has_value());
    devex::core::JobSystem jobs(4);
    auto database = devex::asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());
    (*database)->waitForImports();
    static_cast<void>((*database)->update());

    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = size.width, .height = size.height, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = false});
    REQUIRE(renderer.has_value());
    auto editor = devex::tools::ToolsOverlay::create(*platform, *window, *renderer, devex::tools::ToolsMode::Editor,
                                                     root / "user.dvx");
    REQUIRE(editor.has_value());
    (*editor)->setAssetDatabase(database->get());
    (*editor)->openWindow(devex::tools::EditorWindow::Profiler);
    devex::scene::Scene scene;

    const auto frame = [&] {
        devex::core::profiler::beginFrame();
        platform->pollEvents([](const devex::platform::Event&) {});
        {
            DEVEX_PROFILE_SCOPE("Editor");
            (*editor)->update(scene, std::chrono::milliseconds(16), devex::tools::PlayState::Editing);
        }
        {
            DEVEX_PROFILE_SCOPE("Render");
            devex::render::RenderWorld& world = renderer->beginFrame();
            {
                DEVEX_PROFILE_SCOPE("Editor overlays");
                (*editor)->prepareRender(scene, world, devex::tools::PlayState::Editing);
            }
            REQUIRE(renderer->endFrame());
        }
        devex::core::profiler::endFrame();
    };
    for (int index = 0; index < 20; ++index)
    {
        frame();
    }
    // The camera of the scene selected, as in the editor.
    for (devex::scene::Entity entity = scene.firstRoot(); entity.isValid(); entity = scene.nextSibling(entity))
    {
        if (scene.name(entity) == "Camera")
        {
            const std::array<devex::core::Uuid, 1> selected{scene.uuid(entity)};
            (*editor)->select(selected);
        }
    }
    setup(**editor, root, **database);
    for (int index = 0; index < 20; ++index)
    {
        frame();
    }
    EditorRun running{.editor = **editor, .renderer = *renderer, .platform = *platform, .scene = scene, .frame = frame};
    run(running);
    (*editor)->setAssetDatabase(nullptr);
}

// Prints the time of a frame of the editor and its zones, once `setup` opened what is measured.
void measureEditor(const EditorSetup& setup)
{
    runEditor(setup, [](EditorRun& running) {
        devex::core::profiler::clear();
        const auto start = std::chrono::steady_clock::now();
        constexpr int frames = 100;
        for (int index = 0; index < frames; ++index)
        {
            running.frame();
        }
        const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        std::map<std::string, double> zones;
        const auto history = devex::core::profiler::history();
        for (const auto& kept : history)
        {
            for (const devex::core::ProfileZone& zone : kept->cpu)
            {
                if (zone.thread == 0)
                {
                    zones[std::string(zone.name) + "@" + std::to_string(zone.depth)] += static_cast<double>(zone.duration()) / 1.0e6;
                }
            }
        }
        std::vector<std::pair<std::string, double>> sorted(zones.begin(), zones.end());
        std::ranges::sort(sorted, [](const auto& first, const auto& second) { return first.second > second.second; });
        std::printf("frames %zu, %.2f ms a frame, %zu entities\n", history.size(), total / frames, running.scene.entityCount());
        for (std::size_t index = 0; index < std::min<std::size_t>(sorted.size(), 40); ++index)
        {
            std::printf("%8.3f ms  %s\n", sorted[index].second / static_cast<double>(history.size()), sorted[index].first.c_str());
        }
    });
}

// Writes a picture of the editor's window, as the next frame draws it, into DEVEX_PICTURES.
void picture(EditorRun& running, const std::string& name)
{
    const char* const folder = std::getenv("DEVEX_PICTURES");
    const std::filesystem::path file = (folder != nullptr ? std::filesystem::path(folder) : std::filesystem::temp_directory_path()) / (name + ".png");
    const std::uint64_t request = running.renderer.requestWindowCapture();
    for (int index = 0; index < 10; ++index)
    {
        running.frame();
        for (devex::render::CapturedImage& captured : running.renderer.takeCaptures())
        {
            if (captured.request != request)
            {
                continue;
            }
            REQUIRE_FALSE(captured.rgba.empty());
            const devex::asset::Image image{.width = captured.width, .height = captured.height, .rgba = std::move(captured.rgba)};
            REQUIRE(devex::core::writeFileAtomically(file, devex::asset::encodePng(image)).has_value());
            std::printf("%s\n", devex::core::toUtf8(file).c_str());
            return;
        }
    }
    FAIL("the window was not captured");
}

// Moves the pointer of the tools to a point of the window and lets a few frames show it there.
void pointAt(EditorRun& running, devex::math::Vec2 point, int frames = 12)
{
    auto& input = const_cast<devex::platform::Input&>(running.platform.toolsInput());
    for (int index = 0; index < frames; ++index)
    {
        running.platform.pollEvents([](const devex::platform::Event&) {});
        input.moveMouse(point, {});
        running.frame();
    }
}

} // namespace

TEST_CASE("Editor frame benchmark", "[.][benchmark]")
{
    measureEditor([](devex::tools::ToolsOverlay&, const std::filesystem::path&, const devex::asset::AssetDatabase&) {});
}

// The Script screen on a file of five thousand lines: what it costs does not grow with the file.
TEST_CASE("Script screen benchmark", "[.][benchmark]")
{
    measureEditor([](devex::tools::ToolsOverlay& editor, const std::filesystem::path& root, const devex::asset::AssetDatabase&) {
        std::string code = "using Devex;\n\npublic class Big : Component\n{\n";
        for (int line = 0; line < 5000; ++line)
        {
            code += std::format("    public float Value{} = {}.0f; // A field of the class, number {}.\n", line, line, line);
        }
        code += "}\n";
        const std::filesystem::path file = root / "code" / "Big.cs";
        REQUIRE(devex::core::writeTextFile(file, code).has_value());
        editor.openTextFile(file);
    });
}

// The Animator panel on the controller of the robot, its graph and its parameters.
TEST_CASE("Animator panel benchmark", "[.][benchmark]")
{
    measureEditor([](devex::tools::ToolsOverlay& editor, const std::filesystem::path&, const devex::asset::AssetDatabase& database) {
        const std::optional<devex::asset::AssetId> controller = database.findByPath("res://assets/animators/robot.dvxanimator");
        REQUIRE(controller.has_value());
        editor.selectAsset(*controller);
        editor.openWindow(devex::tools::EditorWindow::Animator);
    });
}

// The editor at the size of a laptop screen: its camera in the inspector, a scrollbar and a button
// under the pointer, and an open menu.
TEST_CASE("Editor pictures", "[.][pictures]")
{
    runEditor([](devex::tools::ToolsOverlay&, const std::filesystem::path&, const devex::asset::AssetDatabase&) {},
              [](EditorRun& running) {
                  // The player in the inspector: its transform, its physics and its code.
                  for (devex::scene::Entity entity = running.scene.firstRoot(); entity.isValid(); entity = running.scene.nextSibling(entity))
                  {
                      if (running.scene.name(entity) == "Player")
                      {
                          const std::array<devex::core::Uuid, 1> selected{running.scene.uuid(entity)};
                          running.editor.select(selected);
                      }
                  }
                  pointAt(running, {800.0f, 400.0f});
                  picture(running, "editor");
                  pointAt(running, {296.0f, 130.0f});
                  picture(running, "editor-scrollbar");
                  pointAt(running, {975.0f, 660.0f});
                  picture(running, "editor-button");
                  running.editor.openMenu(devex::tools::EditorMenu::Scene);
                  pointAt(running, {80.0f, 60.0f});
                  picture(running, "editor-menu");
                  running.editor.closeMenu();
              },
              {1600, 900});
}
