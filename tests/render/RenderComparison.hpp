#pragma once

// What the tests of the renderers share to compare the images of two APIs: the errors they log, a 2D scene with its
// interface drawn by one API, and how far two images are.
#include <devex/asset/Primitives.hpp>
#include <devex/asset/TextureData.hpp>
#include <devex/asset/import/ShaderFile.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devex::test {

// Collects error messages, those of WebGPU, of OpenGL and of the Vulkan validation layer among them.
class ErrorCapture
{
public:
    ErrorCapture()
        : m_sink(devex::core::addLogSink([this](const devex::core::LogRecord& record) {
            if (record.level >= devex::core::LogLevel::Error)
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

[[nodiscard]] inline devex::math::Mat4 at(float x, float y, float z)
{
    return devex::math::translate(devex::math::Mat4(1.0f), devex::math::Vec3{x, y, z});
}

// The window, with its interface, of a 2D scene drawn by one API: sprites simple, sliced, tiled, flipped, tinted and
// additive over the colour of the environment, then a rectangle and a rounded one of the interface. OpenGL draws with
// OpenGL ES 3.0 when `es` asks for it, where the driver of the computer has it.
[[nodiscard]] inline devex::render::CapturedImage drawScene(devex::render::RenderApi api, bool es = false)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    const bool openGl = api == devex::render::RenderApi::OpenGl;
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = !openGl, .openGl = openGl, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .api = api, .openGlEs = es});
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
        // At 60 pixels a unit, every edge falls between two pixels: one on the centre of a row, Vulkan and OpenGL may
        // each give to their side, as Mesa does.
        world.sprites.push_back({.transform = at(-2.0f, 1.0f, 0.0f), .size = {1.5f, 0.8f}, .naturalSize = {0.5f, 0.5f},
                                 .border = {0.1f, 0.1f, 0.1f, 0.1f}, .texture = *texture,
                                 .mode = devex::render::SpriteMode::Sliced, .flipX = true});
        world.sprites.push_back({.transform = at(2.0f, -1.0f, 0.0f), .size = {1.5f, 0.8f}, .naturalSize = {0.25f, 0.25f},
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


// How far two images of the same scene are: the channels further apart than `tolerance`, and the mean difference.
struct Difference
{
    std::size_t channels = 0;
    double mean = 0.0;
};

[[nodiscard]] inline Difference compare(const devex::render::CapturedImage& first, const devex::render::CapturedImage& second,
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

// A shader of the project, compiled for Vulkan, for WebGPU and for OpenGL.
[[nodiscard]] inline devex::asset::ShaderData compiledForEveryApi(std::string_view text, std::string_view name)
{
    devex::asset::ShaderCompiler compiler = devex::asset::defaultShaderCompiler();
    compiler.web = true;
    compiler.openGl = devex::asset::translatesGlsl();
    devex::asset::ShaderData shader = devex::asset::compileShader(text, std::string(name), compiler);
    for (const devex::asset::ShaderDiagnostic& diagnostic : shader.diagnostics)
    {
        UNSCOPED_INFO(diagnostic.message);
    }
    REQUIRE(shader.compiled());
    REQUIRE_FALSE(shader.wgsl.empty());
    REQUIRE(shader.glsl.empty() != devex::asset::translatesGlsl());
    return shader;
}

// The window of meshes drawn by shaders of the project, by one API: a lit one reading a texture and a uniform with its
// own light(), one bent by vertex() and unshaded, and a cut-out that casts its shadow on the ground.
[[nodiscard]] inline devex::render::CapturedImage drawShaded(devex::render::RenderApi api, const std::array<devex::asset::ShaderData, 3>& shaders,
                                                       bool es = false)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    const bool openGl = api == devex::render::RenderApi::OpenGl;
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = !openGl, .openGl = openGl, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .meshLodError = 0.0f, .api = api, .openGlEs = es});
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
    std::array<devex::render::ShaderHandle, 3> handles;
    for (std::size_t index = 0; index < shaders.size(); ++index)
    {
        const auto created = renderer->createShader(shaders[index]);
        REQUIRE(created.has_value());
        handles[index] = *created;
    }
    const devex::render::MaterialHandle lit = renderer->createMaterial(
        {.shader = handles[0], .parameters = {{1.0f, 0.6f, 0.3f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}}, .textures = {{.slot = 1, .texture = *texture}}});
    const devex::render::MaterialHandle bent = renderer->createMaterial({.shader = handles[1], .parameters = {{0.2f, 0.8f, 0.4f, 0.0f}}});
    const devex::render::MaterialHandle cutout =
        renderer->createMaterial({.shader = handles[2], .parameters = {{0.0f, 0.0f, 0.0f, 0.0f}}, .textures = {{.slot = 0, .texture = *texture}}});
    const devex::render::MaterialHandle ground = renderer->createMaterial({.baseColorFactor = {0.4f, 0.45f, 0.4f, 1.0f}});
    const auto sphere = renderer->createMesh(devex::asset::makeUvSphere(0.6f));
    const auto cube = renderer->createMesh(devex::asset::makeCube());
    const auto plane = renderer->createMesh(devex::asset::makePlane(8.0f));
    REQUIRE((sphere && cube && plane));

    std::vector<devex::render::CapturedImage> captured;
    for (int frame = 0; frame < 12 && captured.empty(); ++frame)
    {
        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::lookAt(devex::math::Vec3{0.0f, 2.0f, 5.0f}, devex::math::Vec3{0.0f, 0.4f, 0.0f},
                                                devex::math::Vec3{0.0f, 1.0f, 0.0f});
        world.camera.autoExposure = false;
        world.camera.ev100 = 0.0f;
        world.camera.antialiasing = devex::render::Antialiasing::None;
        world.camera.bloom = 0.0f;
        world.camera.ambientOcclusion = 0.0f;
        world.camera.screenReflections = 0.0f;
        world.environment.color = {0.8f, 0.9f, 1.0f};
        world.environment.intensity = 0.8f;
        world.sun.direction = {-0.4f, -1.0f, -0.5f};
        world.sun.illuminance = {2.5f, 2.4f, 2.2f};
        world.sun.castShadows = true;
        world.meshes.push_back({.mesh = *plane, .material = ground});
        world.meshes.push_back({.mesh = *cube, .material = lit, .transform = at(-1.3f, 0.5f, 0.0f)});
        world.meshes.push_back({.mesh = *sphere, .material = bent, .transform = at(0.0f, 0.7f, -0.4f)});
        world.meshes.push_back({.mesh = *cube, .material = cutout, .transform = at(1.3f, 0.8f, 0.3f)});
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

// The window of sprites drawn by one API under a 2D light and the sun: one with a canvas_item shader of the project,
// bent by vertex(), reading its texture, a uniform and an instance uniform, and turning its normal map; the same lit by
// the sun; and a lit sprite of the standard shader beside a plain one.
[[nodiscard]] inline devex::render::CapturedImage drawShadedSprites(devex::render::RenderApi api, const devex::asset::ShaderData& shader, bool es = false)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    const bool openGl = api == devex::render::RenderApi::OpenGl;
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = !openGl, .openGl = openGl, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .api = api, .openGlEs = es});
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
    const auto created = renderer->createShader(shader);
    REQUIRE(created.has_value());
    const devex::render::MaterialHandle shaded =
        renderer->createMaterial({.shader = *created, .parameters = {{1.0f, 0.5f, 0.2f, 0.0f}, {0.5f, 0.0f, 0.0f, 0.0f}}});

    std::vector<devex::render::CapturedImage> captured;
    for (int frame = 0; frame < 10 && captured.empty(); ++frame)
    {
        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::inverse(at(0.0f, 0.0f, 10.0f));
        world.camera.projection = devex::render::Projection::Orthographic;
        world.camera.orthographicSize = 3.0f;
        world.camera.farPlane = 100.0f;
        world.camera.autoExposure = false;
        world.camera.ev100 = 0.0f;
        world.camera.antialiasing = devex::render::Antialiasing::None;
        world.camera.bloom = 0.0f;
        world.environment.color = {0.3f, 0.35f, 0.4f};
        world.environment.intensity = 0.5f;
        world.sun.direction = {-0.3f, -0.4f, -1.0f};
        world.sun.illuminance = {2.0f, 1.9f, 1.7f};
        world.canvasModulate = {0.6f, 0.6f, 0.7f};
        world.lights2D.push_back({.position = {-1.5f, 0.5f}, .color = {1.0f, 0.8f, 0.5f}, .radius = 5.0f, .falloff = 1.0f, .height = 0.5f});
        // The second sprite of the shader gives its instance uniform a value of its own.
        world.instanceParameters.push_back({0.9f, 0.0f, 0.0f, 0.0f});
        world.sprites.push_back({.transform = at(-1.8f, 0.8f, 0.0f), .size = {1.6f, 1.6f}, .naturalSize = {1.6f, 1.6f},
                                 .texture = *texture, .material = shaded});
        world.sprites.push_back({.transform = at(0.2f, 0.8f, 0.0f), .size = {1.6f, 1.6f}, .naturalSize = {1.6f, 1.6f},
                                 .texture = *texture, .lit = true, .material = shaded, .instanceParameters = 0});
        world.sprites.push_back({.transform = at(-1.0f, -1.2f, 0.0f), .size = {1.6f, 1.2f}, .naturalSize = {1.6f, 1.2f},
                                 .texture = *texture, .lit = true});
        world.sprites.push_back({.transform = at(1.6f, -1.2f, 0.0f), .size = {1.2f, 1.2f}, .color = {0.4f, 0.8f, 0.5f, 0.8f}});
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

// The window of a sky shader of the project drawn by one API, which lights a sphere and a cube through the environment
// it bakes, with particles of a particles shader of the project beside a ribbon of the standard one.
[[nodiscard]] inline devex::render::CapturedImage drawShadedSky(devex::render::RenderApi api, const devex::asset::ShaderData& sky,
                                                         const devex::asset::ShaderData& particles, bool es = false)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    const bool openGl = api == devex::render::RenderApi::OpenGl;
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = !openGl, .openGl = openGl, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .meshLodError = 0.0f, .api = api, .openGlEs = es});
    if (!renderer)
    {
        FAIL(std::format("{}", renderer.error()));
    }
    const auto skyShader = renderer->createShader(sky);
    const auto particlesShader = renderer->createShader(particles);
    REQUIRE((skyShader && particlesShader));
    const devex::render::MaterialHandle skyMaterial =
        renderer->createMaterial({.shader = *skyShader, .parameters = {{0.2f, 0.4f, 0.9f, 0.0f}, {0.8f, 0.6f, 0.4f, 0.0f}}});
    const devex::render::MaterialHandle embers =
        renderer->createMaterial({.shader = *particlesShader, .parameters = {{1.0f, 0.6f, 0.2f, 0.0f}}});
    const devex::render::MaterialHandle rough = renderer->createMaterial({.baseColorFactor = {0.9f, 0.9f, 0.9f, 1.0f}, .roughnessFactor = 0.6f});
    const devex::render::MaterialHandle metal =
        renderer->createMaterial({.baseColorFactor = {0.9f, 0.8f, 0.5f, 1.0f}, .metallicFactor = 1.0f, .roughnessFactor = 0.3f});
    const auto sphere = renderer->createMesh(devex::asset::makeUvSphere(0.6f));
    const auto cube = renderer->createMesh(devex::asset::makeCube());
    REQUIRE((sphere && cube));

