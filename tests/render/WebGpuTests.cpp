// The WebGPU renderer, on a computer with Dawn: it draws a 2D scene as Vulkan does, and decodes the blocks of BC
// textures for a device without them as basisu does.
#include "render/webgpu/BlockDecoding.hpp"

#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
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
