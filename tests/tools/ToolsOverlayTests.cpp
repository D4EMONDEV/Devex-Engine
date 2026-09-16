#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Components.hpp>
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
