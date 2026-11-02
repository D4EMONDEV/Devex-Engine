#include <devex/asset/Primitives.hpp>
#include <devex/asset/TextureData.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

namespace
{
using namespace devex;

enum class Comparison
{
    AmbientOcclusion,
    Exposure,
    ResumedView,
    Sky,
};

constexpr math::Extent2D extent{128, 96};

asset::TextureData checker()
{
    asset::TextureData texture{.format = asset::TextureFormat::Rgba8Unorm,
                               .filter = asset::TextureFilter::Nearest};
    asset::TextureMip mip{.width = 128, .height = 64};
    mip.bytes.resize(128 * 64 * 4);
    for (std::size_t y = 0; y < 64; ++y)
        for (std::size_t x = 0; x < 128; ++x)
        {
            const bool bright = ((x / 3 + y / 3) % 2) != 0;
            const auto offset = (y * 128 + x) * 4;
            mip.bytes[offset] = std::byte{static_cast<unsigned char>(bright ? 220 : 30)};
            mip.bytes[offset + 1] = std::byte{static_cast<unsigned char>(bright ? 80 : 180)};
            mip.bytes[offset + 2] = std::byte{static_cast<unsigned char>(bright ? 30 : 220)};
            mip.bytes[offset + 3] = std::byte{255};
        }
    texture.mips.push_back(std::move(mip));
    return texture;
}

math::Mat4 projection(const render::RenderCamera& camera)
{
    auto result = math::perspectiveReverseZ(camera.verticalFov, 4.0f / 3.0f, camera.nearPlane);
    result[1] = -result[1];
    return result;
}

float halton(unsigned index, unsigned base)
{
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0)
    {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

struct LogScope
{
    core::LogSinkId id;
    ~LogScope() { core::removeLogSink(id); }
};

// Both images are drawn in the same frame, with the same light and the same uploaded resources.
// The reference either has a fresh history or expresses the same frustum without TAA jitter.
std::array<render::CapturedImage, 2>
compare(Comparison comparison, bool occlusion = true,
        render::Projection skyProjection = render::Projection::Perspective)
{
    std::vector<std::string> errors;
    const LogScope sink{core::addLogSink(
        [&](const core::LogRecord& record)
        {
            if (record.level >= core::LogLevel::Error)
                errors.emplace_back(record.message);
        })};
    std::vector<render::CapturedImage> images;
    {
        auto platform = platform::Platform::create();
        REQUIRE(platform);
        auto window = platform->createWindow(
            {.width = extent.width, .height = extent.height, .vulkan = true, .hidden = true});
        REQUIRE(window);
        auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer);
        const auto cube = renderer->createMesh(asset::makeCube());
        REQUIRE(cube);
        const auto texture = renderer->createTexture(checker());
        REQUIRE(texture);
        const auto pattern = renderer->createMaterial({.baseColorFactor = {0.0f, 0.0f, 0.0f, 1.0f},
                                                       .emissiveFactor = math::Vec3{8000.0f},
                                                       .emissiveTexture = *texture});
        const auto matte = renderer->createMaterial({.baseColorFactor = {0.7f, 0.7f, 0.7f, 1.0f}});
        // Give the sky history time to forget the white fallback while the texture uploads.
        const unsigned capturedFrame = comparison == Comparison::Sky ? 67u : 23u;
        for (unsigned frame = 0; frame < capturedFrame + 5; ++frame)
        {
            platform->pollEvents([](const auto&) {});
            auto& world = renderer->beginFrame();
            world.camera.autoExposure = false;
            world.camera.antialiasing = render::Antialiasing::None;
            world.camera.ambientOcclusion = 0.0f;
            world.camera.bloom = 0.0f;
            world.environment.intensity = 8000.0f;
            render::RenderCamera actual = world.camera;
            actual.antialiasing = render::Antialiasing::Temporal;
            render::RenderCamera reference = actual;
            bool drawActual = true;
            bool drawReference = true;
            if (comparison == Comparison::AmbientOcclusion)
            {
                actual.view = math::lookAt(math::Vec3{2.0f, 1.2f, 3.0f}, math::Vec3{0.0f},
                                           math::Vec3{0.0f, 1.0f, 0.0f});
                actual.ambientOcclusion = occlusion ? 1.0f : 0.0f;
                actual.ambientOcclusionRadius = 0.3f;
                reference = actual;
                reference.antialiasing = render::Antialiasing::None;
                const unsigned sample = frame % 8 + 1;
                const math::Vec3 jitter{(halton(sample, 2) - 0.5f) * 2.0f / extent.width,
                                        (halton(sample, 3) - 0.5f) * 2.0f / extent.height, 0.0f};
                const auto p = projection(actual);
                reference.view =
                    math::inverse(p) * math::translate(math::Mat4{1.0f}, -jitter) * p * actual.view;
                // No accumulated colour: compare one AO evaluation in equivalent frusta.
                drawActual = drawReference = frame >= capturedFrame;
                world.meshes.push_back({.mesh = *cube, .material = matte});
                world.meshes.push_back(
                    {.mesh = *cube,
                     .material = matte,
                     .transform = math::translate(math::Mat4{1.0f}, {1.05f, -0.15f, -0.3f})});
                world.meshes.push_back({.mesh = *cube,
                                        .material = matte,
                                        .transform = math::scale(
                                            math::translate(math::Mat4{1.0f}, {0.0f, -0.75f, 0.0f}),
                                            {10.0f, 0.5f, 10.0f})});
            }
            else if (comparison == Comparison::Sky)
            {
                world.environment.sky = *texture;
                actual.projection = skyProjection;
                // An off-axis camera pans the background by exactly one pixel per frame.
                actual.view[2][0] =
                    static_cast<float>(frame) * 2.0f / (extent.width * projection(actual)[0][0]);
                reference = actual;
                reference.antialiasing = render::Antialiasing::None;
            }
            else
            {
                actual.projection = render::Projection::Orthographic;
                actual.orthographicSize = 1.0f;
                reference = actual;
                if (comparison == Comparison::Exposure)
                {
                    actual.ev100 = frame < capturedFrame ? 14.0f : 12.0f;
                    reference.ev100 = 12.0f;
                    drawReference = frame >= capturedFrame;
                }
                else
                {
                    drawActual = frame < capturedFrame - 3 || frame >= capturedFrame;
                    drawReference = frame >= capturedFrame;
                }
                const float x = comparison == Comparison::ResumedView && frame >= capturedFrame - 3
                                    ? 0.11f
                                    : 0.0f;
                world.meshes.push_back(
                    {.mesh = *cube,
                     .material = pattern,
                     .transform = math::scale(math::translate(math::Mat4{1.0f}, {x, 0.0f, -2.0f}),
                                              {3.0f, 2.0f, 1.0f}),
                     .objectId = 1});
            }
            if (drawActual)
                world.views.push_back({.id = 11, .camera = actual, .extent = extent});
            if (drawReference)
                world.views.push_back({.id = 12, .camera = reference, .extent = extent});
            if (frame == capturedFrame)
            {
                (void)renderer->requestViewCapture(11, extent.width, extent.height);
                (void)renderer->requestViewCapture(12, extent.width, extent.height);
            }
            REQUIRE(renderer->endFrame());
            auto captured = renderer->takeCaptures();
            std::ranges::move(captured, std::back_inserter(images));
        }
    }
    for (const auto& error : errors)
        UNSCOPED_INFO(error);
    CHECK(errors.empty());
    REQUIRE(images.size() == 2);
    std::ranges::sort(images, {}, &render::CapturedImage::request);
    REQUIRE(images[0].rgba.size() == extent.width * extent.height * 4);
    REQUIRE(images[1].rgba.size() == images[0].rgba.size());
    return {std::move(images[0]), std::move(images[1])};
}

struct Difference
{
    double mean = 0.0;
    std::size_t changed = 0;
};

Difference difference(const std::array<render::CapturedImage, 2>& images)
{
    Difference result;
    for (std::size_t i = 0; i < images[0].rgba.size(); ++i)
    {
        if (i % 4 == 3)
            continue;
        const int delta = std::abs(int(images[0].rgba[i]) - int(images[1].rgba[i]));
        result.mean += delta;
        result.changed += delta > 3 ? 1u : 0u;
    }
    result.mean /= static_cast<double>(extent.width * extent.height * 3);
    return result;
}
} // namespace

