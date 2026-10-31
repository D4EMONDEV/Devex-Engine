#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace
{

struct SkinSequence
{
    std::vector<std::vector<std::uint8_t>> pictures;
    std::vector<std::vector<std::uint32_t>> picks;
    std::vector<devex::render::RendererStats> stats;
    std::vector<std::string> errors;
};

// Two submeshes share a pose. A few vertices have no weights and follow the instance, while the
// others blend two independently moving bones, with mirrored and nonuniform transforms.
SkinSequence skinSequence(bool gpuCulling, bool gpuSkinning, bool temporal)
{
    using namespace devex;
    SkinSequence sequence;
    const auto sink = core::addLogSink(
        [&](const core::LogRecord& message)
        {
            if (message.level >= core::LogLevel::Error)
                sequence.errors.emplace_back(message.message);
        });
    {
        auto platform = platform::Platform::create();
        REQUIRE(platform);
        auto window =
            platform->createWindow({.width = 321, .height = 239, .vulkan = true, .hidden = true});
        REQUIRE(window);
        auto renderer = render::Renderer::create(
            *platform, *window,
            {.validation = true, .gpuCulling = gpuCulling, .gpuSkinning = gpuSkinning});
        REQUIRE(renderer);
        auto data = asset::makeCube();
        const auto vertexCount = data.vertices.size();
        data.submeshes = {{.firstIndex = 0, .indexCount = 18},
                          {.firstIndex = 18, .indexCount = 18}};
        data.inverseBind.assign(2, math::Mat4{1.0f});
        for (std::size_t index = 0; index < vertexCount; ++index)
        {
            const float second = data.vertices[index].position.y > 0 ? 0.75f : 0.25f;
            data.skin.push_back({.joints = {0, 1, 0, 0},
                                 .weights = index == 0 || index + 1 == vertexCount
                                                ? math::Vec4{0.0f}
                                                : math::Vec4{1.0f - second, second, 0.0f, 0.0f}});
        }
        // The reference's old CPU culling must keep even the deliberately displaced poses. The
        // new path instead measures the actual deformed positions and can reject the hidden ones.
        data.bounds = {.min = math::Vec3{-100.0f}, .max = math::Vec3{100.0f}};
        const auto mesh = renderer->createMesh(data);
        const auto cube = renderer->createMesh(asset::makeCube());
        const auto ground = renderer->createMesh(asset::makePlane(40.0f));
        REQUIRE(mesh);
        REQUIRE(cube);
        REQUIRE(ground);
        const auto body = renderer->createMaterial({.baseColorFactor = {0.8f, 0.15f, 0.05f, 1.0f}});
        const auto mask = renderer->createMaterial({.baseColorFactor = {0.2f, 0.8f, 0.3f, 0.9f},
                                                    .alphaMode = asset::AlphaMode::Mask,
                                                    .doubleSided = true});
        const auto glass = renderer->createMaterial(
            {.baseColorFactor = {0.2f, 0.5f, 0.9f, 0.4f}, .alphaMode = asset::AlphaMode::Blend});
        const auto wall = renderer->createMaterial({.baseColorFactor = {0.5f, 0.5f, 0.6f, 1.0f}});
        std::vector<render::CapturedImage> pictures;
        std::vector<std::uint64_t> requests;
        for (int frame = 0; frame < 28; ++frame)
        {
            platform->pollEvents([](const platform::Event&) {});
            auto& world = renderer->beginFrame();
            world.viewport = frame < 20 ? math::Extent2D{321, 239} : math::Extent2D{319, 241};
            const math::Vec3 eye = frame < 14 ? math::Vec3{0, 4, 12} : math::Vec3{9, 3, 4};
            world.camera.view = math::lookAt(eye, math::Vec3{0, 1, -5}, math::Vec3{0, 1, 0});
            world.camera.autoExposure = false;
            world.camera.antialiasing =
                temporal ? render::Antialiasing::Temporal : render::Antialiasing::None;
            world.sun = {.direction = {-0.5f, -1.0f, -0.3f}, .illuminance = math::Vec3{50000.0f}};
            world.environment.intensity = 4000.0f;
            world.lights.push_back({.position = {0, 8, 2},
                                    .intensity = math::Vec3{3000.0f},
                                    .range = 30,
                                    .castShadows = true});
            world.meshes.push_back({.mesh = *ground, .material = wall, .objectId = 1});
            const float wallX = frame >= 6 && frame < 10 ? 100.0f : 0.0f;
            world.meshes.push_back(
                {.mesh = *cube,
                 .material = wall,
                 .transform =
                     math::scale(math::translate(math::Mat4{1}, {wallX, 2, 0}), {14, 6, 0.5f}),
                 .objectId = 2});
            world.boneMatrices.resize(48);
            for (std::uint32_t index = 0; index < 24; ++index)
            {
                math::Vec3 position{(float(index % 6) - 2.5f) * 1.6f, 1.0f,
                                    -3.0f - float(index / 6) * 2.0f};
                if (index == 0 && frame >= 10)
                    position.z = 2.0f; // A previously occluded pose comes in front of the wall.
                if (index == 23)
                    position.z = 50.0f; // Precise GPU bounds reject this pose behind the camera.
                const auto firstBone = (frame < 16 ? index : 23u - index) * 2;
                const float angle = std::sin(float(frame) * 0.2f + float(index)) * 0.7f;
                auto transform = math::translate(math::Mat4{1}, position);
                transform = math::scale(transform, {index % 3 == 0 ? -0.8f : 0.8f, 1.3f, 0.6f});
                world.boneMatrices[firstBone] = math::rotate(transform, angle, {0, 0, 1});
                world.boneMatrices[firstBone + 1] =
                    math::rotate(math::translate(transform, {0.2f, 0.4f, 0}), -angle, {0, 1, 0});
                for (std::uint32_t part = 0; part < 2; ++part)
                {
                    // Reusing a picking ID does not allow two different fallback transforms to
                    // share their unweighted vertices. All other submeshes share one pose.
                    const auto partTransform = index == 11 && part == 1
                                                   ? math::translate(transform, {0.8f, 0, 0})
                                                   : transform;
                    world.meshes.push_back({.mesh = *mesh,
                                            .submesh = part,
                                            .material = part == 0        ? body
                                                        : index % 7 == 0 ? glass
                                                                         : mask,
                                            .transform = partTransform,
                                            .firstBone = firstBone,
                                            .boneCount = 2,
                                            .objectId = 10 + index,
                                            .outlined = index == 0});
                }
            }
            if (frame >= 18)
                std::ranges::reverse(world.meshes);
            if (frame == 3 || frame == 12)
                world.pick = render::PickRequest{
                    .x = 0, .y = 0, .id = std::uint64_t(frame + 1), .width = 321, .height = 239};
            if (frame >= 22)
            {
                render::RenderView view;
                view.id = 7;
                view.extent = {177, 131};
                view.camera = world.camera;
                world.views.push_back(view);
            }
            if (frame < 24)
                requests.push_back(
                    renderer->requestCapture(world.viewport.width, world.viewport.height));
            REQUIRE(renderer->endFrame());
            sequence.stats.push_back(renderer->stats());
            std::ranges::move(renderer->takeCaptures(), std::back_inserter(pictures));
            for (auto& result : renderer->takePickResults())
            {
                std::ranges::sort(result.objectIds);
                sequence.picks.push_back(std::move(result.objectIds));
            }
        }
        REQUIRE(pictures.size() == requests.size());
        for (const auto request : requests)
        {
            const auto found =
                std::ranges::find(pictures, request, &render::CapturedImage::request);
            REQUIRE(found != pictures.end());
            sequence.pictures.push_back(std::move(found->rgba));
        }
        REQUIRE(sequence.picks.size() == 2);
        CHECK(sequence.stats[5].gpuSkinnedPoses == (gpuCulling && gpuSkinning ? 25u : 0u));
        CHECK(sequence.stats[5].gpuSkinnedVertices ==
              (gpuCulling && gpuSkinning ? 25 * vertexCount : 0));
    }
    core::removeLogSink(sink);
    return sequence;
}

} // namespace

