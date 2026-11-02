#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{

std::vector<devex::render::CapturedImage> drawLines(bool grid, float width)
{
    using namespace devex;
    std::vector<std::string> errors;
    const auto sink = core::addLogSink(
        [&](const core::LogRecord& record)
        {
            if (record.level >= core::LogLevel::Error)
                errors.emplace_back(record.message);
        });
    std::vector<render::CapturedImage> images;
    {
        auto platform = platform::Platform::create();
        REQUIRE(platform);
        auto window =
            platform->createWindow({.width = 321, .height = 239, .vulkan = true, .hidden = true});
        REQUIRE(window);
        auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer);
        const auto cube = renderer->createMesh(asset::makeCube());
        REQUIRE(cube);
        const auto black = renderer->createMaterial({.baseColorFactor = {0.0f, 0.0f, 0.0f, 1.0f}});
        for (int frame = 0; frame < 36; ++frame)
        {
            platform->pollEvents([](const auto&) {});
            auto& world = renderer->beginFrame();
            world.camera.autoExposure = false;
            world.camera.bloom = 0.0f;
            world.camera.antialiasing =
                grid ? render::Antialiasing::Temporal : render::Antialiasing::None;
            world.environment.color = math::Vec3{0.0f};
            world.environment.intensity = 0.0f;
            world.sun.illuminance = math::Vec3{0.0f};
            world.lineWidth = width;
            if (grid)
            {
                // A low, immobile camera, where coplanar depth differences from TAA jitter are
                // larger than a fixed relative bias. The black floor isolates the tool's pixels.
                world.camera.view =
                    math::lookAt(math::Vec3{0.3f, 0.2f, 4.0f}, math::Vec3{0.0f, 0.0f, -70.0f},
                                 math::Vec3{0.0f, 1.0f, 0.0f});
                world.meshes.push_back({.mesh = *cube,
                                        .material = black,
                                        .transform = math::scale(
                                            math::translate(math::Mat4{1.0f}, {0.0f, -0.5f, 0.0f}),
                                            {1000.0f, 1.0f, 1000.0f})});
                if (platform::environmentVariable("DEVEX_GRID_DEPTH") != "0")
                    world.sceneLineDepthPlane = {0.0f, 1.0f, 0.0f, 0.0f};
                for (int i = -20; i <= 20; ++i)
                {
                    const float x = static_cast<float>(i);
                    world.sceneLines.push_back({{x, 0.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 1.0f}});
                    world.sceneLines.push_back({{x, 0.0f, -160.0f}, {1.0f, 0.0f, 0.0f, 1.0f}});
                }
                for (int i = 1; i <= 80; ++i)
                {
                    const float z = -static_cast<float>(i) * 2.0f;
                    world.sceneLines.push_back({{-20.0f, 0.0f, z}, {1.0f, 0.0f, 0.0f, 1.0f}});
                    world.sceneLines.push_back({{20.0f, 0.0f, z}, {1.0f, 0.0f, 0.0f, 1.0f}});
                }
            }
            else
            {
                world.camera.projection = render::Projection::Orthographic;
                world.camera.orthographicSize = 5.0f;
                world.camera.view = math::Mat4{1.0f};
                world.meshes.push_back(
                    {.mesh = *cube,
                     .material = black,
                     .transform = math::translate(math::Mat4{1.0f}, {0.0f, 0.0f, -1.0f})});
                world.sceneLines = {{{0.0f, -3.0f, -2.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
                                    {{0.0f, 3.0f, -2.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}};
                world.overlayLines = {{{2.0f, -3.0f, -2.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                                      {{2.0f, 3.0f, -2.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}};
            }
            if (frame >= 16 && frame < 32)
                (void)renderer->requestWindowCapture();
            REQUIRE(renderer->endFrame());
            auto captured = renderer->takeCaptures();
            std::ranges::move(captured, std::back_inserter(images));
        }
    }
    core::removeLogSink(sink);
    for (const auto& error : errors)
        UNSCOPED_INFO(error);
    CHECK(errors.empty());
    if (images.empty() || images.front().rgba.empty())
        SKIP("This display does not support window readback");
    REQUIRE(images.size() == 16);
    std::ranges::sort(images, {}, &render::CapturedImage::request);
    return images;
}

bool redAt(const devex::render::CapturedImage& image, std::size_t pixel)
{
    return image.rgba[pixel * 4] > 128 && image.rgba[pixel * 4 + 1] < 32;
}

} // namespace

TEST_CASE("The coplanar editor grid stays still through temporal jitter", "[render][gpu][overlay]")
{
    const auto images = drawLines(true, 1.5f);
    const auto& first = images.front();
    const std::size_t pixels = first.rgba.size() / 4;
    std::size_t visible = 0;
    for (std::size_t i = 0; i < pixels; ++i)
        visible += redAt(first, i) ? 1u : 0u;
    REQUIRE(visible > 100);
    for (const auto& image : images)
    {
        REQUIRE(image.rgba.size() == first.rgba.size());
        std::size_t changed = 0;
        for (std::size_t i = 0; i < pixels; ++i)
            changed += redAt(image, i) != redAt(first, i) ? 1u : 0u;
        INFO("grid pixels: " << visible << ", changing: " << changed);
        CHECK(changed == 0);
    }
}

TEST_CASE("Tool lines have a configurable pixel width and preserve occlusion",
          "[render][gpu][overlay]")
{
    const auto thin = drawLines(false, 1.0f);
    const auto thick = drawLines(false, 5.0f);
    const auto redCount = [](const auto& image)
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < image.rgba.size() / 4; ++i)
            count += redAt(image, i) ? 1u : 0u;
        return count;
    };
    REQUIRE(redCount(thin.front()) > 50);
    CHECK(redCount(thick.front()) > redCount(thin.front()) * 2);
    const auto& image = thick.front();
    const auto middle =
        (static_cast<std::size_t>(image.height / 2) * image.width + image.width / 2) * 4;
    CHECK(image.rgba[middle + 2] < 32); // The cube hides the middle of the blue scene line.
    const auto above =
        (static_cast<std::size_t>(image.height / 4) * image.width + image.width / 2) * 4;
    CHECK(image.rgba[above + 2] > 128); // Its exposed part is still drawn.
}
