// The WebGPU renderer, on a computer with Dawn: it draws a 2D scene as Vulkan does, and decodes the blocks of BC
// textures for a device without them as basisu does.
#include "render/webgpu/BlockDecoding.hpp"

#include <devex/asset/Primitives.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <utility>
#include <random>
#include <string>
#include <vector>

namespace {

using devex::core::LogLevel;
using devex::core::LogRecord;

// Collects error messages, those of WebGPU and of the Vulkan validation layer among them.
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

[[nodiscard]] devex::math::Mat4 at(float x, float y, float z)
{
    return devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{x, y, z});
}

// The window, with its interface, of a 2D scene drawn by one API: sprites simple, sliced, tiled, flipped, tinted and
// additive over the colour of the environment, then a rectangle and a rounded one of the interface.
[[nodiscard]] devex::render::CapturedImage drawScene(devex::render::RenderApi api)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .api = api});
    if (!renderer)
    {
        FAIL(std::format("{}", renderer.error()));
    }
    // Four pixels: red, blue, green and white, read at their nearest.
    devex::asset::TextureData pixels{.format = devex::asset::TextureFormat::Rgba8Srgb, .filter = devex::asset::TextureFilter::Nearest};
    pixels.mips.push_back({.width = 2,
                           .height = 2,
                           .bytes = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}, std::byte{0}, std::byte{0},
                                     std::byte{255}, std::byte{255}, std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
                                     std::byte{255}, std::byte{255}, std::byte{255}, std::byte{128}}});
    const auto texture = renderer->createTexture(pixels);
    REQUIRE(texture.has_value());

    std::vector<devex::render::CapturedImage> captured;
    for (int frame = 0; frame < 10 && captured.empty(); ++frame)
    {
        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::inverse(at(0.0f, 0.0f, 10.0f));
        world.camera.projection = devex::render::Projection::Orthographic;
        world.camera.orthographicSize = 2.0f;
        world.camera.farPlane = 100.0f;
        world.camera.autoExposure = false;
        world.camera.ev100 = 0.0f;
        world.camera.antialiasing = devex::render::Antialiasing::None;
        world.camera.tonemapper = devex::render::Tonemapper::None;
        world.camera.bloom = 0.0f;
        world.environment.color = {0.1f, 0.15f, 0.3f};
        world.environment.intensity = 1.0f;
        world.canvasModulate = {1.0f, 0.9f, 0.8f};
        world.sprites.push_back({.transform = at(0.0f, 0.0f, 0.0f), .size = {2.0f, 2.0f}, .naturalSize = {2.0f, 2.0f},
                                 .texture = *texture, .layer = 1});
        world.sprites.push_back({.transform = at(-2.0f, 1.0f, 0.0f), .size = {1.5f, 0.75f}, .naturalSize = {0.5f, 0.5f},
                                 .border = {0.1f, 0.1f, 0.1f, 0.1f}, .texture = *texture,
                                 .mode = devex::render::SpriteMode::Sliced, .flipX = true});
        world.sprites.push_back({.transform = at(2.0f, -1.0f, 0.0f), .size = {1.5f, 0.75f}, .naturalSize = {0.25f, 0.25f},
                                 .texture = *texture, .mode = devex::render::SpriteMode::Tiled});
        world.sprites.push_back({.transform = at(-2.0f, -1.2f, 0.0f), .size = {1.0f, 1.0f}, .color = {0.5f, 0.5f, 0.5f, 1.0f},
                                 .additive = true});
        world.sprites.push_back({.transform = at(2.0f, 1.2f, 0.0f), .size = {1.0f, 0.5f}, .color = {0.2f, 0.9f, 0.4f, 0.6f},
                                 .unshaded = true});
        // A rectangle, and a rounded one over it, of the interface.
        const auto rectangle = [&world](float left, float top, float right, float bottom, devex::math::Vec4 color) {
            const auto first = static_cast<std::uint32_t>(world.uiVertices.size());
            world.uiVertices.push_back({.position = {left, top}, .color = color});
            world.uiVertices.push_back({.position = {right, top}, .color = color});
            world.uiVertices.push_back({.position = {right, bottom}, .color = color});
            world.uiVertices.push_back({.position = {left, bottom}, .color = color});
            const auto index = static_cast<std::uint32_t>(world.uiIndices.size());
            for (const std::uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U})
            {
                world.uiIndices.push_back(first + corner);
            }
            return index;
        };
        world.uiDraws.push_back({.firstIndex = rectangle(10.0f, 10.0f, 90.0f, 50.0f, {0.9f, 0.8f, 0.1f, 1.0f}), .indexCount = 6});
        world.uiDraws.push_back({.kind = devex::render::UiDrawKind::RoundedQuad,
                                 .firstIndex = rectangle(30.0f, 30.0f, 130.0f, 90.0f, {0.1f, 0.3f, 0.9f, 0.7f}),
                                 .indexCount = 6,
                                 .rect = {30.0f, 30.0f, 130.0f, 90.0f},
                                 .radius = 12.0f});
        if (frame == 3)
        {
            static_cast<void>(renderer->requestWindowCapture());
        }
        const devex::core::Result<void> presented = renderer->endFrame();
        if (!presented)
        {
            FAIL(std::format("frame {}: {}", frame, presented.error()));
        }
        for (devex::render::CapturedImage& image : renderer->takeCaptures())
        {
            captured.push_back(std::move(image));
        }
    }
    renderer->destroyTexture(*texture);
    REQUIRE(captured.size() == 1);
    return std::move(captured.front());
}


