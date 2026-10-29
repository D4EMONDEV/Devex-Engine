#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/CubeMaps.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <optional>
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
        renderer->destroyMesh(*cube);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("The tools compose the scene image and interface surfaces over the window", "[render][gpu]")
{
    using devex::math::Vec2;
    using devex::math::Vec4;
    using devex::render::UiDraw;
    using devex::render::UiSource;
    using devex::render::UiVertex;

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
        // A quad of the layer of the tools, read from an image or filled with a colour.
        const auto quad = [](devex::render::RenderWorld& world, Vec2 min, Vec2 max, UiSource source, std::uint32_t surface,
                             Vec4 color) {
            const auto first = static_cast<std::uint32_t>(world.toolsVertices.size());
            world.toolsVertices.push_back(UiVertex{.position = min, .uv = {0.0f, 0.0f}, .color = color});
            world.toolsVertices.push_back(UiVertex{.position = {max.x, min.y}, .uv = {1.0f, 0.0f}, .color = color});
            world.toolsVertices.push_back(UiVertex{.position = max, .uv = {1.0f, 1.0f}, .color = color});
            world.toolsVertices.push_back(UiVertex{.position = {min.x, max.y}, .uv = {0.0f, 1.0f}, .color = color});
            const auto firstIndex = static_cast<std::uint32_t>(world.toolsIndices.size());
            for (const std::uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
            {
                world.toolsIndices.push_back(first + corner);
            }
            world.toolsDraws.push_back(UiDraw{.source = source, .surface = surface, .firstIndex = firstIndex, .indexCount = 6});
        };
        for (std::uint64_t frame = 0; frame < 6; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            // The scene image only every other frame: a quad that shows it then shows nothing.
            if (frame % 2 == 0)
            {
                world.viewport = devex::math::Extent2D{200, 150};
            }
            devex::render::UiSurface surface{.id = 7, .size = {80, 60}, .clearColor = Vec4{0.1f, 0.2f, 0.3f, 1.0f}};
            surface.vertices = {UiVertex{.position = {4.0f, 4.0f}}, UiVertex{.position = {40.0f, 4.0f}},
                                UiVertex{.position = {40.0f, 30.0f}}};
            surface.indices = {0, 1, 2};
            surface.draws.push_back(UiDraw{.indexCount = 3});
            world.uiSurfaces.push_back(std::move(surface));

            quad(world, {0.0f, 0.0f}, {320.0f, 240.0f}, UiSource::Texture, 0, Vec4{0.05f, 0.05f, 0.05f, 1.0f});
            quad(world, {10.0f, 10.0f}, {210.0f, 160.0f}, UiSource::SceneImage, 0, Vec4{1.0f});
            quad(world, {220.0f, 10.0f}, {300.0f, 70.0f}, UiSource::Surface, 7, Vec4{1.0f});
            // A surface that was not drawn this frame shows nothing.
            quad(world, {220.0f, 80.0f}, {300.0f, 140.0f}, UiSource::Surface, 9, Vec4{1.0f});
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
        }
        CHECK(renderer->stats().drawCalls > 0);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Rows of a texture are replaced while interfaces draw from it", "[render][gpu][glyphs]")
{
    using devex::math::Vec2;
    using devex::render::UiDraw;
    using devex::render::UiVertex;

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
        // A page of letters: one channel, read at its nearest pixel.
        devex::asset::TextureData image{.format = devex::asset::TextureFormat::R8Unorm, .filter = devex::asset::TextureFilter::Nearest};
        image.mips.push_back({.width = 64, .height = 64, .bytes = std::vector<std::byte>(std::size_t{64} * 64)});
        auto page = renderer->createTexture(image);
        REQUIRE(page.has_value());

        // Rows outside the texture, or of the wrong length, are refused.
        const std::vector<std::byte> rows(std::size_t{64} * 4, std::byte{255});
        CHECK_FALSE(renderer->updateTexture(*page, 62, 4, rows).has_value());
        CHECK_FALSE(renderer->updateTexture(*page, 0, 3, rows).has_value());
        CHECK_FALSE(renderer->updateTexture(devex::render::TextureHandle{}, 0, 4, rows).has_value());

        for (std::uint32_t frame = 0; frame < 6; ++frame)
        {
            // New rows every frame, while the frames before still draw from the page.
            REQUIRE(renderer->updateTexture(*page, frame * 8, 4, rows).has_value());
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.uiVertices = {UiVertex{.position = {10.0f, 10.0f}, .uv = {0.0f, 0.0f}, .color = {1.0f, 1.0f, 1.0f, 1.0f}},
                                UiVertex{.position = {74.0f, 10.0f}, .uv = {1.0f, 0.0f}, .color = {1.0f, 1.0f, 1.0f, 1.0f}},
                                UiVertex{.position = {74.0f, 74.0f}, .uv = {1.0f, 1.0f}, .color = {1.0f, 1.0f, 1.0f, 1.0f}}};
            world.uiIndices = {0, 1, 2};
            world.uiDraws.push_back(UiDraw{.kind = devex::render::UiDrawKind::Glyph, .texture = *page, .indexCount = 3});
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
        }
        CHECK(renderer->stats().pendingUploads == 0);
        renderer->destroyTexture(*page);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("A window capture pictures the whole window with the tools over it", "[render][gpu]")
{
    using devex::math::Vec4;
    using devex::render::UiDraw;
    using devex::render::UiVertex;

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
        std::uint64_t request = 0;
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 6 && captured.empty(); ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.environment.color = {0.0f, 0.0f, 0.0f};
            // A blue-green rectangle of the tools over the left half of the window.
            const Vec4 color{0.1f, 0.5f, 0.9f, 1.0f};
            world.toolsVertices = {UiVertex{.position = {0.0f, 0.0f}, .color = color}, UiVertex{.position = {160.0f, 0.0f}, .color = color},
                                   UiVertex{.position = {160.0f, 240.0f}, .color = color}, UiVertex{.position = {0.0f, 240.0f}, .color = color}};
            world.toolsIndices = {0, 1, 2, 0, 2, 3};
            world.toolsDraws.push_back(UiDraw{.indexCount = 6});
            if (frame == 1)
            {
                request = renderer->requestWindowCapture();
            }
            REQUIRE(renderer->endFrame());
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                CHECK(image.request == request);
                captured.push_back(std::move(image));
            }
        }
        // The frames in flight end with the renderer; a picture still to come is read then.
        REQUIRE(captured.size() <= 1);
        if (captured.empty() || captured.front().rgba.empty())
        {
            WARN("This display cannot be read back: the picture is empty");
        }
        else
        {
            const devex::render::CapturedImage& image = captured.front();
            CHECK(image.width == 320);
            CHECK(image.height == 240);
            REQUIRE(image.rgba.size() == std::size_t{4} * 320 * 240);
            // The tools on the left, red first; the scene, black, on the right.
            const std::size_t left = (std::size_t{120} * 320 + 40) * 4;
            CHECK(image.rgba[left] < image.rgba[left + 1]);
            CHECK(image.rgba[left + 1] < image.rgba[left + 2]);
            CHECK(image.rgba[left + 3] == 255);
            const std::size_t right = (std::size_t{120} * 320 + 280) * 4;
            CHECK(image.rgba[right + 2] < image.rgba[left + 2]);
        }
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Shadows are kept from a frame to the next until what casts them moves", "[render][gpu]")
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
        auto ground = renderer->createMesh(devex::asset::makePlane(10.0f));
        auto sphere = renderer->createMesh(devex::asset::makeUvSphere());
        REQUIRE(ground.has_value());
        REQUIRE(sphere.has_value());

        // The sun and a spot light that casts a shadow, over a ball that rises from the fifth frame.
        const auto draw = [&](devex::math::Mat4 camera, float ball) {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(camera);
            world.sun = {.direction = {-0.3f, -1.0f, -0.2f}, .illuminance = devex::math::Vec3{50000.0f}};
            world.lights.push_back({
                .type = devex::render::LightType::Spot,
                .position = {0.0f, 3.0f, 0.0f},
                .direction = {0.0f, -1.0f, 0.0f},
                .intensity = devex::math::Vec3{500.0f},
                .range = 8.0f,
                .innerAngle = 0.3f,
                .outerAngle = 0.5f,
                .castShadows = true,
            });
            world.meshes.push_back({.mesh = *ground});
            world.meshes.push_back({.mesh = *sphere, .transform = devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, ball, 0.0f})});
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("{}", presented.error()));
            }
            return renderer->stats().keptShadowPasses;
        };
        const devex::math::Mat4 camera = devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 2.0f, 6.0f});
        // Uploads first: the meshes join the frames once copied.
        for (int frame = 0; frame < 4; ++frame)
        {
            static_cast<void>(draw(camera, 0.5f));
        }
        // Nothing changed: the cascades and the atlas are both kept.
        CHECK(draw(camera, 0.5f) == 2);
        CHECK(draw(camera, 0.5f) == 2);
        // The ball moves: both are drawn again, then kept once it stops.
        CHECK(draw(camera, 0.8f) == 0);
        CHECK(draw(camera, 0.8f) == 2);
        // The camera moves: the cascades follow it, the spot light's shadow stays.
        CHECK(draw(devex::math::translate(camera, {0.5f, 0.0f, 0.0f}), 0.8f) == 1);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("An atmosphere draws a blue sky by day and a red horizon at sunset", "[render][gpu][atmosphere]")
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
        devex::render::RenderCamera camera;
        camera.autoExposure = false;
        camera.ev100 = 14.0f;
        camera.antialiasing = devex::render::Antialiasing::None;
        camera.ambientOcclusion = 0.0f;
        camera.bloom = 0.0f;
        // Radiance views looking up, and along the horizon towards the sun.
        devex::render::RenderCamera up = camera;
        up.verticalFov = devex::math::radians(90.0f);
        up.view = devex::render::cubeFaceView(devex::math::Vec3{0.0f, 2.0f, 0.0f}, 2);
        devex::render::RenderCamera east = up;
        east.view = devex::render::cubeFaceView(devex::math::Vec3{0.0f, 2.0f, 0.0f}, 0);

        // The average light of a capture in nits, and of its upper half alone.
        const auto average = [](const devex::render::CapturedImage& image, bool upperHalf) {
            devex::math::Vec3 sum{0.0f};
            std::size_t count = 0;
            for (std::uint32_t y = 0; y < (upperHalf ? image.height / 2 : image.height); ++y)
            {
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    const std::size_t pixel = (std::size_t{y} * image.width + x) * 3;
                    sum += devex::math::Vec3{image.radiance[pixel], image.radiance[pixel + 1], image.radiance[pixel + 2]};
                    ++count;
                }
            }
            return sum / static_cast<float>(std::max<std::size_t>(count, 1));
        };
        bool foggy = false;
        const auto measure = [&](devex::math::Vec3 sunDirection, const devex::render::RenderCamera& view, bool upperHalf) {
            std::uint64_t request = 0;
            std::optional<devex::math::Vec3> measured;
            for (int frame = 0; frame < 8 && !measured; ++frame)
            {
                devex::render::RenderWorld& world = renderer->beginFrame();
                world.camera = camera;
                world.sun = {.direction = sunDirection, .illuminance = devex::math::Vec3{100000.0f}};
                world.atmosphere.enabled = true;
                world.fog.enabled = foggy;
                world.views.push_back({.id = 31, .camera = view, .extent = {32, 32}, .radiance = true});
                REQUIRE(renderer->endFrame());
                if (frame == 2)
                {
                    request = renderer->requestViewCapture(31, 32, 32);
                }
                for (devex::render::CapturedImage& image : renderer->takeCaptures())
                {
                    if (image.request == request)
                    {
                        REQUIRE(image.radiance.size() == std::size_t{32} * 32 * 3);
                        measured = average(image, upperHalf);
                    }
                }
            }
            REQUIRE(measured.has_value());
            return *measured;
        };

        // Noon: the sky overhead is blue, thousands of nits under a sun of 100000 lux.
        const devex::math::Vec3 noon = measure({0.1f, -1.0f, 0.0f}, up, false);
        UNSCOPED_INFO("noon " << noon.x << " " << noon.y << " " << noon.z);
        CHECK(noon.z > noon.x * 1.5f);
        CHECK(noon.z > 500.0f);
        CHECK(noon.z < 30000.0f);
        // Sunset: towards the sun, just above the horizon, the sky is red.
        const devex::math::Vec3 sunset = measure(devex::math::normalize(devex::math::Vec3{-1.0f, -0.05f, 0.0f}), east, true);
        UNSCOPED_INFO("sunset " << sunset.x << " " << sunset.y << " " << sunset.z);
        CHECK(sunset.x > sunset.z);
        // The fog hides the horizon in a light as bright as the sky around it, lit by it and the sun.
        foggy = true;
        const devex::math::Vec3 fog = measure({0.1f, -1.0f, 0.0f}, east, true);
        UNSCOPED_INFO("fog " << fog.x << " " << fog.y << " " << fog.z);
        CHECK(fog.z > 1000.0f);
        CHECK(fog.z > fog.x);
        foggy = false;

        // The main image, as the display shows it: a blue sky.
        std::uint64_t request = 0;
        std::vector<std::uint8_t> shown;
        for (int frame = 0; frame < 8 && shown.empty(); ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera = up;
            world.sun = {.direction = {0.1f, -1.0f, 0.0f}, .illuminance = devex::math::Vec3{100000.0f}};
            world.atmosphere.enabled = true;
            REQUIRE(renderer->endFrame());
            if (frame == 2)
            {
                request = renderer->requestCapture(32, 32);
            }
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                if (image.request == request)
                {
                    shown = std::move(image.rgba);
                }
            }
        }
        REQUIRE(shown.size() >= 4);
        UNSCOPED_INFO("shown " << int(shown[0]) << " " << int(shown[1]) << " " << int(shown[2]));
        CHECK(shown[2] > shown[0]);
        CHECK(shown[2] > 60);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("The lens blurs what stands away from the focus, and what moves while the shutter is open", "[render][gpu][lens]")
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
        // A lit white cube 10 m away in front of a black sky, seen through a lens.
        const auto edges = [&](devex::render::RenderCamera camera, float speed) {
            camera.autoExposure = false;
            camera.ev100 = 13.0f;
            camera.antialiasing = devex::render::Antialiasing::None;
            camera.ambientOcclusion = 0.0f;
            camera.bloom = 0.0f;
            std::uint64_t request = 0;
            std::vector<std::uint8_t> shown;
            std::uint32_t width = 0;
            for (int frame = 0; frame < 10 && shown.empty(); ++frame)
            {
                devex::render::RenderWorld& world = renderer->beginFrame();
                world.camera = camera;
                world.sun = {.direction = {0.0f, -0.5f, -1.0f}, .illuminance = devex::math::Vec3{100000.0f}};
                world.environment.intensity = 0.0f;
                // In the middle of the image at the frame captured, its sides out of sight.
                const float x = speed * static_cast<float>(frame - 5);
                world.meshes.push_back({.mesh = *cube,
                                        .transform = devex::math::scale(devex::math::translate(devex::math::Mat4{1.0f}, {x, 0.0f, -10.0f}),
                                                                        devex::math::Vec3{3.0f}),
                                        .objectId = 1});
                REQUIRE(renderer->endFrame());
                if (frame == 5)
                {
                    request = renderer->requestCapture(320, 240);
                }
                for (devex::render::CapturedImage& image : renderer->takeCaptures())
                {
                    if (image.request == request)
                    {
                        width = image.width;
                        shown = std::move(image.rgba);
                    }
                }
            }
            REQUIRE_FALSE(shown.empty());
            // Along the middle row: the pixels between the black of the sky and the light of the cube.
            int between = 0;
            const std::size_t row = std::size_t{120} * width * 4;
            for (std::uint32_t pixel = 0; pixel < width; ++pixel)
            {
                const int value = shown[row + pixel * 4 + 1];
                between += value > 40 && value < 190 ? 1 : 0;
            }
            return between;
        };
        devex::render::RenderCamera camera;
        const int plain = edges(camera, 0.0f);
        UNSCOPED_INFO("plain " << plain);
        CHECK(plain <= 4);

        // Through a lens of 68 mm, focused on the cube, it stays sharp; focused half a meter away, it
        // blurs, as far as the largest blur.
        camera.verticalFov = devex::math::radians(20.0f);
        camera.depthOfField = 1.0f;
        camera.aperture = 1.4f;
        camera.focusDistance = 10.0f;
        const int focused = edges(camera, 0.0f);
        UNSCOPED_INFO("focused " << focused);
        CHECK(focused <= 6);
        camera.focusDistance = 0.5f;
        const int blurred = edges(camera, 0.0f);
        UNSCOPED_INFO("blurred " << blurred);
        CHECK(blurred >= 10);
        // Focused on what the middle shows: the cube again.
        camera.autoFocus = true;
        const int autoFocused = edges(camera, 0.0f);
        UNSCOPED_INFO("auto " << autoFocused);
        CHECK(autoFocused <= 6);

        // Moving fast across the image, it streaks with the shutter open, and only then.
        camera = devex::render::RenderCamera{};
        const int still = edges(camera, 1.0f);
        camera.motionBlur = 0.5f;
        const int streaked = edges(camera, 1.0f);
        UNSCOPED_INFO("still " << still << " streaked " << streaked);
        CHECK(still <= 4);
        CHECK(streaked >= still + 6);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("The grading and the volumes around the camera change the colours of the image", "[render][gpu][post]")
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
        const devex::render::MaterialHandle red = renderer->createMaterial({.baseColorFactor = {0.8f, 0.25f, 0.2f, 1.0f}});
        // The middle of a lit red cube 10 m away, through a camera and the volumes around it.
        const auto middle = [&](devex::render::RenderCamera camera, std::vector<devex::render::RenderPostVolume> volumes) {
            camera.autoExposure = false;
            camera.ev100 = 15.0f;
            camera.antialiasing = devex::render::Antialiasing::None;
            camera.ambientOcclusion = 0.0f;
            camera.bloom = 0.0f;
            std::uint64_t request = 0;
            std::vector<std::uint8_t> shown;
            std::uint32_t width = 0;
            for (int frame = 0; frame < 6 && shown.empty(); ++frame)
            {
                devex::render::RenderWorld& world = renderer->beginFrame();
                world.camera = camera;
                world.sun = {.direction = {0.0f, -0.5f, -1.0f}, .illuminance = devex::math::Vec3{100000.0f}};
                world.environment.intensity = 0.0f;
                world.postVolumes = volumes;
                world.meshes.push_back({.mesh = *cube,
                                        .material = red,
                                        .transform = devex::math::scale(devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.0f, -10.0f}),
                                                                        devex::math::Vec3{3.0f}),
                                        .objectId = 1});
                REQUIRE(renderer->endFrame());
                if (frame == 2)
                {
                    request = renderer->requestCapture(320, 240);
                }
                for (devex::render::CapturedImage& image : renderer->takeCaptures())
                {
                    if (image.request == request)
                    {
                        width = image.width;
                        shown = std::move(image.rgba);
                    }
                }
            }
            REQUIRE_FALSE(shown.empty());
            const std::size_t pixel = (std::size_t{120} * width + 160) * 4;
            return std::array<int, 3>{shown[pixel], shown[pixel + 1], shown[pixel + 2]};
        };
        const auto describe = [](std::array<int, 3> color) { return std::format("{} {} {}", color[0], color[1], color[2]); };

        const devex::render::RenderCamera camera;
        const std::array<int, 3> plain = middle(camera, {});
        UNSCOPED_INFO("plain " << describe(plain));
        CHECK(plain[0] > plain[1] + 40);

        // Without saturation, gray; through a cyan filter, without red.
        devex::render::RenderCamera grey = camera;
        grey.saturation = 0.0f;
        const std::array<int, 3> greyed = middle(grey, {});
        UNSCOPED_INFO("grey " << describe(greyed));
        CHECK(std::abs(greyed[0] - greyed[1]) <= 3);
        CHECK(std::abs(greyed[1] - greyed[2]) <= 3);
        devex::render::RenderCamera cyan = camera;
        cyan.colorFilter = {0.0f, 1.0f, 1.0f};
        const std::array<int, 3> filtered = middle(cyan, {});
        UNSCOPED_INFO("cyan " << describe(filtered));
        CHECK(filtered[0] < plain[0] / 4);

        // Warmer, more red against blue; cooler, less.
        devex::render::RenderCamera warm = camera;
        warm.temperature = 0.8f;
        const std::array<int, 3> warmed = middle(warm, {});
        devex::render::RenderCamera cool = camera;
        cool.temperature = -0.8f;
        const std::array<int, 3> cooled = middle(cool, {});
        UNSCOPED_INFO("warm " << describe(warmed) << " cool " << describe(cooled));
        CHECK(warmed[0] - warmed[2] > plain[0] - plain[2] + 5);
        CHECK(cooled[0] - cooled[2] < plain[0] - plain[2] - 5);

        // A global volume that takes the colours away; a box elsewhere, which does nothing here.
        devex::render::RenderPostVolume everywhere{.global = true, .overrides = static_cast<std::uint32_t>(devex::render::PostOverride::Saturation)};
        everywhere.settings.saturation = 0.0f;
        const std::array<int, 3> inGrey = middle(camera, {everywhere});
        UNSCOPED_INFO("global " << describe(inGrey));
        CHECK(std::abs(inGrey[0] - inGrey[1]) <= 3);
        devex::render::RenderPostVolume away = everywhere;
        away.global = false;
        away.worldToBox = devex::math::inverse(devex::math::translate(devex::math::Mat4{1.0f}, {50.0f, 0.0f, 0.0f}));
        away.halfSize = devex::math::Vec3{5.0f};
        away.blendDistance = 1.0f;
        const std::array<int, 3> outside = middle(camera, {away});
        UNSCOPED_INFO("outside " << describe(outside));
        CHECK(std::abs(outside[0] - plain[0]) <= 2);
        CHECK(std::abs(outside[1] - plain[1]) <= 2);
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
        // The CPU culls, as it does for the shaders of projects and skinned meshes, and counts what it drops.
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .gpuCulling = false});
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
        // The CPU culls, as it does for the shaders of projects and skinned meshes, and counts what it drops.
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .gpuCulling = false});
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
            // A millimetre further every frame, so that the atlas is drawn again rather than kept.
            world.meshes.push_back({.mesh = *cube,
                                    .transform = devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{0.0f, 0.0f, 0.001f * static_cast<float>(frame)}),
                                    .objectId = 1});
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