    std::vector<devex::render::CapturedImage> captured;
    for (int frame = 0; frame < 12 && captured.empty(); ++frame)
    {
        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::lookAt(devex::math::Vec3{0.0f, 1.0f, 4.0f}, devex::math::Vec3{0.0f, 0.6f, 0.0f},
                                                devex::math::Vec3{0.0f, 1.0f, 0.0f});
        world.camera.autoExposure = false;
        world.camera.ev100 = 0.0f;
        world.camera.antialiasing = devex::render::Antialiasing::None;
        world.camera.bloom = 0.0f;
        world.camera.ambientOcclusion = 0.0f;
        world.camera.screenReflections = 0.0f;
        world.environment.skyMaterial = skyMaterial;
        world.environment.color = {1.0f, 1.0f, 1.0f};
        world.environment.intensity = 1.0f;
        world.environment.rotation = 0.4f;
        world.sun.direction = {-0.4f, -0.6f, -0.5f};
        world.sun.illuminance = {1.5f, 1.4f, 1.3f};
        world.meshes.push_back({.mesh = *sphere, .material = metal, .transform = at(-0.8f, 0.6f, 0.0f)});
        world.meshes.push_back({.mesh = *cube, .material = rough, .transform = at(0.9f, 0.4f, -0.3f)});
        for (int index = 0; index < 6; ++index)
        {
            const float offset = static_cast<float>(index);
            world.particles.push_back({.position = {-0.9f + offset * 0.35f, 1.6f, 0.8f}, .size = 0.3f, .color = {0.9f, 0.9f, 1.0f, 0.9f},
                                       .rotation = offset});
        }
        world.particleDraws.push_back({.first = 0, .count = 6, .center = {0.0f, 1.6f, 0.8f}, .material = embers});
        for (int index = 0; index < 4; ++index)
        {
            const float offset = static_cast<float>(index);
            world.trailPoints.push_back({.position = {-1.2f + offset * 0.8f, 0.2f + offset * 0.1f, 0.6f}, .width = 0.15f,
                                         .color = {0.3f, 1.0f, 0.5f, 0.8f}, .direction = {1.0f, 0.1f, 0.0f}, .u = offset / 3.0f});
        }
        world.trailSegments = {0, 1, 2};
        // A ribbon is drawn by the standard shader, even with a material of particles shader.
        world.particleDraws.push_back({.ribbons = true, .first = 0, .count = 3, .center = {0.0f, 0.35f, 0.6f}, .material = embers});
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
// from occluders, over a dark canvas, on a floor, on a sprite with a normal map, and on a lit sprite that a local light
// shines on. OpenGL draws with OpenGL ES 3.0 when `es` asks for it, where the driver of the computer has it.
[[nodiscard]] inline devex::render::CapturedImage drawLights2D(devex::render::RenderApi api, bool es = false)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    const bool openGl = api == devex::render::RenderApi::OpenGl;
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = !openGl, .openGl = openGl, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .api = api, .openGlEs = es});
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
        // A lit sprite, which the local light shines on as on a matte surface.
        world.sprites.push_back({.transform = at(-2.2f, 1.6f, 0.1f), .size = {1.2f, 1.2f}, .naturalSize = {1.2f, 1.2f},
                                 .texture = *texture, .lit = true, .layer = 1});
        world.lights.push_back({.position = {-2.0f, 1.4f, 1.0f}, .intensity = {2.0f, 1.5f, 1.0f}, .range = 4.0f});
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