// The window of a 3D scene drawn by one API: a rough sphere and a metallic one, a textured box on a plane and a
// skinned box, lit by the sun, a point light and a spot, which cast shadows, and the environment baked from a sky
// texture, in a height fog, with particles and a ribbon.
[[nodiscard]] devex::render::CapturedImage drawMeshes(devex::render::RenderApi api)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    // The geometry as it is given, and every instance drawn as the CPU sends it.
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .meshLodError = 0.0f, .api = api});
    if (!renderer)
    {
        FAIL(std::format("{}", renderer.error()));
    }
    devex::asset::TextureData checker{.format = devex::asset::TextureFormat::Rgba8Srgb, .filter = devex::asset::TextureFilter::Nearest};
    checker.mips.push_back({.width = 2,
                            .height = 2,
                            .bytes = {std::byte{230}, std::byte{230}, std::byte{230}, std::byte{255}, std::byte{40}, std::byte{90},
                                      std::byte{200}, std::byte{255}, std::byte{40}, std::byte{90}, std::byte{200}, std::byte{255},
                                      std::byte{230}, std::byte{230}, std::byte{230}, std::byte{255}}});
    const auto texture = renderer->createTexture(checker);
    REQUIRE(texture.has_value());
    // A sky of 16 by 8 pixels: blue above, brown below, brighter towards one side, which the environment is baked from.
    devex::asset::TextureData skyPixels{.format = devex::asset::TextureFormat::Rgba8Unorm};
    skyPixels.mips.push_back({.width = 16, .height = 8});
    for (std::uint32_t y = 0; y < 8; ++y)
    {
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const auto shade = static_cast<std::uint8_t>(120 + x * 8);
            const std::array<std::uint8_t, 4> pixel = y < 4 ? std::array<std::uint8_t, 4>{60, 110, shade, 255}
                                                            : std::array<std::uint8_t, 4>{shade, 90, 50, 255};
            for (const std::uint8_t channel : pixel)
            {
                skyPixels.mips.back().bytes.push_back(std::byte{channel});
            }
        }
    }
    const auto skyTexture = renderer->createTexture(skyPixels);
    REQUIRE(skyTexture.has_value());
    const auto sphere = renderer->createMesh(devex::asset::makeUvSphere(0.5f));
    const auto cube = renderer->createMesh(devex::asset::makeCube());
    const auto plane = renderer->createMesh(devex::asset::makePlane(8.0f));
    // A box whose vertices all follow one bone.
    devex::asset::MeshData skinnedBox = devex::asset::makeCube(0.6f);
    skinnedBox.skin.assign(skinnedBox.vertices.size(), devex::asset::VertexSkin{.joints = {0, 0, 0, 0}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}});
    skinnedBox.inverseBind.assign(1, devex::math::Mat4(1.0f));
    const auto skinned = renderer->createMesh(skinnedBox);
    REQUIRE((sphere && cube && plane && skinned));
    const devex::render::MaterialHandle rough = renderer->createMaterial({.baseColorFactor = {0.8f, 0.2f, 0.1f, 1.0f}, .roughnessFactor = 0.7f});
    const devex::render::MaterialHandle metal =
        renderer->createMaterial({.baseColorFactor = {0.9f, 0.8f, 0.5f, 1.0f}, .metallicFactor = 1.0f, .roughnessFactor = 0.3f});
    const devex::render::MaterialHandle textured = renderer->createMaterial({.baseColorTexture = *texture, .roughnessFactor = 0.5f});
    const devex::render::MaterialHandle ground = renderer->createMaterial({.baseColorFactor = {0.4f, 0.45f, 0.4f, 1.0f}});
    const devex::render::MaterialHandle glass = renderer->createMaterial(
        {.baseColorFactor = {0.2f, 0.6f, 1.0f, 0.5f}, .roughnessFactor = 0.2f, .alphaMode = devex::asset::AlphaMode::Blend});

    std::vector<devex::render::CapturedImage> captured;
    for (int frame = 0; frame < 12 && captured.empty(); ++frame)
    {
        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::lookAt(devex::math::Vec3{0.0f, 2.0f, 5.0f}, devex::math::Vec3{0.0f, 0.3f, 0.0f},
                                                devex::math::Vec3{0.0f, 1.0f, 0.0f});
        world.camera.autoExposure = false;
        world.camera.ev100 = 0.0f;
        world.camera.antialiasing = devex::render::Antialiasing::None;
        world.camera.bloom = 0.0f;
        world.camera.ambientOcclusion = 0.0f;
        world.camera.screenReflections = 0.0f;
        world.environment.sky = *skyTexture;
        world.environment.color = {0.9f, 1.0f, 1.0f};
        world.environment.intensity = 1.2f;
        world.environment.rotation = 0.5f;
        world.sun.direction = {-0.4f, -1.0f, -0.5f};
        world.sun.illuminance = {2.5f, 2.4f, 2.2f};
        world.sun.castShadows = true;
        // A height fog, lit by the sky and the sun, which melts into the sky far away.
        world.fog.enabled = true;
        world.fog.density = 0.08f;
        world.fog.height = 0.5f;
        world.fog.falloff = 2.0f;
        world.fog.startDistance = 1.0f;
        world.lights.push_back({.position = {1.5f, 1.0f, 1.0f}, .intensity = {3.0f, 1.0f, 0.5f}, .range = 5.0f, .castShadows = true});
        world.lights.push_back({.type = devex::render::LightType::Spot,
                                .position = {-1.5f, 2.5f, 1.0f},
                                .direction = {0.3f, -1.0f, -0.2f},
                                .intensity = {0.5f, 1.0f, 4.0f},
                                .range = 6.0f,
                                .innerAngle = 0.3f,
                                .outerAngle = 0.6f,
                                .castShadows = true});
        world.meshes.push_back({.mesh = *plane, .material = ground});
        world.meshes.push_back({.mesh = *sphere, .material = rough, .transform = at(-1.2f, 0.5f, 0.0f)});
        world.meshes.push_back({.mesh = *sphere, .material = metal, .transform = at(0.0f, 0.5f, -0.5f)});
        world.meshes.push_back({.mesh = *cube, .material = textured, .transform = at(1.3f, 0.5f, 0.0f)});
        world.meshes.push_back({.mesh = *cube, .material = glass, .transform = at(0.2f, 0.5f, 1.2f)});
        // Soft particles that fade into the ground, additive ones stretched along their motion, and a lit ribbon.
        for (int index = 0; index < 6; ++index)
        {
            const float offset = static_cast<float>(index);
            world.particles.push_back({.position = {-1.8f + offset * 0.3f, 0.1f + offset * 0.05f, 1.5f}, .size = 0.6f,
                                       .color = {0.9f, 0.9f, 1.0f, 0.6f}, .rotation = offset});
        }
        world.particleDraws.push_back({.first = 0, .count = 6, .softness = 0.4f, .center = {-1.0f, 0.2f, 1.5f}});
        for (int index = 0; index < 4; ++index)
        {
            const float offset = static_cast<float>(index);
            world.particles.push_back({.position = {0.8f + offset * 0.2f, 1.4f, 0.5f}, .size = 0.15f, .color = {1.0f, 0.6f, 0.2f, 1.0f},
                                       .stretch = {0.1f, 0.3f, 0.0f}});
        }
        world.particleDraws.push_back({.first = 6, .count = 4, .blend = devex::render::ParticleBlend::Additive,
                                       .facing = devex::render::ParticleFacing::Stretched, .center = {1.1f, 1.4f, 0.5f}});
        for (int index = 0; index < 4; ++index)
        {
            const float offset = static_cast<float>(index);
            world.trailPoints.push_back({.position = {-1.5f + offset * 0.5f, 1.8f + offset * 0.1f, 0.0f}, .width = 0.2f,
                                         .color = {0.3f, 1.0f, 0.5f, 0.8f}, .direction = {1.0f, 0.2f, 0.0f}, .u = offset / 3.0f});
        }
        world.trailSegments = {0, 1, 2};
        world.particleDraws.push_back({.ribbons = true, .first = 0, .count = 3, .lit = true, .center = {-0.75f, 1.95f, 0.0f}});
        // The skinned box, moved by its bone.
        world.boneMatrices.push_back(at(-0.6f, 0.3f, 1.4f));
        world.meshes.push_back({.mesh = *skinned, .material = rough, .firstBone = 0, .boneCount = 1});
        if (frame == 4)
        {
            static_cast<void>(renderer->requestWindowCapture());
        }
        const devex::core::Result<void> presented = renderer->endFrame();
        if (!presented)
        {
            FAIL(std::format("frame {}: {}", frame, presented.error()));
        }
        for (devex::render::CapturedImage& image : renderer->takeCaptures())
        {
            captured.push_back(std::move(image));
        }
    }
    REQUIRE(captured.size() == 1);
    return std::move(captured.front());
}

