#pragma once

// What the tests of the renderers share to compare the images of two APIs: the errors they log, a 2D scene with its
// interface drawn by one API, and how far two images are.
#include <devex/asset/TextureData.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
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

} // namespace devex::test