// The window of a 3D scene drawn by one API: a rough sphere and a metallic one, a textured box on a plane and a
// skinned box, lit by the sun, a point light and a spot, which cast shadows, and the environment baked from a sky
// texture, in a height fog, with a decal, unless `decal` leaves it out, and particles and a ribbon, unless `particles`
// does. OpenGL draws with OpenGL ES 3.0 when `es` asks for it, where the driver of the computer has it.
[[nodiscard]] inline devex::render::CapturedImage drawMeshes(devex::render::RenderApi api, bool temporalAndBloom = false, bool lens = false,
                                                            float renderScale = 1.0f, bool decal = true, bool particles = true,
                                                            bool es = false)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    const bool openGl = api == devex::render::RenderApi::OpenGl;
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = !openGl, .openGl = openGl, .hidden = true});
    REQUIRE(window.has_value());
    // The geometry as it is given, and every instance drawn as the CPU sends it.
    auto renderer =
        devex::render::Renderer::create(*platform, *window, {.validation = true, .meshLodError = 0.0f, .api = api, .openGlEs = es});
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
    // A table of colours of 8 steps, as a strip of 8 squares, one per step of blue: a little warmer than the image.
    devex::asset::TextureData tablePixels{.format = devex::asset::TextureFormat::Rgba8Unorm};
    tablePixels.mips.push_back({.width = 64, .height = 8});
    for (std::uint32_t y = 0; y < 8; ++y)
    {
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            const float red = static_cast<float>(x % 8) / 7.0f;
            const float green = static_cast<float>(y) / 7.0f;
            const float blue = static_cast<float>(x / 8) / 7.0f;
            for (const float channel : {std::min(red * 1.1f, 1.0f), green * 0.95f, blue * 0.8f, 1.0f})
            {
                tablePixels.mips.back().bytes.push_back(std::byte{static_cast<std::uint8_t>(std::lround(channel * 255.0f))});
            }
        }
    }
    const auto colorTable = renderer->createTexture(tablePixels);
    REQUIRE(colorTable.has_value());
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
        // The antialiasing mixes the frames from the first, and the bloom halos what is brighter than its threshold.
        world.camera.antialiasing = temporalAndBloom ? devex::render::Antialiasing::Temporal : devex::render::Antialiasing::None;
        world.camera.bloom = temporalAndBloom ? 0.6f : 0.0f;
        world.camera.bloomThreshold = 0.6f;
        world.renderScale = renderScale;
        world.camera.ambientOcclusion = 0.0f;
        world.camera.screenReflections = 0.0f;
        if (lens)
        {
            // The camera slides along, which the motion blur follows, focused on the metal sphere, its lens pulling the
            // colours apart at the edges, and the image through the table of colours.
            const float along = 0.1f * static_cast<float>(frame);
            world.camera.view = devex::math::lookAt(devex::math::Vec3{along, 2.0f, 5.0f}, devex::math::Vec3{along, 0.3f, 0.0f},
                                                    devex::math::Vec3{0.0f, 1.0f, 0.0f});
            world.camera.depthOfField = 1.0f;
            world.camera.aperture = 1.4f;
            world.camera.focusDistance = 5.5f;
            world.camera.motionBlur = 0.5f;
            world.camera.chromaticAberration = 0.004f;
            world.camera.colorTable = *colorTable;
        }
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
        if (decal)
        {
            // A decal on the ground, the sky as its albedo, faded at the edges of its box.
            world.decals.push_back({.transform = at(0.6f, 0.0f, 0.8f), .size = {1.6f, 1.0f, 1.2f}, .albedo = *skyTexture,
                                    .modulate = {1.0f, 0.9f, 0.8f, 0.9f}});
        }
        world.meshes.push_back({.mesh = *sphere, .material = rough, .transform = at(-1.2f, 0.5f, 0.0f)});
        world.meshes.push_back({.mesh = *sphere, .material = metal, .transform = at(0.0f, 0.5f, -0.5f)});
        world.meshes.push_back({.mesh = *cube, .material = textured, .transform = at(1.3f, 0.5f, 0.0f)});
        world.meshes.push_back({.mesh = *cube, .material = glass, .transform = at(0.2f, 0.5f, 1.2f)});
        if (particles)
        {
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
        }
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
    // The scene in fewer pixels than the window, which the capture shows whole.
    CHECK(renderer->stats().sceneExtent == devex::render::scaledExtent({320, 240}, renderScale));
    REQUIRE(captured.size() == 1);
    return std::move(captured.front());
}

