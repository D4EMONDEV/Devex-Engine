// Where the time of an editor frame goes, on a copy of the sandbox project with its camera selected:
// the zones of the profiler, a frame's worth each. Hidden, run on demand:
//     devex_tools_tests "Editor frame benchmark"
//     devex_tools_tests "Script screen benchmark"
//     devex_tools_tests "Animator panel benchmark"
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
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
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

// Runs the editor on a copy of the sandbox, lets `setup` open what is measured once the project is
// loaded and its camera selected, then prints the time of a frame and its zones.
void measureEditor(const std::function<void(devex::tools::ToolsOverlay&, const std::filesystem::path&, const devex::asset::AssetDatabase&)>& setup)
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
    auto window = platform->createWindow({.width = 2560, .height = 1369, .vulkan = true, .hidden = true});
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
    devex::core::profiler::clear();
    const auto start = std::chrono::steady_clock::now();
    constexpr int frames = 100;
    for (int index = 0; index < frames; ++index)
    {
        frame();
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
    std::printf("frames %zu, %.2f ms a frame, %zu entities\n", history.size(), total / frames, scene.entityCount());
    for (std::size_t index = 0; index < std::min<std::size_t>(sorted.size(), 40); ++index)
    {
        std::printf("%8.3f ms  %s\n", sorted[index].second / static_cast<double>(history.size()), sorted[index].first.c_str());
    }
    (*editor)->setAssetDatabase(nullptr);
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
