#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

namespace
{
struct Sequence
{
    std::vector<devex::render::CapturedImage> images;
    std::vector<devex::render::RendererStats> stats;
    std::vector<std::vector<std::uint32_t>> picks;
    std::vector<std::string> errors;
};

Sequence drawLevels(bool automatic, bool skinned)
{
    using namespace devex;
    Sequence result;
    const auto sink = core::addLogSink(
        [&](const core::LogRecord& message)
        {
            if (message.level >= core::LogLevel::Error)
                result.errors.emplace_back(message.message);
        });
    {
        auto platform = platform::Platform::create();
        REQUIRE(platform);
        auto window =
            platform->createWindow({.width = 256, .height = 192, .vulkan = true, .hidden = true});
        REQUIRE(window);
        auto renderer = render::Renderer::create(
            *platform, *window, {.validation = true, .meshLodError = automatic ? 1.0f : 0.0f});
        REQUIRE(renderer);
        auto data = asset::makeUvSphere(1, 32, 16);
        const auto half = static_cast<std::uint32_t>(data.indices.size() / 6 * 3);
        data.submeshes = {{.indexCount = half},
                          {.firstIndex = half,
                           .indexCount = static_cast<std::uint32_t>(data.indices.size()) - half}};
        if (skinned)
        {
            data.inverseBind.assign(1, math::Mat4{1});
            data.skin.assign(data.vertices.size(), {.weights = {1, 0, 0, 0}});
        }
        asset::generateMeshLods(data);
        REQUIRE_FALSE(data.lods.empty());
        // Controlled errors isolate level selection from the simplifier's quality metric.
        for (auto& lod : data.lods)
            lod.error = 0.02f;
        const auto& last = data.lods.back();
        asset::MeshData low{.vertices = last.vertices,
                            .indices = last.indices,
                            .submeshes = last.submeshes,
                            .skin = last.skin,
                            .inverseBind = data.inverseBind};
        auto original = renderer->createMesh(data);
        auto reduced = renderer->createMesh(low);
        auto ground = renderer->createMesh(asset::makePlane(200));
        REQUIRE(original);
        REQUIRE(reduced);
        REQUIRE(ground);
        const auto material = renderer->createMaterial({.baseColorFactor = {0.7f, 0.15f, 0.3f, 1}});
        std::vector<std::uint64_t> requests;
        for (int frame = 0; frame < 26; ++frame)
        {
            platform->pollEvents([](const platform::Event&) {});
            auto& world = renderer->beginFrame();
            world.viewport = {256, 192};
            const bool coarse = frame < 8 || (frame >= 14 && frame < 20);
            world.camera.autoExposure = false;
            world.camera.antialiasing = render::Antialiasing::Temporal;
            world.camera.view =
                math::lookAt(math::Vec3{0, 15, coarse ? 100.0f : 40.0f}, math::Vec3{0}, {0, 1, 0});
            if (frame >= 16)
            {
                world.camera.projection = render::Projection::Orthographic;
                world.camera.orthographicSize = coarse ? 40.0f : 10.0f;
                world.camera.farPlane = 500;
            }
            world.sun = {.direction = {-1, -1, -0.2f}, .illuminance = math::Vec3{50000}};
            world.environment.intensity = 4000;
            world.meshes.push_back({.mesh = *ground,
                                    .transform = math::translate(math::Mat4{1}, {0, -12, 0}),
                                    .objectId = 1});
            const auto transform = math::rotate(math::scale(math::Mat4{1}, math::Vec3{10}),
                                                float(frame) * 0.03f, {0, 1, 0});
            if (skinned)
                world.boneMatrices.push_back(transform);
            for (std::uint32_t part = 0; part < 2; ++part)
                world.meshes.push_back({.mesh = automatic || !coarse ? *original : *reduced,
                                        .submesh = part,
                                        .material = material,
                                        .transform = transform,
                                        .boneCount = skinned ? 1u : 0u,
                                        .objectId = 42,
                                        .outlined = true});
            // The extra camera stays near while the main uses coarse geometry. Main captures
            // have an explicit reference; counters also check that different LODs cannot share a
            // pose.
            if (automatic)
            {
                auto camera = world.camera;
                camera.projection = render::Projection::Perspective;
                camera.view = math::lookAt(math::Vec3{0, 15, 40}, math::Vec3{0}, {0, 1, 0});
                world.views.push_back({.id = 7, .camera = camera, .extent = {256, 192}});
            }
            if (frame == 5 || frame == 18)
                world.pick = render::PickRequest{
                    .x = 0, .y = 0, .id = std::uint64_t(frame), .width = 256, .height = 192};
            if (frame % 2 == 0)
                std::ranges::reverse(world.meshes);
            if (frame < 22)
                requests.push_back(renderer->requestCapture(256, 192));
            REQUIRE(renderer->endFrame());
            result.stats.push_back(renderer->stats());
            std::ranges::move(renderer->takeCaptures(), std::back_inserter(result.images));
            for (auto& pick : renderer->takePickResults())
            {
                std::ranges::sort(pick.objectIds);
                result.picks.push_back(std::move(pick.objectIds));
            }
        }
        REQUIRE(result.images.size() == requests.size());
        std::ranges::sort(result.images, {}, &render::CapturedImage::request);
        if (automatic)
        {
            CHECK(result.stats[5].meshLodInstances == 2);
            CHECK(result.stats[10].meshLodInstances == 0);
            if (skinned)
            {
                CHECK(result.stats[5].gpuSkinnedVertices ==
                      data.vertices.size() + low.vertices.size());
                CHECK(result.stats[10].gpuSkinningReusedPoses == 1);
            }
        }
        renderer->destroyMesh(*original);
        CHECK(renderer->stats().meshCount == 2); // All owned levels retire with their original.
    }
    core::removeLogSink(sink);
    return result;
}
} // namespace