// The window of a terrain drawn by one API: hills of 33 samples on a side, two layers painted left and right, holes of
// none, grass painted in the middle without wind, under a sun that casts the shadows of the hills and of a sphere.
[[nodiscard]] inline devex::render::CapturedImage drawTerrain(devex::render::RenderApi api, bool es = false)
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    const bool openGl = api == devex::render::RenderApi::OpenGl;
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = !openGl, .openGl = openGl, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true, .meshLodError = 0.0f, .api = api, .openGlEs = es});
    if (!renderer)
    {
        FAIL(std::format("{}", renderer.error()));
    }
    constexpr std::uint32_t resolution = 33;
    constexpr float size = 16.0f;
    const auto heightOf = [](std::uint32_t x, std::uint32_t z) {
        return 0.8f * std::sin(static_cast<float>(x) * 0.35f) * std::cos(static_cast<float>(z) * 0.28f) + 0.8f;
    };
    const auto map = [&](devex::asset::TextureFormat format, devex::asset::TextureFilter filter, auto&& texel) {
        devex::asset::TextureData data{.format = format, .filter = filter};
        data.mips.push_back({.width = resolution, .height = resolution});
        for (std::uint32_t z = 0; z < resolution; ++z)
        {
            for (std::uint32_t x = 0; x < resolution; ++x)
            {
                texel(data.mips.back().bytes, x, z);
            }
        }
        const auto created = renderer->createTexture(data);
        REQUIRE(created.has_value());
        return *created;
    };
    const auto put = [](std::vector<std::byte>& bytes, const auto& value) {
        const auto* const raw = reinterpret_cast<const std::byte*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(value));
    };
    devex::render::TerrainTextures textures;
    textures.heights = map(devex::asset::TextureFormat::R32Float, devex::asset::TextureFilter::Nearest,
                           [&](std::vector<std::byte>& bytes, std::uint32_t x, std::uint32_t z) { put(bytes, heightOf(x, z)); });
    textures.slopes = map(devex::asset::TextureFormat::Rgba16Float, devex::asset::TextureFilter::Linear,
                          [&](std::vector<std::byte>& bytes, std::uint32_t x, std::uint32_t z) {
                              const std::uint32_t left = x > 0 ? x - 1 : x;
                              const std::uint32_t right = x + 1 < resolution ? x + 1 : x;
                              const std::uint32_t up = z > 0 ? z - 1 : z;
                              const std::uint32_t down = z + 1 < resolution ? z + 1 : z;
                              const float slopeX = (heightOf(right, z) - heightOf(left, z)) / static_cast<float>(right - left);
                              const float slopeZ = (heightOf(x, down) - heightOf(x, up)) / static_cast<float>(down - up);
                              for (const float value : {slopeX, slopeZ, 0.0f, 1.0f})
                              {
                                  put(bytes, static_cast<std::uint16_t>(glm::packHalf1x16(value)));
                              }
                          });
    textures.paint0 = map(devex::asset::TextureFormat::Rgba8Unorm, devex::asset::TextureFilter::Linear,
                          [&](std::vector<std::byte>& bytes, std::uint32_t x, std::uint32_t) {
                              const auto right = static_cast<std::uint8_t>(std::clamp((static_cast<int>(x) - 12) * 32, 0, 255));
                              for (const std::uint8_t value : {static_cast<std::uint8_t>(255 - right), right, std::uint8_t{0}, std::uint8_t{0}})
                              {
                                  bytes.push_back(std::byte{value});
                              }
                          });
    textures.paint1 = map(devex::asset::TextureFormat::Rgba8Unorm, devex::asset::TextureFilter::Linear,
                          [](std::vector<std::byte>& bytes, std::uint32_t, std::uint32_t) { bytes.insert(bytes.end(), 4, std::byte{0}); });
    textures.detail = map(devex::asset::TextureFormat::Rgba8Unorm, devex::asset::TextureFilter::Linear,
                          [](std::vector<std::byte>& bytes, std::uint32_t x, std::uint32_t z) {
                              const bool grass = x > 10 && x < 22 && z > 14 && z < 26;
                              for (const std::uint8_t value : {std::uint8_t{0}, grass ? std::uint8_t{255} : std::uint8_t{0}, std::uint8_t{0}, std::uint8_t{255}})
                              {
                                  bytes.push_back(std::byte{value});
                              }
                          });
    devex::asset::TextureData checker{.format = devex::asset::TextureFormat::Rgba8Srgb};
    checker.mips.push_back({.width = 2, .height = 2,
                            .bytes = {std::byte{200}, std::byte{180}, std::byte{120}, std::byte{255}, std::byte{90}, std::byte{120}, std::byte{60},
                                      std::byte{255}, std::byte{90}, std::byte{120}, std::byte{60}, std::byte{255}, std::byte{200}, std::byte{180},
                                      std::byte{120}, std::byte{255}}});
    const auto checkerTexture = renderer->createTexture(checker);
    REQUIRE(checkerTexture.has_value());
    const devex::render::MaterialHandle sand = renderer->createMaterial({.baseColorTexture = *checkerTexture, .roughnessFactor = 0.8f});
    const devex::render::MaterialHandle rock = renderer->createMaterial({.baseColorFactor = {0.45f, 0.45f, 0.5f, 1.0f}, .roughnessFactor = 0.6f});
    const auto sphere = renderer->createMesh(devex::asset::makeUvSphere(0.6f));
    REQUIRE(sphere.has_value());

    std::vector<devex::render::CapturedImage> captured;
    for (int frame = 0; frame < 12 && captured.empty(); ++frame)
    {
        devex::render::RenderWorld& world = renderer->beginFrame();
        world.camera.view = devex::math::lookAt(devex::math::Vec3{0.0f, 7.0f, 13.0f}, devex::math::Vec3{0.0f, 0.5f, 0.0f},
                                                devex::math::Vec3{0.0f, 1.0f, 0.0f});
        world.camera.autoExposure = false;
        world.camera.ev100 = 0.0f;
        world.camera.antialiasing = devex::render::Antialiasing::None;
        world.camera.bloom = 0.0f;
        world.camera.ambientOcclusion = 0.0f;
        world.camera.screenReflections = 0.0f;
        world.environment.color = {0.8f, 0.9f, 1.0f};
        world.environment.intensity = 0.6f;
        world.sun.direction = {-0.5f, -1.0f, -0.4f};
        world.sun.illuminance = {2.5f, 2.4f, 2.2f};
        world.sun.castShadows = true;
        world.terrains.push_back({.textures = textures,
                                  .resolution = resolution,
                                  .origin = {-size * 0.5f, 0.0f, -size * 0.5f},
                                  .size = size,
                                  .blockHeights = {{0.0f, 1.6f}},
                                  .blockGrass = {255},
                                  .layers = {sand, rock},
                                  .layerCount = 2,
                                  .textureSize = 2.0f,
                                  .grassDensity = 6.0f,
                                  .grassDistance = 25.0f,
                                  .windStrength = 0.0f});
        world.meshes.push_back({.mesh = *sphere, .material = rock, .transform = at(1.5f, 2.2f, 1.0f)});
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

} // namespace devex::test