TEST_CASE("Views draw the scene through other cameras into images the tools show", "[render][gpu]")
{
    using devex::math::Vec2;
    using devex::math::Vec4;
    using devex::render::UiDraw;
    using devex::render::UiSource;
    using devex::render::UiVertex;

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
        const devex::math::Mat4 cubeTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, devex::math::Vec3{0.0f, 0.0f, -3.0f});
        // The main camera turns its back on the cube, which the view looks at.
        const devex::math::Mat4 away =
            devex::math::rotate(devex::math::Mat4{1.0f}, devex::math::radians(180.0f), devex::math::Vec3{0.0f, 1.0f, 0.0f});

        // A quad of the layer of the tools, read from an image.
        const auto quad = [](devex::render::RenderWorld& world, Vec2 min, Vec2 max, UiSource source, std::uint32_t surface) {
            const auto first = static_cast<std::uint32_t>(world.toolsVertices.size());
            world.toolsVertices.push_back(UiVertex{.position = min, .uv = {0.0f, 0.0f}, .color = Vec4{1.0f}});
            world.toolsVertices.push_back(UiVertex{.position = {max.x, min.y}, .uv = {1.0f, 0.0f}, .color = Vec4{1.0f}});
            world.toolsVertices.push_back(UiVertex{.position = max, .uv = {1.0f, 1.0f}, .color = Vec4{1.0f}});
            world.toolsVertices.push_back(UiVertex{.position = {min.x, max.y}, .uv = {0.0f, 1.0f}, .color = Vec4{1.0f}});
            const auto firstIndex = static_cast<std::uint32_t>(world.toolsIndices.size());
            for (const std::uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
            {
                world.toolsIndices.push_back(first + corner);
            }
            world.toolsDraws.push_back(UiDraw{.source = source, .surface = surface, .firstIndex = firstIndex, .indexCount = 6});
        };

        std::uint64_t mainCapture = 0;
        std::uint64_t viewCapture = 0;
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 10; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.environment.color = {1.0f, 0.0f, 0.0f};
            world.meshes.push_back({.mesh = *cube, .transform = cubeTransform, .objectId = 1, .outlined = true});
            world.viewport = devex::math::Extent2D{200, 100};
            world.camera.view = away;
            // The view changes size once, and takes the interface of the game.
            world.views.push_back({.id = 7, .extent = frame < 5 ? devex::math::Extent2D{120, 90} : devex::math::Extent2D{90, 120},
                                   .interface = true});
            world.uiVertices = {UiVertex{.position = {4.0f, 4.0f}, .color = Vec4{1.0f}}, UiVertex{.position = {40.0f, 4.0f}, .color = Vec4{1.0f}},
                                UiVertex{.position = {40.0f, 30.0f}, .color = Vec4{1.0f}}};
            world.uiIndices = {0, 1, 2};
            world.uiDraws.push_back(UiDraw{.indexCount = 3});
            quad(world, {0.0f, 0.0f}, {200.0f, 100.0f}, UiSource::SceneImage, 0);
            quad(world, {200.0f, 0.0f}, {320.0f, 90.0f}, UiSource::View, 7);
            // A view that is not drawn shows nothing.
            quad(world, {200.0f, 100.0f}, {320.0f, 240.0f}, UiSource::View, 8);
            REQUIRE(renderer->endFrame());
            if (frame == 6)
            {
                mainCapture = renderer->requestCapture(160, 160);
                viewCapture = renderer->requestViewCapture(7, 160, 160);
            }
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }

        REQUIRE(captured.size() == 2);
        const auto byRequest = [&](std::uint64_t request) -> const devex::render::CapturedImage& {
            return captured[0].request == request ? captured[0] : captured[1];
        };
        const devex::render::CapturedImage& main = byRequest(mainCapture);
        const devex::render::CapturedImage& view = byRequest(viewCapture);
        CHECK(main.width == 160);
        CHECK(main.height == 80);
        // The view keeps its own shape.
        CHECK(view.width == 90);
        CHECK(view.height == 120);
        const auto middle = [](const devex::render::CapturedImage& image) {
            return (std::size_t{image.height / 2} * image.width + image.width / 2) * 4;
        };
        const auto isSky = [](const devex::render::CapturedImage& image, std::size_t pixel) {
            return image.rgba[pixel] == image.rgba[0] && image.rgba[pixel + 1] == image.rgba[1] && image.rgba[pixel + 2] == image.rgba[2];
        };
        // The main image shows the red sky alone; the view, the cube in its middle.
        CHECK(main.rgba[0] > 100);
        CHECK(isSky(main, middle(main)));
        CHECK(view.rgba[0] > 100);
        CHECK_FALSE(isSky(view, middle(view)));
        CHECK(renderer->stats().drawCalls > 0);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Radiance views capture light in nits, and reflection probes take the place of the sky", "[render][gpu][probe]")
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
        // A polished white metal cube 3 m in front of the camera reflects what is behind the camera.
        auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());
        const devex::render::MaterialHandle metal =
            renderer->createMaterial({.metallicFactor = 1.0f, .roughnessFactor = 0.05f});
        const devex::math::Mat4 cubeTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, devex::math::Vec3{0.0f, 0.0f, -3.0f});
        // A probe that captured a red room of 1000 nits all around.
        constexpr std::uint32_t probeSize = 32;
        std::vector<float> red(std::size_t{probeSize} * probeSize * 6 * 3, 0.0f);
        for (std::size_t pixel = 0; pixel < red.size(); pixel += 3)
        {
            red[pixel] = 1000.0f;
        }
        auto probe = renderer->createReflectionProbe(devex::asset::makeReflectionProbe(probeSize, red));
        REQUIRE(probe.has_value());

        // A camera that shows light as it is: exposed by hand, without anything that changes it.
        devex::render::RenderCamera camera;
        camera.autoExposure = false;
        camera.ev100 = 12.0f;
        camera.tonemapper = devex::render::Tonemapper::None;
        camera.antialiasing = devex::render::Antialiasing::None;
        camera.ambientOcclusion = 0.0f;
        camera.bloom = 0.0f;
        devex::render::RenderCamera behind = camera;
        behind.verticalFov = devex::math::radians(90.0f);
        behind.view = devex::render::cubeFaceView(devex::math::Vec3{0.0f}, 4);

        std::uint64_t radianceRequest = 0;
        std::array<std::uint64_t, 3> requests{};
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 18; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera = camera;
            world.environment.intensity = 2000.0f;
            world.meshes.push_back({.mesh = *cube, .material = metal, .transform = cubeTransform, .objectId = 1});
            // Without a probe, then with one around the cube, then with one elsewhere.
            if (frame >= 6)
            {
                const devex::math::Vec3 where = frame < 12 ? devex::math::Vec3{0.0f, 0.0f, -3.0f} : devex::math::Vec3{50.0f, 0.0f, 0.0f};
                world.reflectionProbes.push_back({.probe = *probe,
                                                  .transform = devex::math::translate(devex::math::Mat4{1.0f}, where),
                                                  .size = devex::math::Vec3{10.0f}});
            }
            // A radiance view of the sky behind the camera.
            world.views.push_back({.id = 30, .camera = behind, .extent = {64, 64}, .radiance = true});
            REQUIRE(renderer->endFrame());
            if (frame == 1)
            {
                radianceRequest = renderer->requestViewCapture(30, 64, 64);
            }
            if (frame % 6 == 2)
            {
                requests[static_cast<std::size_t>(frame / 6)] = renderer->requestCapture(160, 160);
            }
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }
        const auto byRequest = [&](std::uint64_t request) -> const devex::render::CapturedImage* {
            const auto found = std::ranges::find(captured, request, &devex::render::CapturedImage::request);
            return found != captured.end() ? &*found : nullptr;
        };

        // The light of the sky, in nits, whatever the exposure it was drawn at.
        const devex::render::CapturedImage* const radiance = byRequest(radianceRequest);
        REQUIRE(radiance != nullptr);
        CHECK(radiance->rgba.empty());
        REQUIRE(radiance->radiance.size() == std::size_t{64} * 64 * 3);
        const std::size_t centre = (std::size_t{32} * 64 + 32) * 3;
        CHECK(radiance->radiance[centre] == Catch::Approx(2000.0f).epsilon(0.02));
        CHECK(radiance->radiance[centre + 2] == Catch::Approx(2000.0f).epsilon(0.02));

        // The middle of the cube: white sky, red probe, then white sky again outside its box.
        std::array<std::array<std::uint8_t, 3>, 3> middles{};
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            const devex::render::CapturedImage* const image = byRequest(requests[index]);
            REQUIRE(image != nullptr);
            REQUIRE(image->width == 160);
            const std::size_t middle = (std::size_t{60} * 160 + 80) * 4;
            middles[index] = {image->rgba[middle], image->rgba[middle + 1], image->rgba[middle + 2]};
        }
        // The picture keeps the bytes of the target: sRGB on a screen, linear on lavapipe's offscreen
        // images, where the probe, half as bright as the sky, comes out darker.
        CHECK(middles[0][0] > 90);
        CHECK(std::abs(int{middles[0][0]} - int{middles[0][1]}) < 8);
        CHECK(middles[1][0] > 40);
        CHECK(middles[1][1] * 4 < middles[1][0]);
        CHECK(middles[1][2] * 4 < middles[1][0]);
        CHECK(middles[2] == middles[0]);
        renderer->destroyReflectionProbe(*probe);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Irradiance volumes take the place of the sky in the light surfaces take around", "[render][gpu][probe]")
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
        // A rough white cube 3 m in front of the camera, under a white sky.
        auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());
        const devex::render::MaterialHandle white = renderer->createMaterial({.roughnessFactor = 1.0f});
        const devex::math::Mat4 cubeTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, devex::math::Vec3{0.0f, 0.0f, -3.0f});
        // A volume whose probes all saw a dim red room: 400 nits of red, whichever way a surface faces.
        devex::asset::IrradianceVolumeData red{.countX = 2, .countY = 2, .countZ = 2};
        red.coefficients.assign(red.probeCount() * devex::asset::irradianceCoefficients, 0.0f);
        for (std::size_t probe = 0; probe < red.probeCount(); ++probe)
        {
            red.coefficients[probe * devex::asset::irradianceCoefficients] = 400.0f / 0.282095f;
        }
        auto volume = renderer->createIrradianceVolume(red);
        REQUIRE(volume.has_value());

        devex::render::RenderCamera camera;
        camera.autoExposure = false;
        camera.ev100 = 12.0f;
        camera.tonemapper = devex::render::Tonemapper::None;
        camera.antialiasing = devex::render::Antialiasing::None;
        camera.ambientOcclusion = 0.0f;
        camera.bloom = 0.0f;

        std::array<std::uint64_t, 3> requests{};
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 18; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera = camera;
            world.environment.intensity = 2000.0f;
            world.meshes.push_back({.mesh = *cube, .material = white, .transform = cubeTransform, .objectId = 1});
            // Without a volume, then with one around the cube, then with one elsewhere.
            if (frame >= 6)
            {
                const devex::math::Vec3 where = frame < 12 ? devex::math::Vec3{0.0f, 0.0f, -3.0f} : devex::math::Vec3{50.0f, 0.0f, 0.0f};
                world.irradianceVolumes.push_back({.volume = *volume,
                                                   .transform = devex::math::translate(devex::math::Mat4{1.0f}, where),
                                                   .size = devex::math::Vec3{6.0f}});
            }
            REQUIRE(renderer->endFrame());
            if (frame % 6 == 2)
            {
                requests[static_cast<std::size_t>(frame / 6)] = renderer->requestCapture(160, 160);
            }
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }
        std::array<std::array<std::uint8_t, 3>, 3> middles{};
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            const auto found = std::ranges::find(captured, requests[index], &devex::render::CapturedImage::request);
            REQUIRE(found != captured.end());
            const std::size_t middle = (std::size_t{60} * 160 + 80) * 4;
            middles[index] = {found->rgba[middle], found->rgba[middle + 1], found->rgba[middle + 2]};
        }
        // Lit by the white sky, then by the dim red of the volume, then by the sky again.
        CHECK(middles[0][0] > 90);
        CHECK(std::abs(int{middles[0][0]} - int{middles[0][1]}) < 8);
        CHECK(middles[1][0] > 10);
        CHECK(middles[1][0] < middles[0][0]);
        CHECK(middles[1][1] * 3 < middles[1][0]);
        CHECK(middles[2] == middles[0]);
        renderer->destroyIrradianceVolume(*volume);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Particles and ribbons blend among the blended surfaces without validation errors", "[render][gpu]")
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
        const devex::render::MaterialHandle glass =
            renderer->createMaterial({.baseColorFactor = {0.4f, 0.6f, 0.9f, 0.35f}, .alphaMode = devex::asset::AlphaMode::Blend});
        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{0.0f, 0.0f, 6.0f});

        // A dark sky with a bright glow in its middle, and the same sky without it.
        const auto draw = [&](devex::render::RenderWorld& world, bool particles) {
            world.camera.view = devex::math::inverse(cameraTransform);
            world.camera.autoExposure = false;
            world.camera.ev100 = 0.0f;
            world.camera.antialiasing = devex::render::Antialiasing::None;
            world.environment.color = {0.02f, 0.02f, 0.02f};
            world.environment.intensity = 1.0f;
            world.sun.illuminance = {5.0f, 5.0f, 5.0f};
            // Glass behind the particles and glass in front of them: they sort together.
            world.meshes.push_back({.mesh = *cube,
                                    .material = glass,
                                    .transform = devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{2.5f, 0.0f, -2.0f}),
                                    .objectId = 1});
            world.meshes.push_back({.mesh = *cube,
                                    .material = glass,
                                    .transform = devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{-2.5f, 0.0f, 3.0f}),
                                    .objectId = 2});
            if (!particles)
            {
                return;
            }
            // Soft discs by alpha, lit; bright additive streaks; and a ribbon.
            for (int index = 0; index < 5; ++index)
            {
                world.particles.push_back({.position = {static_cast<float>(index) * 0.2f - 0.4f, 0.0f, 0.0f},
                                           .size = 1.5f,
                                           .color = {1.0f, 0.8f, 0.4f, 0.8f}});
            }
            world.particleDraws.push_back({.first = 0, .count = 5, .lit = true, .softness = 0.3f});
            world.particles.push_back({.position = {0.0f, 0.0f, 1.0f}, .size = 1.0f, .color = {40.0f, 40.0f, 40.0f, 1.0f}});
            world.particles.push_back({.position = {1.0f, 1.0f, 1.0f},
                                       .size = 0.1f,
                                       .color = {8.0f, 4.0f, 1.0f, 1.0f},
                                       .stretch = {0.5f, 0.0f, 0.0f}});
            world.particleDraws.push_back({.first = 5,
                                           .count = 2,
                                           .blend = devex::render::ParticleBlend::Additive,
                                           .facing = devex::render::ParticleFacing::Stretched,
                                           .center = {0.0f, 0.0f, 1.0f}});
            for (int index = 0; index < 4; ++index)
            {
                world.trailPoints.push_back({.position = {static_cast<float>(index) - 1.5f, -1.0f, 0.5f},
                                             .width = 0.2f,
                                             .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                             .direction = {1.0f, 0.0f, 0.0f},
                                             .u = static_cast<float>(index) / 3.0f});
            }
            world.trailSegments = {0, 1, 2};
            world.particleDraws.push_back({.ribbons = true, .first = 0, .count = 3, .center = {0.0f, -1.0f, 0.5f}});
        };

        const std::uint64_t without = renderer->requestCapture(64, 48);
        std::vector<devex::render::CapturedImage> captured;
        std::uint64_t with = 0;
        for (int frame = 0; frame < 10; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            draw(world, frame >= 4);
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            if (frame >= 4)
            {
                // Two blended cubes and three batches of particles: nothing else draws in them.
                CHECK(renderer->stats().drawCalls == 5);
            }
            if (frame == 5)
            {
                with = renderer->requestCapture(64, 48);
            }
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }
        REQUIRE(captured.size() == 2);
        CHECK(captured[0].request == without);
        CHECK(captured[1].request == with);
        // The middle of the picture glows where the additive particle stands.
        const std::size_t middle = (std::size_t{24} * 64 + 32) * 4;
        CHECK(captured[1].rgba[middle] > captured[0].rgba[middle] + 50);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Sprites draw by layer and order through an orthographic camera, and are picked", "[render][gpu]")
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
        // Two pixels, red then blue, read at their nearest pixel.
        devex::asset::TextureData pixels{.format = devex::asset::TextureFormat::Rgba8Srgb,
                                         .filter = devex::asset::TextureFilter::Nearest};
        pixels.mips.push_back({.width = 2,
                               .height = 1,
                               .bytes = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}, std::byte{0},
                                         std::byte{0}, std::byte{255}, std::byte{255}}});
        const auto texture = renderer->createTexture(pixels);
        REQUIRE(texture.has_value());
        const auto cube = renderer->createMesh(devex::asset::makeCube());
        REQUIRE(cube.has_value());

        const auto at = [](float x, float y, float z) {
            return devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{x, y, z});
        };
        std::vector<devex::render::PickResult> results;
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 8; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            // Four meters high, from ten meters in front of the XY plane; colours kept as they are.
            world.camera.view = devex::math::inverse(at(0.0f, 0.0f, 10.0f));
            world.camera.projection = devex::render::Projection::Orthographic;
            world.camera.orthographicSize = 2.0f;
            world.camera.farPlane = 100.0f;
            world.camera.autoExposure = false;
            world.camera.antialiasing = devex::render::Antialiasing::None;
            world.camera.tonemapper = devex::render::Tonemapper::None;
            world.camera.bloom = 0.0f;
            world.environment.color = {0.0f, 0.0f, 0.0f};
            // The red half in a layer in front, although farther; the blue half nearer, behind it.
            world.sprites.push_back({.transform = at(0.0f, 0.0f, -5.0f),
                                     .size = {2.0f, 2.0f},
                                     .uvRect = {0.0f, 0.0f, 0.5f, 1.0f},
                                     .naturalSize = {2.0f, 2.0f},
                                     .texture = *texture,
                                     .layer = 1,
                                     .objectId = 11,
                                     .outlined = true});
            world.sprites.push_back({.transform = at(0.0f, 0.0f, 2.0f),
                                     .size = {2.0f, 2.0f},
                                     .uvRect = {0.5f, 0.0f, 1.0f, 1.0f},
                                     .naturalSize = {2.0f, 2.0f},
                                     .texture = *texture,
                                     .objectId = 12});
            // Sliced, tiled, flipped, lit and additive sprites at the edges, and one out of view.
            world.sprites.push_back({.transform = at(-2.2f, 1.0f, 0.0f),
                                     .size = {1.5f, 0.5f},
                                     .naturalSize = {0.5f, 0.5f},
                                     .border = {0.1f, 0.1f, 0.1f, 0.1f},
                                     .texture = *texture,
                                     .mode = devex::render::SpriteMode::Sliced,
                                     .flipX = true,
                                     .lit = true});
            world.sprites.push_back({.transform = at(2.2f, -1.0f, 0.0f),
                                     .size = {1.5f, 0.5f},
                                     .naturalSize = {0.25f, 0.25f},
                                     .texture = *texture,
                                     .mode = devex::render::SpriteMode::Tiled,
                                     .additive = true});
            world.sprites.push_back({.transform = at(50.0f, 0.0f, 0.0f), .texture = *texture});
            // An opaque cube beside them.
            world.meshes.push_back({.mesh = *cube, .transform = at(2.2f, 1.0f, 0.0f), .objectId = 13});
            if (frame == 4)
            {
                world.pick = devex::render::PickRequest{.x = 160, .y = 120, .id = 1};
                static_cast<void>(renderer->requestCapture(64, 48));
            }
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            std::ranges::copy(renderer->takePickResults(), std::back_inserter(results));
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }
        REQUIRE(results.size() == 1);
        // The sprite in front by its layer is the one under the middle.
        CHECK(results[0].objectId == 11);
        REQUIRE(captured.size() == 1);
        const std::size_t middle = (std::size_t{24} * 64 + 32) * 4;
        CHECK(captured[0].rgba[middle] > 200);
        CHECK(captured[0].rgba[middle + 2] < 40);
        renderer->destroyTexture(*texture);
        renderer->destroyMesh(*cube);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Temporal antialiasing keeps distant orthographic sprites stationary", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer.has_value());

        // The editor's 2D camera stands 500 m from the XY plane. Jitter must still move the
        // image by less than a pixel, not grow with that distance. Cover two full TAA cycles.
        constexpr int capturedFrames = 16;
        std::vector<devex::render::CapturedImage> captured;
        std::vector<devex::render::PickResult> picks;
        for (int frame = 0; frame < capturedFrames + 3; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.viewport = devex::math::Extent2D{320, 240};
            world.camera.view = devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.0f, -500.0f});
            world.camera.projection = devex::render::Projection::Orthographic;
            world.camera.orthographicSize = 5.0f;
            world.camera.farPlane = 1000.0f;
            world.camera.autoExposure = false;
            world.camera.antialiasing = devex::render::Antialiasing::Temporal;
            world.camera.tonemapper = devex::render::Tonemapper::None;
            world.camera.ambientOcclusion = 0.0f;
            world.camera.bloom = 0.0f;
            world.environment.color = {0.0f, 0.0f, 0.0f};
            world.sprites.push_back({.size = {1.0f, 1.0f}, .color = {1.0f, 0.0f, 0.0f, 1.0f}, .objectId = 13});
            if (frame < capturedFrames)
            {
                static_cast<void>(renderer->requestCapture(320, 240));
                world.pick = devex::render::PickRequest{.x = 160, .y = 120, .id = static_cast<std::uint64_t>(frame + 1)};
            }
            REQUIRE(renderer->endFrame());
            std::ranges::copy(renderer->takePickResults(), std::back_inserter(picks));
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }

        REQUIRE(captured.size() == capturedFrames);
        REQUIRE(picks.size() == capturedFrames);
        for (const devex::render::PickResult& pick : picks)
        {
            CAPTURE(pick.request);
            CHECK(pick.objectId == 13);
        }
        for (const devex::render::CapturedImage& image : captured)
        {
            CAPTURE(image.request);
            REQUIRE(image.width == 320);
            REQUIRE(image.height == 240);
            REQUIRE(image.rgba.size() == std::size_t{320} * 240 * 4);
            std::uint32_t redPixels = 0;
            double sumX = 0.0;
            double sumY = 0.0;
            for (std::uint32_t y = 0; y < image.height; ++y)
            {
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    const std::size_t pixel = (std::size_t{y} * image.width + x) * 4;
                    if (image.rgba[pixel] > 200 && image.rgba[pixel + 1] < 40 && image.rgba[pixel + 2] < 40)
                    {
                        ++redPixels;
                        sumX += x;
                        sumY += y;
                    }
                }
            }
            // A 24 by 24 pixel square stays visible, centered within one pixel throughout.
            CHECK(redPixels >= 500);
            if (redPixels > 0)
            {
                CHECK(sumX / redPixels >= 158.5);
                CHECK(sumX / redPixels <= 160.5);
                CHECK(sumY / redPixels >= 118.5);
                CHECK(sumY / redPixels <= 120.5);
            }
        }
    }
    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Picking at a stationary edge does not alternate with temporal antialiasing", "[render][gpu]")
{
    const bool orthographic = GENERATE(true, false);
    const bool inside = GENERATE(true, false);
    CAPTURE(orthographic, inside);
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer.has_value());

        const float verticalFov = devex::math::radians(60.0f);
        const float pixelsPerMeter = orthographic ? 60.0f : 120.0f / (std::tan(verticalFov * 0.5f) * 6.0f);
        // The sprite spans x=100.25..120.25. The samples at 100.5 and 120.5 are a quarter
        // pixel inside and outside. Picking must agree with the stable outline in every TAA phase.
        const devex::math::Mat4 transform = devex::math::translate(
            devex::math::Mat4{1.0f}, {(110.25f - 160.0f) / pixelsPerMeter, 0.0f, 0.0f});
        std::vector<devex::render::PickResult> picks;
        for (int frame = 0; frame < 19; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.viewport = {320, 240};
            world.camera.view = devex::math::translate(devex::math::Mat4{1.0f},
                                                      {0.0f, 0.0f, orthographic ? -500.0f : -6.0f});
            world.camera.projection = orthographic ? devex::render::Projection::Orthographic
                                                   : devex::render::Projection::Perspective;
            world.camera.orthographicSize = 2.0f;
            world.camera.verticalFov = verticalFov;
            world.camera.antialiasing = devex::render::Antialiasing::Temporal;
            world.sprites.push_back({.transform = transform,
                                     .size = {20.0f / pixelsPerMeter, 20.0f / pixelsPerMeter},
                                     .objectId = 13,
                                     .outlined = true});
            if (frame < 16)
            {
                world.pick = devex::render::PickRequest{.x = inside ? 100u : 120u,
                                                        .y = 120,
                                                        .id = static_cast<std::uint64_t>(frame + 1)};
            }
            REQUIRE(renderer->endFrame());
            std::ranges::copy(renderer->takePickResults(), std::back_inserter(picks));
        }
        REQUIRE(picks.size() == 16);
        for (const devex::render::PickResult& pick : picks)
        {
            CAPTURE(pick.request);
            CHECK(pick.objectId == (inside ? 13u : 0u));
        }
    }
    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Tilemaps draw their tiles in one batch among the sprites, and are picked", "[render][gpu]")
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
        // Green then red, at their nearest pixel.
        devex::asset::TextureData pixels{.format = devex::asset::TextureFormat::Rgba8Srgb,
                                         .filter = devex::asset::TextureFilter::Nearest};
        pixels.mips.push_back({.width = 2,
                               .height = 1,
                               .bytes = {std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}, std::byte{255},
                                         std::byte{0}, std::byte{0}, std::byte{255}}});
        const auto texture = renderer->createTexture(pixels);
        REQUIRE(texture.has_value());

        const auto at = [](float x, float y, float z) {
            return devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{x, y, z});
        };
        std::vector<devex::render::PickResult> results;
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 8; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(at(0.0f, 0.0f, 10.0f));
            world.camera.projection = devex::render::Projection::Orthographic;
            world.camera.orthographicSize = 2.0f;
            world.camera.farPlane = 100.0f;
            world.camera.autoExposure = false;
            world.camera.antialiasing = devex::render::Antialiasing::None;
            world.camera.tonemapper = devex::render::Tonemapper::None;
            world.camera.bloom = 0.0f;
            world.environment.color = {0.0f, 0.0f, 0.0f};
            // A red sprite behind, in the layer of the tilemap but at a lower order.
            world.sprites.push_back({.transform = at(0.0f, 0.0f, 1.0f),
                                     .size = {3.0f, 3.0f},
                                     .uvRect = {0.5f, 0.0f, 1.0f, 1.0f},
                                     .naturalSize = {3.0f, 3.0f},
                                     .texture = *texture,
                                     .layer = 1,
                                     .objectId = 5});
            // Six by four green cells of half a meter around the middle, one mirrored, and one far
            // away that the camera does not see.
            const auto first = static_cast<std::uint32_t>(world.tiles.size());
            for (int y = -2; y < 2; ++y)
            {
                for (int x = -3; x < 3; ++x)
                {
                    world.tiles.push_back({.cell = {x, y},
                                           .uvRect = x == 0 ? devex::math::Vec4{0.5f, 0.0f, 0.0f, 1.0f}
                                                            : devex::math::Vec4{0.0f, 0.0f, 0.5f, 1.0f},
                                           .texture = *texture});
                }
            }
            world.tiles.push_back({.cell = {400, 0}, .uvRect = {0.0f, 0.0f, 0.5f, 1.0f}, .texture = *texture});
            world.tilemaps.push_back({.transform = at(0.0f, 0.0f, 0.0f),
                                      .cellSize = {0.5f, 0.5f},
                                      .firstTile = first,
                                      .tileCount = static_cast<std::uint32_t>(world.tiles.size()) - first,
                                      .layer = 1,
                                      .order = 1,
                                      .objectId = 9,
                                      .outlined = true});
            if (frame == 4)
            {
                world.pick = devex::render::PickRequest{.x = 150, .y = 110, .id = 1};
                static_cast<void>(renderer->requestCapture(64, 48));
            }
            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            if (frame >= 4)
            {
                // The sprite, then every tile the camera sees in one draw.
                CHECK(renderer->stats().drawCalls == 1);
            }
            std::ranges::copy(renderer->takePickResults(), std::back_inserter(results));
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }
        REQUIRE(results.size() == 1);
        CHECK(results[0].objectId == 9);
        REQUIRE(captured.size() == 1);
        // Left of the middle, a green tile covers the red sprite.
        const std::size_t pixel = (std::size_t{24} * 64 + 28) * 4;
        CHECK(captured[0].rgba[pixel + 1] > 200);
        CHECK(captured[0].rgba[pixel] < 40);
        renderer->destroyTexture(*texture);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Scrolling tilemaps do not sample neighbouring atlas cells", "[render][gpu][tilemap]")
{
    const auto filter = GENERATE(devex::asset::TextureFilter::Nearest, devex::asset::TextureFilter::Linear);
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform);
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window);
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer);

        // A green 16x16 tile within a 17x8 atlas. Its neighbours are red, so that even a
        // single sample across its edges is visible. No mipmaps: test atlas boundaries,
        // including nearest sampling at pixel centres exactly on a mirrored cell edge.
        devex::asset::TextureData atlas{.format = devex::asset::TextureFormat::Rgba8Srgb, .filter = filter};
        auto& mip = atlas.mips.emplace_back(devex::asset::TextureMip{
            .width = 272, .height = 128, .bytes = std::vector<std::byte>(272 * 128 * 4)});
        for (std::uint32_t y = 0; y < mip.height; ++y)
        {
            for (std::uint32_t x = 0; x < mip.width; ++x)
            {
                const bool green = x >= 128 && x < 144 && y >= 48 && y < 64;
                const std::size_t pixel = (std::size_t{y} * mip.width + x) * 4;
                mip.bytes[pixel] = green ? std::byte{0} : std::byte{255};
                mip.bytes[pixel + 1] = green ? std::byte{255} : std::byte{0};
                mip.bytes[pixel + 2] = std::byte{0};
                mip.bytes[pixel + 3] = std::byte{255};
            }
        }
        const auto texture = renderer->createTexture(atlas);
        REQUIRE(texture);

        constexpr int capturedFrames = 32;
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < capturedFrames + 7; ++frame)
        {
            auto& world = renderer->beginFrame();
            world.viewport = devex::math::Extent2D{320, 240};
            // One pixel of camera travel, in small steps, with exactly half-pixel phases.
            const float phase = static_cast<float>(std::max(frame - 4, 0)) / 32.0f;
            world.camera.view = devex::math::translate(devex::math::Mat4{1.0f}, {-phase / 24.0f, phase / 24.0f, -10.0f});
            world.camera.projection = devex::render::Projection::Orthographic;
            world.camera.orthographicSize = 5.0f;
            world.camera.autoExposure = false;
            world.camera.antialiasing = devex::render::Antialiasing::None;
            world.camera.tonemapper = devex::render::Tonemapper::None;
            world.camera.ambientOcclusion = 0.0f;
            world.camera.bloom = 0.0f;
            world.environment.color = {0.0f, 0.0f, 0.0f};
            for (int y = -6; y < 6; ++y)
            {
                for (int x = -8; x < 8; ++x)
                {
                    devex::math::Vec4 uv{128.0f / 272.0f, 48.0f / 128.0f, 144.0f / 272.0f, 64.0f / 128.0f};
                    if (x % 2 != 0)
                    {
                        std::swap(uv.x, uv.z);
                    }
                    if (y % 2 != 0)
                    {
                        std::swap(uv.y, uv.w);
                    }
                    world.tiles.push_back({.cell = {x, y}, .uvRect = uv, .texture = *texture});
                }
            }
            world.tilemaps.push_back({.cellSize = {1.0f, 1.0f}, .tileCount = static_cast<std::uint32_t>(world.tiles.size()),
                                      .objectId = 17});
            if (frame >= 4 && frame < capturedFrames + 4)
            {
                static_cast<void>(renderer->requestCapture(320, 240));
            }
            REQUIRE(renderer->endFrame());
            for (auto& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }
        REQUIRE(captured.size() == capturedFrames);
        for (const auto& image : captured)
        {
            CAPTURE(filter, image.request);
            REQUIRE(image.width == 320);
            REQUIRE(image.height == 240);
            std::size_t seams = 0;
            for (std::uint32_t y = 8; y < image.height - 8; ++y)
            {
                for (std::uint32_t x = 8; x < image.width - 8; ++x)
                {
                    const std::size_t pixel = (std::size_t{y} * image.width + x) * 4;
                    if (image.rgba[pixel] > 8 || image.rgba[pixel + 1] < 245 || image.rgba[pixel + 2] > 8)
                    {
                        ++seams;
                    }
                }
            }
            CHECK(seams == 0);
        }
        renderer->destroyTexture(*texture);
    }
    for (const auto& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("2D lights shine on sprites over the tint of the canvas, through normal maps, hidden by occluders",
          "[render][gpu][light2d]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer.has_value());
        // A white pixel, and a normal map facing +X.
        devex::asset::TextureData white{.format = devex::asset::TextureFormat::Rgba8Srgb, .filter = devex::asset::TextureFilter::Nearest};
        white.mips.push_back({.width = 1, .height = 1, .bytes = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}});
        devex::asset::TextureData facing{.format = devex::asset::TextureFormat::Rgba8Unorm, .filter = devex::asset::TextureFilter::Nearest};
        facing.mips.push_back({.width = 1, .height = 1, .bytes = {std::byte{255}, std::byte{128}, std::byte{255}, std::byte{255}}});
        const auto texture = renderer->createTexture(white);
        const auto normals = renderer->createTexture(facing);
        REQUIRE(texture.has_value());
        REQUIRE(normals.has_value());

        const auto at = [](float x, float y) { return devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{x, y, 0.0f}); };
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 8; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{0.0f, 0.0f, 10.0f}));
            world.camera.projection = devex::render::Projection::Orthographic;
            world.camera.orthographicSize = 2.0f;
            world.camera.farPlane = 100.0f;
            world.camera.autoExposure = false;
            world.camera.antialiasing = devex::render::Antialiasing::None;
            world.camera.tonemapper = devex::render::Tonemapper::None;
            world.camera.bloom = 0.0f;
            world.environment.color = {0.0f, 0.0f, 0.0f};
            // A grey wall behind everything, darkened by the canvas; an unshaded green square on it.
            world.sprites.push_back({.size = {8.0f, 6.0f}, .naturalSize = {8.0f, 6.0f}, .color = {0.5f, 0.5f, 0.5f, 1.0f}});
            world.sprites.push_back({.transform = at(2.0f, 1.5f), .size = {0.5f, 0.5f}, .color = {0.0f, 0.5f, 0.0f, 1.0f}, .unshaded = true, .order = 1});
            // Two squares facing +X by their normal map, the second one flipped, lit by a blue light
            // on their right that shines on their light mask alone.
            for (const bool flipped : {false, true})
            {
                world.sprites.push_back({.transform = at(flipped ? 1.4f : 0.6f, -1.2f),
                                         .size = {0.6f, 0.6f},
                                         .naturalSize = {0.6f, 0.6f},
                                         .texture = *texture,
                                         .flipX = flipped,
                                         .lightMask = 2,
                                         .normalTexture = *normals,
                                         .order = 1});
            }
            world.canvasModulate = {0.2f, 0.2f, 0.2f};
            world.lights2D.push_back({.position = {-1.5f, 0.0f}, .color = {1.0f, 0.0f, 0.0f}, .radius = 2.0f, .shadows = true});
            world.lights2D.push_back(
                {.position = {3.0f, -1.2f}, .color = {0.0f, 0.0f, 1.0f}, .radius = 4.0f, .height = 0.1f, .itemMask = 2});
            // A wall between the red light and what lies right of it.
            world.occluders2D.push_back({.from = {-1.0f, -0.5f}, .to = {-1.0f, 0.5f}});
            if (frame == 5)
            {
                static_cast<void>(renderer->requestCapture(64, 48));
            }
            REQUIRE(renderer->endFrame());
            for (devex::render::CapturedImage& image : renderer->takeCaptures())
            {
                captured.push_back(std::move(image));
            }
        }
        REQUIRE(captured.size() == 1);
        const devex::render::CapturedImage& image = captured[0];
        const auto pixel = [&](int x, int y) {
            const std::size_t index = (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
            return std::array<int, 3>{image.rgba[index], image.rgba[index + 1], image.rgba[index + 2]};
        };
        // Above the red light: reddened. Behind the wall, out of its reach, and beyond its radius: only
        // the tint of the canvas.
        const auto lit = pixel(14, 12);
        const auto shadowed = pixel(25, 23);
        const auto far = pixel(55, 23);
        CAPTURE(lit, shadowed, far);
        CHECK(lit[0] > lit[1] + 40);
        CHECK(std::abs(shadowed[0] - shadowed[1]) < 8);
        CHECK(std::abs(far[0] - far[1]) < 8);
        CHECK(std::abs(shadowed[1] - far[1]) < 8);
        CHECK(lit[1] == far[1]);
        // The unshaded square keeps its colour, brighter than the tinted wall.
        const auto unshaded = pixel(55, 5);
        CAPTURE(unshaded);
        CHECK(unshaded[1] > far[1] + 60);
        // The square facing the blue light turns blue, the flipped one facing away does not.
        const auto facingLight = pixel(38, 37);
        const auto facingAway = pixel(48, 37);
        CAPTURE(facingLight, facingAway);
        CHECK(facingLight[2] > facingLight[0] + 40);
        CHECK(std::abs(facingAway[2] - facingAway[0]) < 8);
        renderer->destroyTexture(*texture);
        renderer->destroyTexture(*normals);
    }
    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}