TEST_CASE("Mesh LODs match explicit geometry through motion shadows picking and camera changes",
          "[render][gpu][lod]")
{
    const bool skinned = GENERATE(false, true);
    const auto reference = drawLevels(false, skinned);
    const auto result = drawLevels(true, skinned);
    for (const auto& error : result.errors)
        UNSCOPED_INFO(error);
    CHECK(reference.errors.empty());
    CHECK(result.errors.empty());
    CHECK(result.picks == reference.picks);
    REQUIRE(result.images.size() == reference.images.size());
    for (std::size_t frame = 0; frame < result.images.size(); ++frame)
    {
        CAPTURE(frame, skinned);
        const auto& a = result.images[frame].rgba;
        const auto& b = reference.images[frame].rgba;
        REQUIRE(a.size() == b.size());
        std::size_t different = 0;
        for (std::size_t channel = 0; channel < a.size(); ++channel)
            different += std::abs(int(a[channel]) - int(b[channel])) > 2 ? 1u : 0u;
        CHECK(different * 1000 < a.size());
    }
}

TEST_CASE("Mesh LOD hysteresis avoids toggling at the pixel threshold", "[render][gpu][lod]")
{
    using namespace devex;
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window =
        platform->createWindow({.width = 128, .height = 96, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    auto data = asset::makeUvSphere();
    asset::generateMeshLods(data);
    REQUIRE_FALSE(data.lods.empty());
    data.lods.resize(1);
    data.lods.front().error = 0.01f;
    auto mesh = renderer->createMesh(data);
    REQUIRE(mesh);
    const std::array errors{1.3f, 0.95f, 0.8f, 0.95f, 1.2f, 0.95f, 0.8f};
    const std::array expected{0u, 0u, 1u, 1u, 0u, 0u, 1u};
    for (std::size_t frame = 0; frame < errors.size(); ++frame)
    {
        platform->pollEvents([](const platform::Event&) {});
        auto& world = renderer->beginFrame();
        world.camera.projection = render::Projection::Orthographic;
        world.camera.orthographicSize = 0.01f * 96.0f / (2 * errors[frame]);
        world.camera.view = math::translate(math::Mat4{1}, {0, 0, frame % 2 == 0 ? -5.0f : -7.0f});
        world.meshes.push_back(
            {.mesh = *mesh,
             .transform =
                 math::rotate(math::Mat4{1}, frame == 3 ? math::radians(45.0f) : 0.0f, {0, 1, 0}),
             .objectId = 9});
        REQUIRE(renderer->endFrame());
        CAPTURE(frame);
        CHECK(renderer->stats().meshLodInstances == expected[frame]);
    }
    auto& world = renderer->beginFrame();
    world.camera.projection = render::Projection::Orthographic;
    world.camera.orthographicSize = 10;
    world.camera.view = math::translate(math::Mat4{1}, {0, 0, -5});
    world.meshes.push_back({.mesh = *mesh, .objectId = 9, .lodBias = 0});
    REQUIRE(renderer->endFrame());
    CHECK(renderer->stats().meshLodInstances == 0);
}
