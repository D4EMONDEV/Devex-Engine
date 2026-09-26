#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using devex::core::LogLevel;
using devex::core::LogRecord;

namespace {

// Collects error messages, including those reported by the Vulkan validation layer.
class ErrorCapture
{
public:
    ErrorCapture()
        : m_sink(devex::core::addLogSink([this](const LogRecord& record) {
            if (record.level >= LogLevel::Error)
            {
                m_errors.emplace_back(record.message);
            }
        }))
    {
    }

    ~ErrorCapture()
    {
        devex::core::removeLogSink(m_sink);
    }

    ErrorCapture(const ErrorCapture&) = delete;
    ErrorCapture& operator=(const ErrorCapture&) = delete;

    [[nodiscard]] const std::vector<std::string>& errors() const noexcept
    {
        return m_errors;
    }

private:
    std::vector<std::string> m_errors;
    devex::core::LogSinkId m_sink;
};

} // namespace

TEST_CASE("The renderer presents frames without validation errors", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());

        auto window = platform->createWindow({
            .title = "Devex render tests",
            .width = 320,
            .height = 240,
            .vulkan = true,
            .hidden = true,
        });
        REQUIRE(window.has_value());

        auto renderer = devex::render::Renderer::create(*platform, *window, {
            .applicationName = "Devex render tests",
            .validation = true,
        });
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        for (int frame = 0; frame < 5; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.environment.color = {0.1f * static_cast<float>(frame), 0.2f, 0.3f};

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
        }

        CHECK_FALSE(renderer->gpu().name.empty());
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Meshes and textures reach the GPU within the upload budget of each frame", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        // A budget of one byte: each frame copies its first waiting resource only.
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .uploadBytesPerFrame = 1});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        devex::asset::TextureData image{.format = devex::asset::TextureFormat::Rgba8Srgb};
        image.mips.push_back({.width = 4, .height = 4, .bytes = std::vector<std::byte>(4 * 4 * 4, std::byte{200})});
        auto cube = renderer->createMesh(devex::asset::makeCube());
        auto texture = renderer->createTexture(image);
        auto dropped = renderer->createMesh(devex::asset::makeUvSphere());
        REQUIRE(cube.has_value());
        REQUIRE(texture.has_value());
        REQUIRE(dropped.has_value());
        // Created at once, copied later.
        CHECK_FALSE(renderer->isReady(*cube));
        CHECK_FALSE(renderer->isReady(*texture));
        CHECK(renderer->stats().pendingUploads == 3);
        CHECK(renderer->stats().pendingUploadBytes > 0);
        // Destroyed before any frame copied it: nothing is copied.
        renderer->destroyMesh(*dropped);

        const devex::render::MaterialHandle material = renderer->createMaterial({.baseColorTexture = *texture});
        std::vector<bool> cubeReady;
        std::vector<bool> textureReady;
        for (int frame = 0; frame < 4; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.0f, 3.0f}));
            world.meshes.push_back({.mesh = *cube, .material = material});
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            cubeReady.push_back(renderer->isReady(*cube));
            textureReady.push_back(renderer->isReady(*texture));
        }
        CHECK(cubeReady == std::vector<bool>{true, true, true, true});
        CHECK(textureReady == std::vector<bool>{false, true, true, true});
        CHECK(renderer->stats().pendingUploads == 0);
        CHECK(renderer->stats().pendingUploadBytes == 0);
        CHECK_FALSE(renderer->isReady(*dropped));
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Meshes are drawn and destroyed without validation errors", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        auto cube = renderer->createMesh(devex::asset::makeCube());
        auto sphere = renderer->createMesh(devex::asset::makeUvSphere());
        REQUIRE(cube.has_value());
        REQUIRE(sphere.has_value());
        CHECK_FALSE(renderer->createMesh(devex::asset::MeshData{}).has_value());

        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.0f, 3.0f});
        for (int frame = 0; frame < 6; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.meshes.push_back({.mesh = *cube});
            if (frame < 3)
            {
                world.meshes.push_back({
                    .mesh = *sphere,
                    .transform = devex::math::translate(devex::math::Mat4{1.0f}, {1.5f, 0.0f, 0.0f}),
                });
            }

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            // The sphere is released while frames that draw it may still be in flight.
            if (frame == 2)
            {
                renderer->destroyMesh(*sphere);
            }
        }
        // Destroying a stale handle is ignored.
        renderer->destroyMesh(*sphere);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Textured materials draw submeshes and survive texture removal", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        // Two submeshes: the first two faces of the cube, then the rest.
        devex::asset::MeshData cubeData = devex::asset::makeCube();
        cubeData.submeshes = {{.firstIndex = 0, .indexCount = 12},
                              {.firstIndex = 12, .indexCount = 24}};
        auto cube = renderer->createMesh(cubeData);
        REQUIRE(cube.has_value());
        CHECK(renderer->submeshCount(*cube) == 2);

        // A 2x2 texture with its mip, and a block-compressed one with a partial block.
        devex::asset::TextureData checker{.format = devex::asset::TextureFormat::Rgba8Srgb};
        checker.mips.push_back({.width = 2, .height = 2, .bytes = std::vector<std::byte>(16, std::byte{200})});
        checker.mips.push_back({.width = 1, .height = 1, .bytes = std::vector<std::byte>(4, std::byte{100})});
        devex::asset::TextureData compressed{.format = devex::asset::TextureFormat::Bc7Srgb};
        compressed.mips.push_back({.width = 5, .height = 3, .bytes = std::vector<std::byte>(2 * 16)});
        auto checkerTexture = renderer->createTexture(checker);
        auto compressedTexture = renderer->createTexture(compressed);
        REQUIRE(checkerTexture.has_value());
        REQUIRE(compressedTexture.has_value());
        compressed.mips.front().bytes.pop_back();
        CHECK_FALSE(renderer->createTexture(compressed).has_value());

        const devex::render::MaterialHandle opaque = renderer->createMaterial({
            .baseColorTexture = *checkerTexture,
            .emissiveTexture = *compressedTexture,
        });
        const devex::render::MaterialHandle cutout = renderer->createMaterial({
            .baseColorFactor = {1.0f, 0.5f, 0.2f, 0.4f},
            .alphaMode = devex::asset::AlphaMode::Mask,
            .doubleSided = true,
        });
        CHECK(renderer->stats().textureCount == 2);
        CHECK(renderer->stats().materialCount == 2);

        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.0f, 3.0f});
        for (int frame = 0; frame < 8; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.meshes.push_back({.mesh = *cube, .submesh = 0, .material = opaque});
            world.meshes.push_back({.mesh = *cube, .submesh = 1, .material = cutout});
            // Out of range submeshes and unknown materials are tolerated.
            world.meshes.push_back({.mesh = *cube, .submesh = 7});

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            if (frame == 2)
            {
                // The material falls back to the white texture while frames still use the old one.
                renderer->destroyTexture(*checkerTexture);
                renderer->updateMaterial(cutout, {.baseColorFactor = {0.0f, 1.0f, 0.0f, 1.0f}});
            }
            if (frame == 5)
            {
                renderer->destroyMaterial(opaque);
            }
        }
        // Two instances, each drawn twice: once by the prepass and once by the shading.
        CHECK(renderer->stats().drawCalls == 4);
        CHECK(renderer->stats().textureCount == 1);
        CHECK(renderer->stats().materialCount == 1);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Lit frames with shadows, local lights and a sky texture render without validation errors",
          "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        auto ground = renderer->createMesh(devex::asset::makePlane(20.0f));
        auto sphere = renderer->createMesh(devex::asset::makeUvSphere());
        REQUIRE(ground.has_value());
        REQUIRE(sphere.has_value());

        // A half-float equirectangular sky of value 1, with its mip chain.
        devex::asset::TextureData sky{.format = devex::asset::TextureFormat::Rgba16Float};
        for (std::uint32_t width = 8, height = 4; width > 0; width /= 2, height = std::max(height / 2, 1u))
        {
            devex::asset::TextureMip& mip = sky.mips.emplace_back();
            mip.width = width;
            mip.height = height;
            for (std::uint32_t texel = 0; texel < width * height * 4; ++texel)
            {
                mip.bytes.push_back(std::byte{0x00});
                mip.bytes.push_back(std::byte{0x3C});
            }
        }
        auto skyTexture = renderer->createTexture(sky);
        REQUIRE(skyTexture.has_value());

        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 2.0f, 6.0f});
        float firstEv100 = 0.0f;
        for (int frame = 0; frame < 12; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.camera.autoExposure = frame >= 4;
            world.camera.adaptationSpeed = 100.0f;
            world.sun = {.direction = {-0.3f, -1.0f, -0.2f}, .illuminance = devex::math::Vec3{50000.0f}};
            world.lights.push_back({.position = {1.0f, 1.0f, 0.0f}, .intensity = devex::math::Vec3{200.0f}, .range = 5.0f});
            world.lights.push_back({
                .type = devex::render::LightType::Spot,
                .position = {0.0f, 3.0f, 0.0f},
                .direction = {0.0f, -1.0f, 0.0f},
                .intensity = devex::math::Vec3{500.0f},
                .range = 8.0f,
                .innerAngle = 0.3f,
                .outerAngle = 0.5f,
            });
            // The sky texture is replaced by the uniform sky halfway, then destroyed.
            world.environment = {.sky = frame < 6 ? *skyTexture : devex::render::TextureHandle{},
                                 .intensity = 5000.0f};
            world.meshes.push_back({.mesh = *ground});
            world.meshes.push_back({
                .mesh = *sphere,
                .transform = devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.5f, 0.0f}),
            });

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            if (frame == 3)
            {
                firstEv100 = renderer->stats().ev100;
            }
            if (frame == 7)
            {
                renderer->destroyTexture(*skyTexture);
            }
        }
        CHECK(renderer->stats().lightCount == 2);
        // Manual exposure keeps the default EV100; automatic exposure then measures the image.
        CHECK(firstEv100 == 14.0f);
        CHECK(renderer->stats().ev100 != 14.0f);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("The scene renders into a viewport image with picking, outlines and overlay", "[render][gpu]")
{
    using devex::math::Vec3;
    using devex::math::Vec4;
    using devex::render::OverlayVertex;

    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }
        auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());

        // A cube 3 m in front of the camera covers the center of the image; the corner shows sky.
        const devex::math::Mat4 cubeTransform = devex::math::translate(devex::math::Mat4{1.0f}, Vec3{0.0f, 0.0f, -3.0f});
        // Another one at the left edge.
        const devex::math::Mat4 leftTransform = devex::math::translate(devex::math::Mat4{1.0f}, Vec3{-1.6f, 0.0f, -3.0f});
        std::vector<devex::render::PickResult> results;
        for (std::uint64_t frame = 0; frame < 12; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            // The viewport changes size once, which replaces its image.
            world.viewport = frame < 6 ? devex::math::Extent2D{200, 150} : devex::math::Extent2D{160, 120};
            world.meshes.push_back({.mesh = *cube, .transform = cubeTransform, .objectId = 42, .outlined = true});
            world.meshes.push_back({.mesh = *cube, .transform = leftTransform, .objectId = 7});
            world.sceneLines = {OverlayVertex{Vec3{-5.0f, -0.5f, -3.0f}, Vec4{1.0f}},
                                OverlayVertex{Vec3{5.0f, -0.5f, -3.0f}, Vec4{1.0f}}};
            world.overlayLines = {OverlayVertex{Vec3{0.0f}, Vec4{1.0f, 0.0f, 0.0f, 1.0f}},
                                  OverlayVertex{Vec3{0.0f, 1.0f, -3.0f}, Vec4{1.0f, 0.0f, 0.0f, 1.0f}}};
            world.overlayTriangles = {OverlayVertex{Vec3{0.0f, 0.0f, -2.0f}, Vec4{0.0f, 1.0f, 0.0f, 0.5f}},
                                      OverlayVertex{Vec3{0.2f, 0.0f, -2.0f}, Vec4{0.0f, 1.0f, 0.0f, 0.5f}},
                                      OverlayVertex{Vec3{0.0f, 0.2f, -2.0f}, Vec4{0.0f, 1.0f, 0.0f, 0.5f}}};
            if (frame == 1)
            {
                world.pick = devex::render::PickRequest{.x = 100, .y = 75, .id = 1};
            }
            else if (frame == 2)
            {
                world.pick = devex::render::PickRequest{.x = 2, .y = 2, .id = 2};
            }
            else if (frame == 3)
            {
                // Outside the image: answered without drawing.
                world.pick = devex::render::PickRequest{.x = 500, .y = 2, .id = 3};
            }
            else if (frame == 4)
            {
                // A rectangle over the whole image sees both cubes; one over the middle, only one.
                world.pick = devex::render::PickRequest{.x = 0, .y = 0, .id = 4, .width = 200, .height = 150};
            }
            else if (frame == 5)
            {
                world.pick = devex::render::PickRequest{.x = 90, .y = 60, .id = 5, .width = 100, .height = 30};
            }

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            std::ranges::copy(renderer->takePickResults(), std::back_inserter(results));
        }

        CHECK(renderer->stats().sceneExtent == devex::math::Extent2D{160, 120});
        std::ranges::sort(results, {}, &devex::render::PickResult::request);
        REQUIRE(results.size() == 5);
        CHECK(results[0].request == 1);
        CHECK(results[0].objectId == 42);
        CHECK(results[0].objectIds == std::vector<std::uint32_t>{42});
        CHECK(results[1].request == 2);
        CHECK(results[1].objectId == 0);
        CHECK(results[1].objectIds.empty());
        CHECK(results[2].request == 3);
        CHECK(results[2].objectId == 0);
        CHECK(results[3].objectIds == std::vector<std::uint32_t>{7, 42});
        CHECK(results[4].objectIds == std::vector<std::uint32_t>{42});
        CHECK(devex::render::Renderer::viewportTexture() != 0);
        renderer->destroyMesh(*cube);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Only one renderer can exist at a time", "[render][gpu]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.vulkan = true, .hidden = true});
    REQUIRE(window.has_value());

    auto first = devex::render::Renderer::create(*platform, *window, {.validation = false});
    REQUIRE(first.has_value());

    auto second = devex::render::Renderer::create(*platform, *window, {.validation = false});
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code == devex::core::ErrorCode::InvalidState);
}

