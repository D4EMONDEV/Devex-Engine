#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <string>
#include <vector>

using devex::core::LogLevel;
using devex::core::LogRecord;

namespace {

// What a frame of the scene below looked like: its picture, the objects seen over the whole image
// and the draws the CPU recorded.
struct Drawn
{
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint32_t> objectIds;
    std::uint32_t drawCalls = 0;
    std::vector<std::string> errors;
};

// A ground, a field of cubes and spheres of three standard materials (opaque, both sides, cut out),
// a blended sphere and objects behind the camera, lit by the sun and a spot that cast shadows.
[[nodiscard]] Drawn drawScene(bool gpuCulling)
{
    Drawn drawn;
    const devex::core::LogSinkId sink = devex::core::addLogSink([&drawn](const LogRecord& record) {
        if (record.level >= LogLevel::Error)
        {
            drawn.errors.emplace_back(record.message);
        }
    });
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .gpuCulling = gpuCulling});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        const auto ground = renderer->createMesh(devex::asset::makePlane(20.0f));
        const auto cube = renderer->createMesh(devex::asset::makeCube());
        const auto sphere = renderer->createMesh(devex::asset::makeUvSphere());
        REQUIRE(ground.has_value());
        REQUIRE(cube.has_value());
        REQUIRE(sphere.has_value());
        const std::array materials{
            renderer->createMaterial({.baseColorFactor = {0.8f, 0.3f, 0.2f, 1.0f}, .roughnessFactor = 0.4f}),
            renderer->createMaterial({.baseColorFactor = {0.2f, 0.7f, 0.3f, 1.0f}, .doubleSided = true}),
            renderer->createMaterial({.baseColorFactor = {0.3f, 0.4f, 0.9f, 0.8f}, .metallicFactor = 1.0f,
                                      .roughnessFactor = 0.3f, .alphaMode = devex::asset::AlphaMode::Mask}),
        };
        const devex::render::MaterialHandle glass = renderer->createMaterial(
            {.baseColorFactor = {0.9f, 0.9f, 1.0f, 0.4f}, .alphaMode = devex::asset::AlphaMode::Blend});

        const std::uint64_t captureRequest = renderer->requestCapture(320, 240);
        std::vector<devex::render::CapturedImage> captured;
        std::vector<devex::render::PickResult> picks;
        for (int frame = 0; frame < 6; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::lookAt(devex::math::Vec3{0.0f, 5.0f, 9.0f}, devex::math::Vec3{0.0f, 0.0f, 0.0f},
                                                    devex::math::Vec3{0.0f, 1.0f, 0.0f});
            world.camera.antialiasing = devex::render::Antialiasing::None;
            world.sun = {.direction = {-0.4f, -1.0f, -0.3f}, .illuminance = devex::math::Vec3{30000.0f}};
            world.lights.push_back({
                .type = devex::render::LightType::Spot,
                .position = {1.0f, 4.0f, 1.0f},
                .direction = {0.0f, -1.0f, 0.0f},
                .intensity = devex::math::Vec3{20000.0f},
                .range = 12.0f,
                .innerAngle = devex::math::radians(25.0f),
                .outerAngle = devex::math::radians(40.0f),
                .castShadows = true,
            });
            world.environment.intensity = 5000.0f;
            world.meshes.push_back({.mesh = *ground, .material = materials[0], .objectId = 1});
            for (std::uint32_t index = 0; index < 25; ++index)
            {
                const float x = static_cast<float>(index % 5) * 2.0f - 4.0f;
                const float z = static_cast<float>(index / 5) * 2.0f - 4.0f;
                world.meshes.push_back({.mesh = index % 2 == 0 ? *cube : *sphere,
                                        .material = materials[index % materials.size()],
                                        .transform = devex::math::translate(devex::math::Mat4{1.0f}, {x, 0.5f, z}),
                                        .objectId = 10 + index});
            }
            world.meshes.push_back({.mesh = *sphere,
                                    .material = glass,
                                    .transform = devex::math::translate(devex::math::Mat4{1.0f}, {1.0f, 1.5f, 3.0f}),
                                    .objectId = 50});
            // Behind the camera, culled by every view of it.
            for (std::uint32_t index = 0; index < 4; ++index)
            {
                world.meshes.push_back({.mesh = *cube,
                                        .material = materials[0],
                                        .transform = devex::math::translate(devex::math::Mat4{1.0f},
                                                                            {static_cast<float>(index) * 3.0f, 0.5f, 30.0f}),
                                        .objectId = 60 + index});
            }
            if (frame == 3)
            {
                world.pick = devex::render::PickRequest{.x = 0, .y = 0, .id = 1, .width = 320, .height = 240};
            }
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            if (frame == 2)
            {
                drawn.drawCalls = renderer->stats().drawCalls;
            }
            std::ranges::move(renderer->takeCaptures(), std::back_inserter(captured));
            std::ranges::move(renderer->takePickResults(), std::back_inserter(picks));
        }
        REQUIRE(captured.size() == 1);
        CHECK(captured[0].request == captureRequest);
        REQUIRE(picks.size() == 1);
        drawn.rgba = std::move(captured[0].rgba);
        drawn.objectIds = picks[0].objectIds;
        std::ranges::sort(drawn.objectIds);
    }
    devex::core::removeLogSink(sink);
    return drawn;
}

} // namespace

TEST_CASE("The GPU culls and draws the instances of standard materials as the CPU does", "[render][gpu]")
{
    const Drawn byCpu = drawScene(false);
    const Drawn byGpu = drawScene(true);
    for (const std::string& error : byCpu.errors)
    {
        UNSCOPED_INFO(error);
    }
    for (const std::string& error : byGpu.errors)
    {
        UNSCOPED_INFO(error);
    }
    CHECK(byCpu.errors.empty());
    CHECK(byGpu.errors.empty());

    // The same picture, to the rounding of a few pixels.
    REQUIRE(byCpu.rgba.size() == byGpu.rgba.size());
    REQUIRE_FALSE(byCpu.rgba.empty());
    std::size_t different = 0;
    int largest = 0;
    for (std::size_t index = 0; index < byCpu.rgba.size(); ++index)
    {
        const int difference = std::abs(static_cast<int>(byCpu.rgba[index]) - static_cast<int>(byGpu.rgba[index]));
        largest = std::max(largest, difference);
        different += difference > 2 ? 1u : 0u;
    }
    INFO("channels that differ: " << different << ", by " << largest << " at most");
    CHECK(different * 1000 < byCpu.rgba.size());

    // The same objects picked, the blended sphere among them; none of those behind the camera.
    CHECK(byGpu.objectIds == byCpu.objectIds);
    CHECK(std::ranges::find(byGpu.objectIds, 50u) != byGpu.objectIds.end());
    CHECK(std::ranges::find(byGpu.objectIds, 60u) == byGpu.objectIds.end());

    // The CPU recorded a draw per instance and pass; the GPU path a draw per batch and view.
    INFO("draw calls: " << byCpu.drawCalls << " by the CPU, " << byGpu.drawCalls << " by the GPU");
    CHECK(byGpu.drawCalls < byCpu.drawCalls);
}