// The window of a 2D scene under 2D lights drawn by one API: a point light and a directional one, which cast shadows
// from occluders, over a dark canvas, on a floor and on a sprite with a normal map.
[[nodiscard]] devex::render::CapturedImage drawLights2D(devex::render::RenderApi api)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .api = api});
    if (!renderer)
    {
        FAIL(std::format("{}", renderer.error()));
    }
    devex::asset::TextureData white{.format = devex::asset::TextureFormat::Rgba8Srgb};
    white.mips.push_back({.width = 2, .height = 2, .bytes = std::vector<std::byte>(16, std::byte{230})});
    // Normals tilted left, right, down and up, as a normal map stores them.
    devex::asset::TextureData normals{.format = devex::asset::TextureFormat::Rgba8Unorm, .filter = devex::asset::TextureFilter::Nearest};
    normals.mips.push_back({.width = 2,
                            .height = 2,
                            .bytes = {std::byte{40}, std::byte{128}, std::byte{230}, std::byte{255}, std::byte{215}, std::byte{128},
                                      std::byte{230}, std::byte{255}, std::byte{128}, std::byte{40}, std::byte{230}, std::byte{255},
                                      std::byte{128}, std::byte{215}, std::byte{230}, std::byte{255}}});
    const auto texture = renderer->createTexture(white);
    const auto normalMap = renderer->createTexture(normals);
    REQUIRE((texture && normalMap));

    std::vector<devex::render::CapturedImage> captured;
    for (int frame = 0; frame < 10 && captured.empty(); ++frame)
    {
        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::inverse(at(0.0f, 0.0f, 10.0f));
        world.camera.projection = devex::render::Projection::Orthographic;
        world.camera.orthographicSize = 3.0f;
        world.camera.farPlane = 100.0f;
        world.camera.autoExposure = false;
        world.camera.antialiasing = devex::render::Antialiasing::None;
        world.camera.tonemapper = devex::render::Tonemapper::None;
        world.camera.bloom = 0.0f;
        world.environment.color = {0.0f, 0.0f, 0.0f};
        world.canvasModulate = {0.25f, 0.25f, 0.3f};
        world.sprites.push_back({.transform = at(0.0f, 0.0f, 0.0f), .size = {8.0f, 6.0f}, .naturalSize = {8.0f, 6.0f}, .texture = *texture});
        world.sprites.push_back({.transform = at(1.5f, 0.5f, 0.1f), .size = {1.5f, 1.5f}, .naturalSize = {1.5f, 1.5f},
                                 .texture = *texture, .normalTexture = *normalMap, .layer = 1});
        world.lights2D.push_back({.position = {-1.0f, 0.5f}, .color = {1.0f, 0.7f, 0.4f}, .radius = 4.0f, .falloff = 1.5f,
                                  .height = 0.4f, .shadows = true});
        world.lights2D.push_back({.directional = true, .direction = {0.5f, -1.0f}, .color = {0.2f, 0.3f, 0.6f}, .height = 0.3f,
                                  .shadows = true});
        world.occluders2D.push_back({.from = {-0.5f, -0.5f}, .to = {0.0f, 1.0f}});
        world.occluders2D.push_back({.from = {-2.5f, -1.5f}, .to = {-1.5f, -1.8f}});
        if (frame == 3)
        {
            static_cast<void>(renderer->requestWindowCapture());
        }
        const devex::core::Result<void> presented = renderer->endFrame();
        if (!presented)
        {
            FAIL(std::format("frame {}: {}", frame, presented.error()));
        }
        for (devex::render::CapturedImage& image : renderer->takeCaptures())
        {
            captured.push_back(std::move(image));
        }
    }
    REQUIRE(captured.size() == 1);
    return std::move(captured.front());
}

