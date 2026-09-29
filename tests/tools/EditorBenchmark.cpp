// Where the time of an editor frame goes, on a copy of the sandbox project with its camera selected:
// the zones of the profiler, a frame's worth each. Hidden, run on demand:
//     devex_tools_tests "Editor frame benchmark"
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
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
#include <map>
#include <string>
#include <vector>

TEST_CASE("Editor frame benchmark", "[.][benchmark]")
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
    auto editor = devex::tools::ToolsOverlay::create(*platform, *window, *renderer, root / "editor.ini", devex::tools::ToolsMode::Editor,
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