TEST_CASE("Blended surfaces, antialiasing, occlusion and bloom draw without validation errors",
          "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        const auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());
        const devex::render::MaterialHandle opaque =
            renderer->createMaterial({.baseColorFactor = {0.8f, 0.8f, 0.8f, 1.0f}});
        const devex::render::MaterialHandle glass =
            renderer->createMaterial({.baseColorFactor = {0.4f, 0.6f, 0.9f, 0.35f},
                                      .alphaMode = devex::asset::AlphaMode::Blend});

        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{0.0f, 0.0f, 6.0f});
        for (int frame = 0; frame < 6; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            // Half of the frames run without the effects, which must be as quiet as with them.
            const bool effects = frame % 2 == 0;
            world.camera.antialiasing = effects ? devex::render::Antialiasing::Temporal
                                                : devex::render::Antialiasing::None;
            world.camera.bloom = effects ? 0.2f : 0.0f;
            world.camera.ambientOcclusion = effects ? 1.0f : 0.0f;
            world.camera.vignette = effects ? 0.3f : 0.0f;
            world.camera.grain = effects ? 0.02f : 0.0f;
            world.camera.chromaticAberration = effects ? 0.002f : 0.0f;
            world.sun.illuminance = {10000.0f, 10000.0f, 10000.0f};

            world.meshes.push_back({.mesh = *cube, .material = opaque, .objectId = 1});
            // The blended cube stands in front of the opaque one.
            world.meshes.push_back({
                .mesh = *cube,
                .material = glass,
                .transform = devex::math::translate(devex::math::Mat4(1.0f),
                                                    devex::math::Vec3{0.0f, 0.0f, 2.0f}),
                .objectId = 2,
            });

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            // The opaque cube is drawn twice, by the prepass and by the shading; the blended one
            // once, by the pass that follows them.
            CHECK(renderer->stats().drawCalls == 3);
        }
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Instances the camera cannot see are not drawn", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        const auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());
        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{0.0f, 0.0f, 6.0f});

        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::inverse(cameraTransform);
        // The sun is off, so nothing is drawn into the cascades either.
        world.sun.illuminance = {0.0f, 0.0f, 0.0f};
        world.meshes.push_back({.mesh = *cube, .objectId = 1});
        // Far behind the camera, and far to the side.
        world.meshes.push_back({
            .mesh = *cube,
            .transform = devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{0.0f, 0.0f, 60.0f}),
            .objectId = 2,
        });
        world.meshes.push_back({
            .mesh = *cube,
            .transform = devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{200.0f, 0.0f, 0.0f}),
            .objectId = 3,
        });
        REQUIRE(renderer->endFrame().has_value());

        // Only the cube in front of the camera is drawn, by the prepass and by the shading.
        CHECK(renderer->stats().drawCalls == 2);
        // The two others are dropped by both passes.
        CHECK(renderer->stats().culledInstances == 4);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Local lights that cast shadows are drawn into the atlas", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        const auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());
        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{0.0f, 0.0f, 6.0f});

        for (int frame = 0; frame < 3; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.sun.illuminance = {0.0f, 0.0f, 0.0f};
            world.meshes.push_back({.mesh = *cube, .objectId = 1});
            // A spot takes one view of the atlas, a point light the six of its cube.
            world.lights.push_back({
                .type = devex::render::LightType::Spot,
                .position = {0.0f, 3.0f, 0.0f},
                .direction = {0.0f, -1.0f, 0.0f},
                .intensity = {50.0f, 50.0f, 50.0f},
                .range = 20.0f,
                .innerAngle = devex::math::radians(20.0f),
                .outerAngle = devex::math::radians(30.0f),
                .castShadows = true,
            });
            world.lights.push_back({
                .type = devex::render::LightType::Point,
                .position = {2.0f, 1.0f, 0.0f},
                .intensity = {20.0f, 20.0f, 20.0f},
                .range = 15.0f,
                .castShadows = true,
            });
            REQUIRE(renderer->endFrame().has_value());
            // The cube is drawn by the prepass, by the shading, by the view of the spot, and by
            // the two faces of the cube of the point light that look at it: the four others are
            // culled, which is the whole point of giving every view its own frustum.
            CHECK(renderer->stats().drawCalls == 2 + 1 + 2);
            CHECK(renderer->stats().culledInstances == 4);
        }
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("The GPU time of every pass reaches the profiler", "[render][gpu][profiler]")
{
    const ErrorCapture capture;
    devex::core::profiler::clear();
    devex::core::profiler::setEnabled(true);
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({
            .title = "Devex render tests",
            .width = 320,
            .height = 240,
            .vulkan = true,
            .hidden = true,
        });
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {
            .applicationName = "Devex render tests",
            .validation = true,
        });
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        // A frame is read back once the GPU has finished it, a few frames later.
        for (int frame = 0; frame < 6; ++frame)
        {
            devex::core::profiler::beginFrame();
            static_cast<void>(renderer->beginFrame());
            const devex::core::Result<void> presented = renderer->endFrame();
            devex::core::profiler::endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
        }
    }
    const auto frames = devex::core::profiler::history();
    devex::core::profiler::setEnabled(false);
    devex::core::profiler::clear();

    const auto measured = std::ranges::find_if(frames, [](const auto& frame) { return frame->gpuMeasured; });
    REQUIRE(measured != frames.end());
    const devex::core::ProfileFrame& frame = **measured;
    CHECK(frame.gpuDuration > 0);
    REQUIRE_FALSE(frame.gpu.empty());
    // The passes follow each other within the frame, named as the render graph names them.
    CHECK(std::ranges::any_of(frame.gpu, [](const devex::core::ProfileZone& pass) {
        return std::string_view(pass.name) == "Present";
    }));
    for (const devex::core::ProfileZone& pass : frame.gpu)
    {
        CHECK(pass.begin <= pass.end);
        CHECK(pass.end <= frame.gpuDuration);
    }
    // The CPU zones of the renderer are there as well.
    CHECK(std::ranges::any_of(frame.cpu, [](const devex::core::ProfileZone& zone) {
        return std::string_view(zone.name) == "Wait for the GPU";
    }));

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Captures picture the scene of a frame at a small size, without its interface", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());
        // A cube 3 m in front of the camera covers the middle of the picture; the sky, its corners.
        const devex::math::Mat4 cubeTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, devex::math::Vec3{0.0f, 0.0f, -3.0f});

        // Into the swapchain, then into a viewport of another shape.
        const std::uint64_t first = renderer->requestCapture(160, 160);
        std::vector<devex::render::CapturedImage> captured;
        std::uint64_t second = 0;
        for (int frame = 0; frame < 10; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.environment.color = {1.0f, 0.0f, 0.0f};
            world.meshes.push_back({.mesh = *cube, .transform = cubeTransform, .objectId = 1});
            if (frame >= 4)
            {
                world.viewport = devex::math::Extent2D{200, 100};
            }
            REQUIRE(renderer->endFrame());
            if (frame == 4)
            {
                second = renderer->requestCapture(100, 100);
            }
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }

        REQUIRE(captured.size() == 2);
        CHECK(captured[0].request == first);
        // The window is 4:3: the picture keeps its shape within 160 by 160.
        CHECK(captured[0].width == 160);
        CHECK(captured[0].height == 120);
        REQUIRE(captured[0].rgba.size() == std::size_t{4} * 160 * 120);
        // A red sky, red first, opaque.
        CHECK(captured[0].rgba[0] > 100);
        CHECK(captured[0].rgba[0] > captured[0].rgba[2]);
        CHECK(captured[0].rgba[3] == 255);
        // The whole scene is in the picture, not a corner of it: the middle shows the cube.
        const std::size_t middle = (std::size_t{60} * 160 + 80) * 4;
        CHECK((captured[0].rgba[middle] != captured[0].rgba[0] || captured[0].rgba[middle + 1] != captured[0].rgba[1] ||
               captured[0].rgba[middle + 2] != captured[0].rgba[2]));
        CHECK(captured[1].request == second);
        CHECK(captured[1].width == 100);
        CHECK(captured[1].height == 50);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}
