#include <devex/asset/Primitives.hpp>
#include <devex/asset/import/ShaderFile.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
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

[[nodiscard]] devex::asset::ShaderData compiled(std::string_view text, std::string_view name)
{
    devex::asset::ShaderData shader = devex::asset::compileShader(text, std::string(name));
    INFO((shader.diagnostics.empty() ? std::string{} : shader.diagnostics.front().message));
    REQUIRE(shader.compiled());
    return shader;
}

} // namespace

TEST_CASE("Shaders of projects draw meshes, sprites, particles and the sky without validation errors",
          "[render][gpu][shader]")
{
    using devex::math::Mat4;
    using devex::math::Vec3;
    using devex::math::Vec4;

    // A flat colour from a uniform; a cut-out that casts its shadow; glass; a sprite turned blue; glowing
    // particles; a sky of a uniform colour.
    const devex::asset::ShaderData flat = compiled(R"(shader_type spatial;
render_mode unshaded;
uniform float3 tint : source_color = float3(0.0, 0.0, 1.0);
void vertex() { VERTEX += NORMAL * 0.01 * sin(TIME); }
void fragment() { ALBEDO = tint; }
)",
                                                   "flat.dvxshader");
    const devex::asset::ShaderData cutout = compiled(R"(shader_type spatial;
uniform sampler2D mask : hint_default_white;
void fragment()
{
    ALBEDO = float3(0.8);
    ALPHA = texture(mask, UV).r;
    ALPHA_SCISSOR_THRESHOLD = 0.5;
}
void light() { DIFFUSE_LIGHT += saturate(dot(NORMAL, LIGHT)) * ATTENUATION * LIGHT_COLOR / PI; }
)",
                                                     "cutout.dvxshader");
    const devex::asset::ShaderData glass = compiled(R"(shader_type spatial;
render_mode cull_disabled;
void fragment() { ALBEDO = float3(0.2, 0.4, 0.9); ALPHA = 0.3; }
)",
                                                    "glass.dvxshader");
    const devex::asset::ShaderData sprite = compiled(R"(shader_type canvas_item;
uniform float4 color : source_color = float4(0.0, 0.0, 1.0, 1.0);
void fragment() { COLOR = color; }
)",
                                                     "sprite.dvxshader");
    const devex::asset::ShaderData sparks = compiled(R"(shader_type particles;
render_mode blend_add;
void fragment() { COLOR.rgb = float3(4.0, 2.0, 0.5); }
)",
                                                     "sparks.dvxshader");
    const devex::asset::ShaderData sky = compiled(R"(shader_type sky;
uniform float3 color : source_color = float3(1.0, 0.0, 1.0);
void sky() { COLOR = color; }
)",
                                                  "sky.dvxshader");
    CHECK(glass.transparent);
    CHECK(cutout.discards);

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

        const auto create = [&](const devex::asset::ShaderData& shader) {
            const auto handle = renderer->createShader(shader);
            if (!handle)
            {
                FAIL(std::format("{}", handle.error()));
            }
            return *handle;
        };
        const devex::render::ShaderHandle flatShader = create(flat);
        const devex::render::ShaderHandle cutoutShader = create(cutout);
        const devex::render::ShaderHandle glassShader = create(glass);
        const devex::render::ShaderHandle spriteShader = create(sprite);
        const devex::render::ShaderHandle sparksShader = create(sparks);
        const devex::render::ShaderHandle skyShader = create(sky);
        // A shader that did not compile has no pipelines.
        CHECK_FALSE(renderer->createShader(devex::asset::ShaderData{}));

        // The flat cube is red through its uniform; the sky is green through its own.
        const devex::render::MaterialHandle red =
            renderer->createMaterial({.shader = flatShader, .parameters = {Vec4{1.0f, 0.0f, 0.0f, 0.0f}}});
        const devex::render::MaterialHandle holed = renderer->createMaterial(
            {.shader = cutoutShader, .parameters = {Vec4{0.0f}}, .textures = {{.slot = 0, .fallback = devex::render::TextureFallback::White}}});
        const devex::render::MaterialHandle pane = renderer->createMaterial({.shader = glassShader});
        const devex::render::MaterialHandle blue = renderer->createMaterial({.shader = spriteShader});
        const devex::render::MaterialHandle glow = renderer->createMaterial({.shader = sparksShader});
        const devex::render::MaterialHandle green =
            renderer->createMaterial({.shader = skyShader, .parameters = {Vec4{0.0f, 1.0f, 0.0f, 0.0f}}});
        // A sky material on a mesh is of the wrong kind: the mesh draws with the standard shader.
        const devex::render::MaterialHandle wrongKind = renderer->createMaterial({.shader = skyShader});

        const Mat4 cameraTransform = devex::math::translate(Mat4(1.0f), Vec3{0.0f, 0.0f, 6.0f});
        std::vector<devex::render::CapturedImage> captured;
        std::vector<devex::render::PickResult> picks;
        for (int frame = 0; frame < 10; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.camera.autoExposure = false;
            world.camera.ev100 = 0.0f;
            world.environment.intensity = 1.0f;
            world.environment.skyMaterial = green;
            world.sun = {.direction = devex::math::normalize(Vec3{-0.3f, -1.0f, -0.4f}),
                         .illuminance = {3.0f, 3.0f, 3.0f},
                         .castShadows = true};
            world.meshes.push_back({.mesh = *cube, .material = red, .transform = Mat4(1.0f), .objectId = 1, .outlined = true});
            world.meshes.push_back({.mesh = *cube,
                                    .material = holed,
                                    .transform = devex::math::translate(Mat4(1.0f), Vec3{2.5f, 0.0f, -1.0f}),
                                    .objectId = 2});
            world.meshes.push_back({.mesh = *cube,
                                    .material = pane,
                                    .transform = devex::math::translate(Mat4(1.0f), Vec3{-2.5f, 0.0f, 1.0f}),
                                    .objectId = 3});
            world.meshes.push_back({.mesh = *cube,
                                    .material = wrongKind,
                                    .transform = devex::math::translate(Mat4(1.0f), Vec3{0.0f, -3.0f, -2.0f}),
                                    .objectId = 4});
            world.sprites.push_back({.transform = devex::math::translate(Mat4(1.0f), Vec3{-2.0f, 2.0f, 0.0f}),
                                     .size = {1.0f, 1.0f},
                                     .material = blue,
                                     .objectId = 5,
                                     .outlined = true});
            world.particles.push_back({.position = {2.0f, 2.0f, 0.0f}, .size = 0.8f, .color = {1.0f, 1.0f, 1.0f, 1.0f}});
            world.particleDraws.push_back({.first = 0, .count = 1, .center = {2.0f, 2.0f, 0.0f}, .material = glow});
            if (frame == 3)
            {
                // In the middle of the image, where the red cube stands.
                world.pick = devex::render::PickRequest{.x = 160, .y = 120, .id = 1};
            }
            if (frame == 4)
            {
                // A new import of the sky replaces its pipelines.
                CHECK(renderer->updateShader(skyShader, sky));
            }
            if (frame == 6)
            {
                static_cast<void>(renderer->requestCapture(64, 48));
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
            std::ranges::move(renderer->takePickResults(), std::back_inserter(picks));
        }
        CHECK(renderer->stats().shaderCount == 6);

        REQUIRE(captured.size() == 1);
        const devex::render::CapturedImage& image = captured.front();
        const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
            const std::size_t at = (std::size_t{y} * image.width + x) * 4;
            return Vec3{static_cast<float>(image.rgba[at]), static_cast<float>(image.rgba[at + 1]),
                        static_cast<float>(image.rgba[at + 2])};
        };
        // The middle shows the cube in the red its material gives, the corner the green sky.
        const Vec3 middle = pixel(image.width / 2, image.height / 2);
        CHECK(middle.r > middle.g + 60.0f);
        CHECK(middle.r > middle.b + 60.0f);
        const Vec3 corner = pixel(2, 2);
        CHECK(corner.g > corner.r + 60.0f);
        CHECK(corner.g > corner.b + 60.0f);
        // Picking draws the shader too.
        REQUIRE(picks.size() == 1);
        CHECK(picks.front().objectId == 1);

        renderer->destroyShader(glassShader);
        renderer->destroyShader(skyShader);
        for (int frame = 0; frame < 3; ++frame)
        {
            static_cast<void>(renderer->beginFrame());
            REQUIRE(renderer->endFrame());
        }
        CHECK(renderer->stats().shaderCount == 4);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Instance uniforms give each object drawn with a material its own value", "[render][gpu][shader]")
{
    using devex::math::Mat4;
    using devex::math::Vec3;
    using devex::math::Vec4;

    const devex::asset::ShaderData tinted = compiled(R"(shader_type spatial;
render_mode unshaded;
instance uniform float3 tint : source_color = float3(0.0, 0.0, 1.0);
void fragment() { ALBEDO = tint; }
)",
                                                     "tinted.dvxshader");
    const devex::asset::ShaderData flashed = compiled(R"(shader_type canvas_item;
instance uniform float4 color : source_color = float4(0.0, 0.0, 1.0, 1.0);
void fragment() { COLOR = color; }
)",
                                                      "flashed.dvxshader");

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
        const auto tintedShader = renderer->createShader(tinted);
        const auto flashedShader = renderer->createShader(flashed);
        REQUIRE(tintedShader.has_value());
        REQUIRE(flashedShader.has_value());
        // The slots of the materials hold the defaults, as the asset manager gives them.
        const devex::render::MaterialHandle shared =
            renderer->createMaterial({.shader = *tintedShader, .parameters = {Vec4{0.0f, 0.0f, 1.0f, 0.0f}}});
        const devex::render::MaterialHandle sprites =
            renderer->createMaterial({.shader = *flashedShader, .parameters = {Vec4{0.0f, 0.0f, 1.0f, 1.0f}}});

        const Mat4 cameraTransform = devex::math::translate(Mat4(1.0f), Vec3{0.0f, 0.0f, 6.0f});
        std::vector<devex::render::CapturedImage> captured;
        for (int frame = 0; frame < 8; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.camera.autoExposure = false;
            world.camera.ev100 = 0.0f;
            world.environment.intensity = 0.05f;
            world.instanceParameters = {Vec4{1.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 1.0f, 0.0f, 1.0f}};
            // The left cube is red by its own value, the right one keeps the blue of the shader.
            world.meshes.push_back({.mesh = *cube,
                                    .material = shared,
                                    .transform = devex::math::translate(Mat4(1.0f), Vec3{-1.5f, 0.0f, 0.0f}),
                                    .objectId = 1,
                                    .instanceParameters = 0});
            world.meshes.push_back({.mesh = *cube,
                                    .material = shared,
                                    .transform = devex::math::translate(Mat4(1.0f), Vec3{1.5f, 0.0f, 0.0f}),
                                    .objectId = 2});
            // The sprite above is green by its own.
            world.sprites.push_back({.transform = devex::math::translate(Mat4(1.0f), Vec3{0.0f, 2.0f, 0.0f}),
                                     .size = {1.0f, 1.0f},
                                     .material = sprites,
                                     .objectId = 3,
                                     .instanceParameters = 1});
            if (frame == 5)
            {
                static_cast<void>(renderer->requestCapture(64, 48));
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
        const devex::render::CapturedImage& image = captured.front();
        const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
            const std::size_t at = (std::size_t{y} * image.width + x) * 4;
            return Vec3{static_cast<float>(image.rgba[at]), static_cast<float>(image.rgba[at + 1]),
                        static_cast<float>(image.rgba[at + 2])};
        };
        const Vec3 left = pixel(image.width / 2 - 10, image.height / 2);
        const Vec3 right = pixel(image.width / 2 + 10, image.height / 2);
        const Vec3 above = pixel(image.width / 2, image.height / 2 - 14);
        INFO(std::format("left {} {} {}, right {} {} {}, above {} {} {}", left.r, left.g, left.b, right.r, right.g, right.b, above.r,
                         above.g, above.b));
        CHECK(left.r > left.b + 60.0f);
        CHECK(right.b > right.r + 60.0f);
        CHECK(above.g > above.r + 60.0f);
        CHECK(above.g > above.b + 60.0f);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}
