// The WebGPU renderer, on a computer with Dawn: it draws a 2D scene as Vulkan does, and decodes the blocks of BC
// textures for a device without them as basisu does.
#include "RenderComparison.hpp"
#include "render/BlockDecoding.hpp"

#include <devex/asset/Primitives.hpp>
#include <devex/asset/import/ShaderFile.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
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
#include <utility>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace devex::test;

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

TEST_CASE("A scene drawn with half the pixels is stretched over the window, by WebGPU as by Vulkan", "[render][gpu][webgpu]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage full = drawMeshes(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan, false, false, 0.5f);
    const devex::render::CapturedImage webgpu = drawMeshes(devex::render::RenderApi::WebGpu, false, false, 0.5f);
    REQUIRE(vulkan.width == full.width);
    REQUIRE(vulkan.height == full.height);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
    const Difference apart = compare(vulkan, webgpu, 8);
    INFO(std::format("WebGPU: {} channels differ, {:.2f} on average", apart.channels, apart.mean));
    CHECK(apart.channels < vulkan.rgba.size() / 50);
    CHECK(apart.mean < 2.0);
    // The same picture as with every pixel, softer along the edges: the scene covers the window, not a corner of it.
    const Difference softer = compare(full, vulkan, 24);
    INFO(std::format("Every pixel: {} channels differ, {:.2f} on average", softer.channels, softer.mean));
    CHECK(softer.channels < full.rgba.size() / 10);
    CHECK(softer.mean < 4.0);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("WebGPU resolves the temporal antialiasing and the bloom as Vulkan does", "[render][gpu][webgpu]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan, true);
    const devex::render::CapturedImage webgpu = drawMeshes(devex::render::RenderApi::WebGpu, true);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
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

TEST_CASE("WebGPU draws the depth of field, the motion blur and the table of colours as Vulkan does", "[render][gpu][webgpu]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan, false, true);
    const devex::render::CapturedImage webgpu = drawMeshes(devex::render::RenderApi::WebGpu, false, true);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
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

TEST_CASE("WebGPU draws terrains, their layers, their shadows and their grass as Vulkan does", "[render][gpu][webgpu]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawTerrain(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage webgpu = drawTerrain(devex::render::RenderApi::WebGpu);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
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

TEST_CASE("WebGPU draws meshes with the spatial shaders of a project as Vulkan does", "[render][gpu][webgpu][shader]")
{
    const std::array shaders{compiledForEveryApi(R"(shader_type spatial;
uniform float3 tint : source_color = float3(1.0, 0.6, 0.3);
uniform sampler2D pattern;
void fragment() { ALBEDO = tint * texture(pattern, UV).rgb; ROUGHNESS = 0.4; METALLIC = 0.2; }
void light() { DIFFUSE_LIGHT += saturate(dot(NORMAL, LIGHT)) * ATTENUATION * LIGHT_COLOR / PI; }
)",
                                                   "lit.dvxshader"),
                             compiledForEveryApi(R"(shader_type spatial;
render_mode unshaded;
uniform float3 color : source_color = float3(0.2, 0.8, 0.4);
void vertex() { VERTEX += NORMAL * 0.1 * sin(VERTEX.y * 8.0); }
void fragment() { ALBEDO = color * (0.5 + 0.5 * NORMAL.y); }
)",
                                               "bent.dvxshader"),
                             compiledForEveryApi(R"(shader_type spatial;
uniform sampler2D mask : hint_default_white;
void fragment() { ALBEDO = float3(0.8, 0.8, 0.85); ALPHA = texture(mask, UV).r; ALPHA_SCISSOR_THRESHOLD = 0.5; }
)",
                                               "cutout.dvxshader")};
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawShaded(devex::render::RenderApi::Vulkan, shaders);
    const devex::render::CapturedImage webgpu = drawShaded(devex::render::RenderApi::WebGpu, shaders);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
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

TEST_CASE("WebGPU draws sprites with the canvas_item shaders of a project, and lit sprites, as Vulkan does",
          "[render][gpu][webgpu][shader]")
{
    const devex::asset::ShaderData shader = compiledForEveryApi(R"(shader_type canvas_item;
uniform float3 tint : source_color = float3(1.0, 0.5, 0.2);
instance uniform float amount = 0.5;
void vertex() { VERTEX.x += VERTEX.y * 0.2; }
void fragment()
{
    const float4 base = texture(TEXTURE, UV);
    COLOR.rgb = lerp(COLOR.rgb, tint * base.rgb, amount) * (TEXTURE_PIXEL_SIZE.x > 0.25 ? 1.0 : 0.5);
    if (UV.x > 0.5)
    {
        COLOR.rgb *= texture(TEXTURE, UV.yx).rgb;
    }
    NORMAL_MAP = float3(0.7, 0.5, 1.0);
}
)",
                                                                 "tinted.dvxshader");
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawShadedSprites(devex::render::RenderApi::Vulkan, shader);
    const devex::render::CapturedImage webgpu = drawShadedSprites(devex::render::RenderApi::WebGpu, shader);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
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

TEST_CASE("WebGPU draws the sky and particles with the shaders of a project, and lights the scene with that sky, as Vulkan "
          "does",
          "[render][gpu][webgpu][shader]")
{
    const devex::asset::ShaderData sky = compiledForEveryApi(R"(shader_type sky;
uniform float3 top : source_color = float3(0.2, 0.4, 0.9);
uniform float3 bottom : source_color = float3(0.8, 0.6, 0.4);
void sky()
{
    COLOR = lerp(bottom, top, saturate(EYEDIR.y * 0.5 + 0.5)) + float3(0.3, 0.0, 0.0) * step(0.5, SKY_COORDS.x);
    if (!AT_CUBEMAP_PASS && LIGHT0_ENABLED)
    {
        COLOR += LIGHT0_COLOR * 4.0 * step(0.995, dot(EYEDIR, LIGHT0_DIRECTION));
    }
}
)",
                                                           "gradient.dvxshader");
    const devex::asset::ShaderData particles = compiledForEveryApi(R"(shader_type particles;
render_mode blend_add;
uniform float3 glow : source_color = float3(1.0, 0.6, 0.2);
void vertex() { VERTEX.y += 0.1 * float(INSTANCE_ID % 2u); }
void fragment() { COLOR.rgb *= glow * (0.5 + 0.5 * PARTICLE_SIZE); }
)",
                                                                 "embers.dvxshader");
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawShadedSky(devex::render::RenderApi::Vulkan, sky, particles);
    const devex::render::CapturedImage webgpu = drawShadedSky(devex::render::RenderApi::WebGpu, sky, particles);
    REQUIRE(webgpu.rgba.size() == vulkan.rgba.size());
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
            const std::vector<std::byte> decoded = devex::render::decodeBlocks(format, bytes, 64, 64);
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