TEST_CASE("Compute skinning preserves animated images shadows picking and motion",
          "[render][gpu][skinning]")
{
    const bool temporal = GENERATE(false, true);
    const auto reference = skinSequence(false, false, temporal);
    for (bool compute : {false, true})
    {
        CAPTURE(temporal, compute);
        const auto result = skinSequence(true, compute, temporal);
        for (const auto& error : reference.errors)
            UNSCOPED_INFO(error);
        for (const auto& error : result.errors)
            UNSCOPED_INFO(error);
        CHECK(reference.errors.empty());
        CHECK(result.errors.empty());
        CHECK(result.picks == reference.picks);
        REQUIRE(result.pictures.size() == reference.pictures.size());
        for (std::size_t frame = 0; frame < result.pictures.size(); ++frame)
        {
            INFO("captured frame " << frame);
            REQUIRE(result.pictures[frame].size() == reference.pictures[frame].size());
            std::size_t different = 0;
            for (std::size_t channel = 0; channel < result.pictures[frame].size(); ++channel)
                different += std::abs(int(result.pictures[frame][channel]) -
                                      int(reference.pictures[frame][channel])) > 2
                                 ? 1u
                                 : 0u;
            CHECK(different * 1000 < result.pictures[frame].size());
        }
        if (compute)
        {
            CHECK(result.stats[5].gpuFrustumCulled >= 2);
            CHECK(result.stats[5].gpuOccludedInstances > 8);
            CHECK(result.stats[5].drawCalls < reference.stats[5].drawCalls);
        }
    }
}