TEST_CASE("AO reads the same depth with temporal jitter as with an equivalent frustum",
          "[render][gpu][temporal]")
{
    const bool occlusion = GENERATE(false, true);
    const auto delta = difference(compare(Comparison::AmbientOcclusion, occlusion));
    INFO("AO " << occlusion << ": mean byte difference " << delta.mean
               << ", channels over 3: " << delta.changed);
    // Equivalent frusta agree up to the rounding of a few byte channels. The uncorrected AO
    // reconstructs and projects against the stable camera instead of the sampled depth ray.
    CHECK(delta.mean < 0.001);
    CHECK(delta.changed < 2);
}

TEST_CASE("Temporal history follows exposure without a delayed brightness change",
          "[render][gpu][temporal]")
{
    const auto delta = difference(compare(Comparison::Exposure));
    INFO("mean byte difference " << delta.mean << ", channels over 3: " << delta.changed);
    CHECK(delta.mean < 0.5);
    CHECK(delta.changed < 100);
}

TEST_CASE("A resumed view discards the history of its last visible frame",
          "[render][gpu][temporal]")
{
    const auto delta = difference(compare(Comparison::ResumedView));
    INFO("mean byte difference " << delta.mean << ", channels over 3: " << delta.changed);
    CHECK(delta.mean == 0.0);
}

TEST_CASE("Temporal sky history follows the camera direction", "[render][gpu][temporal]")
{
    const auto projection =
        GENERATE(render::Projection::Perspective, render::Projection::Orthographic);
    const auto delta = difference(compare(Comparison::Sky, true, projection));
    INFO("projection " << static_cast<int>(projection) << ": mean byte difference " << delta.mean
                       << ", channels over 3: " << delta.changed);
    CHECK(delta.mean < 0.5);
    CHECK(delta.changed < 100);
}