// How far two images of the same scene are: the channels further apart than `tolerance`, and the mean difference.
struct Difference
{
    std::size_t channels = 0;
    double mean = 0.0;
};

[[nodiscard]] Difference compare(const devex::render::CapturedImage& first, const devex::render::CapturedImage& second,
                                 int tolerance)
{
    Difference difference;
    std::size_t total = 0;
    for (std::size_t at = 0; at < first.rgba.size(); ++at)
    {
        const int apart = std::abs(static_cast<int>(first.rgba[at]) - static_cast<int>(second.rgba[at]));
        total += static_cast<std::size_t>(apart);
        if (apart > tolerance)
        {
            ++difference.channels;
        }
    }
    difference.mean = static_cast<double>(total) / static_cast<double>(std::max<std::size_t>(first.rgba.size(), 1));
    return difference;
}
} // namespace

TEST_CASE("WebGPU draws a 2D scene and its interface as Vulkan does", "[render][gpu][webgpu]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawScene(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage webgpu = drawScene(devex::render::RenderApi::WebGpu);
    REQUIRE(webgpu.width == vulkan.width);
    REQUIRE(webgpu.height == vulkan.height);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
    // The same pixels, but for a few along the edges, where the two may round a coverage differently.
    std::size_t different = 0;
    std::size_t total = 0;
    for (std::size_t at = 0; at < vulkan.rgba.size(); ++at)
    {
        const int difference = std::abs(static_cast<int>(vulkan.rgba[at]) - static_cast<int>(webgpu.rgba[at]));
        total += static_cast<std::size_t>(difference);
        if (difference > 6)
        {
            ++different;
        }
    }
    INFO(std::format("{} channels differ, {:.2f} on average", different, static_cast<double>(total) / vulkan.rgba.size()));
    CHECK(different < vulkan.rgba.size() / 200);
    CHECK(static_cast<double>(total) / vulkan.rgba.size() < 1.0);
    // The environment shows through in a corner, exposed and encoded as the display shows it.
    const std::size_t corner = (std::size_t{230} * vulkan.width + 310) * 4;
    CHECK(webgpu.rgba[corner + 2] > webgpu.rgba[corner]);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("WebGPU draws lit meshes, their shadows, the fog and particles as Vulkan does", "[render][gpu][webgpu]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage webgpu = drawMeshes(devex::render::RenderApi::WebGpu);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
    // The same pixels but for a few along the edges, where the two may round a coverage differently.
    const Difference difference = compare(vulkan, webgpu, 8);
    INFO(std::format("{} channels differ, {:.2f} on average", difference.channels, difference.mean));
    CHECK(difference.channels < vulkan.rgba.size() / 100);
    CHECK(difference.mean < 1.5);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("WebGPU lights sprites with the 2D lights and their shadows as Vulkan does", "[render][gpu][webgpu]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawLights2D(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage webgpu = drawLights2D(devex::render::RenderApi::WebGpu);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
    const Difference difference = compare(vulkan, webgpu, 6);
    INFO(std::format("{} channels differ, {:.2f} on average", difference.channels, difference.mean));
    CHECK(difference.channels < vulkan.rgba.size() / 200);
    CHECK(difference.mean < 1.0);
    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Blocks of BC7 and BC5 decode as basisu decodes them", "[render][webgpu][texture][noweb]")
{
    std::mt19937 random(7);
    std::uniform_int_distribution<int> byte(0, 255);
    for (const devex::asset::TextureFormat format : {devex::asset::TextureFormat::Bc7Unorm, devex::asset::TextureFormat::Bc5Unorm})
    {
        // Levels of 64 by 64 pixels of random blocks: every mode, partition and rotation of BC7 among them.
        for (int level = 0; level < 40; ++level)
        {
            devex::asset::TextureData texture{.format = format};
            texture.mips.push_back({.width = 64, .height = 64});
            std::vector<std::byte>& bytes = texture.mips.back().bytes;
            bytes.resize(std::size_t{16} * 16 * 16);
            for (std::size_t at = 0; at < bytes.size(); ++at)
            {
                bytes[at] = static_cast<std::byte>(byte(random));
                // The reserved mode, whose first byte is 0, decodes as nothing at all.
                if (format == devex::asset::TextureFormat::Bc7Unorm && at % 16 == 0 && bytes[at] == std::byte{0})
                {
                    bytes[at] = std::byte{1};
                }
            }
            const auto expected = devex::asset::decodeTextureLevel(texture, 0);
            if (!expected)
            {
                SKIP("this build has no basisu to compare with");
            }
            const std::vector<std::byte> decoded = devex::render::webgpu::decodeBlocks(format, bytes, 64, 64);
            REQUIRE(decoded.size() == expected->rgba.size());
            std::size_t different = 0;
            for (std::size_t at = 0; at < decoded.size(); ++at)
            {
                const int difference = std::abs(std::to_integer<int>(decoded[at]) - static_cast<int>(expected->rgba[at]));
                // BC5 interpolates its channels in floats in basisu: a step of rounding apart.
                if (difference > (format == devex::asset::TextureFormat::Bc5Unorm ? 1 : 0))
                {
                    ++different;
                }
            }
            INFO(std::format("{} level {}", devex::asset::toString(format), level));
            REQUIRE(different == 0);
        }
    }
}
