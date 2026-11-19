// The OpenGL renderer: its shaders translated for OpenGL ES 3.0 and OpenGL 3.3, and the 2D scene with its interface, the
// sprites under the 2D lights and the lit meshes drawn as Vulkan draws them, with OpenGL 3.3 and with OpenGL ES 3.0 where the driver of the computer
// offers it.
#include "RenderComparison.hpp"
#include "render/opengl/GlShaders.hpp"

#include <devex/asset/import/ShaderFile.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>


#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace devex::test;

// Whether the image of OpenGL is that of Vulkan, but for a few pixels along the edges, where the two may round a
// coverage differently.
void checkLikeVulkan(const devex::render::CapturedImage& vulkan, const devex::render::CapturedImage& openGl)
{
    REQUIRE(openGl.width == vulkan.width);
    REQUIRE(openGl.height == vulkan.height);
    REQUIRE(openGl.rgba.size() == vulkan.rgba.size());
    const Difference difference = compare(vulkan, openGl, 6);
    INFO(std::format("{} channels differ, {:.2f} on average", difference.channels, difference.mean));
    CHECK(difference.channels < vulkan.rgba.size() / 200);
    CHECK(difference.mean < 1.0);
    // The environment shows through in a corner, exposed and encoded as the display shows it.
    const std::size_t corner = (std::size_t{230} * vulkan.width + 310) * 4;
    CHECK(openGl.rgba[corner + 2] > openGl.rgba[corner]);
}

// Why OpenGL ES cannot be had on this computer, or nothing when it can.
[[nodiscard]] std::optional<std::string> withoutOpenGlEs()
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 64, .height = 64, .openGl = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer =
        devex::render::Renderer::create(*platform, *window, {.validation = false, .api = devex::render::RenderApi::OpenGl, .openGlEs = true});
    return renderer ? std::nullopt : std::optional(std::format("{}", renderer.error()));
}

// Whether the lit meshes of OpenGL are those of Vulkan, but for a few pixels along the edges and in the filtered shadows.
void checkMeshesLikeVulkan(const devex::render::CapturedImage& vulkan, const devex::render::CapturedImage& openGl)
{
    REQUIRE(openGl.rgba.size() == vulkan.rgba.size());
    const Difference difference = compare(vulkan, openGl, 8);
    INFO(std::format("{} channels differ, {:.2f} on average", difference.channels, difference.mean));
    CHECK(difference.channels < vulkan.rgba.size() / 100);
    CHECK(difference.mean < 1.5);
}

// Whether the sprites under the 2D lights are lit as Vulkan lights them, but for a few pixels along the edges of the
// shadows, where the two may round differently.
void checkLights2DLikeVulkan(const devex::render::CapturedImage& vulkan, const devex::render::CapturedImage& openGl)
{
    REQUIRE(openGl.rgba.size() == vulkan.rgba.size());
    const Difference difference = compare(vulkan, openGl, 6);
    INFO(std::format("{} channels differ, {:.2f} on average", difference.channels, difference.mean));
    CHECK(difference.channels < vulkan.rgba.size() / 200);
    CHECK(difference.mean < 1.0);
}

} // namespace

TEST_CASE("Every shader of OpenGL is translated for OpenGL ES 3.0 and OpenGL 3.3", "[render][opengl]")
{
    for (const std::string_view entry : {"skyVertex", "skyFragment", "spriteVertex", "spriteFragment", "uiVertex", "uiFragment", "meshVertex",
                                         "skinnedVertex", "meshFragment", "shadowVertex", "skinnedShadowVertex", "shadowFragment", "bakeVertex",
                                         "equirectangularToCube", "prefilterSpecular", "irradiance", "brdfTable", "luminanceFragment", "particleVertex",
                                         "particleFragment", "ribbonVertex", "ribbonFragment",
                                         "groundVertex", "groundFragment", "groundShadowVertex", "groundShadowFragment", "grassVertex",
                                         "grassFragment", "blitVertex", "blitFragment",
                                         "tonemapVertex", "tonemapFragment"})
    {
        INFO(entry);
        CHECK(devex::render::opengl::shaderSource(entry, true).starts_with("#version 300 es"));
        CHECK(devex::render::opengl::shaderSource(entry, false).starts_with("#version 330"));
    }
    // GLSL ES links what the vertex stage gives the fragment stage by name, which the build makes the same.
    CHECK(devex::render::opengl::shaderSource("spriteVertex", true).find(" devex_varying0;") != std::string_view::npos);
    CHECK(devex::render::opengl::shaderSource("spriteFragment", true).find(" devex_varying0;") != std::string_view::npos);
    CHECK(devex::render::opengl::shaderSource("nothing", true).empty());
}

TEST_CASE("OpenGL draws a 2D scene and its interface as Vulkan does", "[render][gpu][opengl]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawScene(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGl = drawScene(devex::render::RenderApi::OpenGl);
    checkLikeVulkan(vulkan, openGl);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL ES 3.0 draws the same 2D scene, where the computer offers it", "[render][gpu][opengl]")
{
    // A driver without OpenGL ES on a computer has nothing to compare.
    if (const std::optional<std::string> missing = withoutOpenGlEs())
    {
        SKIP(*missing);
    }
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawScene(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGlEs = drawScene(devex::render::RenderApi::OpenGl, true);
    checkLikeVulkan(vulkan, openGlEs);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL draws lit meshes, their decals, their shadows, the fog and particles as Vulkan does", "[render][gpu][opengl]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGl = drawMeshes(devex::render::RenderApi::OpenGl);
    checkMeshesLikeVulkan(vulkan, openGl);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL ES 3.0 draws the same lit meshes, decals and particles, where the computer offers it", "[render][gpu][opengl]")
{
    if (const std::optional<std::string> missing = withoutOpenGlEs())
    {
        SKIP(*missing);
    }
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGlEs = drawMeshes(devex::render::RenderApi::OpenGl, false, false, 1.0f, true, true, true);
    checkMeshesLikeVulkan(vulkan, openGlEs);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL resolves the temporal antialiasing and the bloom as Vulkan does", "[render][gpu][opengl]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan, true);
    const devex::render::CapturedImage openGl = drawMeshes(devex::render::RenderApi::OpenGl, true);
    checkMeshesLikeVulkan(vulkan, openGl);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL draws the depth of field, the motion blur and the table of colours as Vulkan does", "[render][gpu][opengl]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawMeshes(devex::render::RenderApi::Vulkan, false, true);
    const devex::render::CapturedImage openGl = drawMeshes(devex::render::RenderApi::OpenGl, false, true);
    checkMeshesLikeVulkan(vulkan, openGl);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL ES 3.0 draws the same effects after the light, where the computer offers it", "[render][gpu][opengl]")
{
    if (const std::optional<std::string> missing = withoutOpenGlEs())
    {
        SKIP(*missing);
    }
    const ErrorCapture capture;
    const devex::render::CapturedImage temporalVulkan = drawMeshes(devex::render::RenderApi::Vulkan, true);
    const devex::render::CapturedImage temporalEs = drawMeshes(devex::render::RenderApi::OpenGl, true, false, 1.0f, true, true, true);
    checkMeshesLikeVulkan(temporalVulkan, temporalEs);
    const devex::render::CapturedImage lensVulkan = drawMeshes(devex::render::RenderApi::Vulkan, false, true);
    const devex::render::CapturedImage lensEs = drawMeshes(devex::render::RenderApi::OpenGl, false, true, 1.0f, true, true, true);
    checkMeshesLikeVulkan(lensVulkan, lensEs);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL lights sprites with the 2D lights, their shadows and the local lights as Vulkan does", "[render][gpu][opengl]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawLights2D(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGl = drawLights2D(devex::render::RenderApi::OpenGl);
    checkLights2DLikeVulkan(vulkan, openGl);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL ES 3.0 lights the same sprites, where the computer offers it", "[render][gpu][opengl]")
{
    if (const std::optional<std::string> missing = withoutOpenGlEs())
    {
        SKIP(*missing);
    }
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawLights2D(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGlEs = drawLights2D(devex::render::RenderApi::OpenGl, true);
    checkLights2DLikeVulkan(vulkan, openGlEs);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL draws terrains, their layers, their shadows and their grass as Vulkan does", "[render][gpu][opengl]")
{
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawTerrain(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGl = drawTerrain(devex::render::RenderApi::OpenGl);
    checkMeshesLikeVulkan(vulkan, openGl);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL ES 3.0 draws the same terrains, where the computer offers it", "[render][gpu][opengl]")
{
    if (const std::optional<std::string> missing = withoutOpenGlEs())
    {
        SKIP(*missing);
    }
    const ErrorCapture capture;
    const devex::render::CapturedImage vulkan = drawTerrain(devex::render::RenderApi::Vulkan);
    const devex::render::CapturedImage openGlEs = drawTerrain(devex::render::RenderApi::OpenGl, true);
    checkMeshesLikeVulkan(vulkan, openGlEs);

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL draws meshes with the spatial shaders of a project as Vulkan does", "[render][gpu][opengl][shader]")
{
    if (!devex::asset::translatesGlsl())
    {
        SKIP("this build does not translate shaders into GLSL");
    }
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
    const devex::render::CapturedImage openGl = drawShaded(devex::render::RenderApi::OpenGl, shaders);
    checkMeshesLikeVulkan(vulkan, openGl);
    if (const std::optional<std::string> missing = withoutOpenGlEs(); !missing)
    {
        checkMeshesLikeVulkan(vulkan, drawShaded(devex::render::RenderApi::OpenGl, shaders, true));
    }
    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL draws sprites with the canvas_item shaders of a project, and lit sprites, as Vulkan does",
          "[render][gpu][opengl][shader]")
{
    if (!devex::asset::translatesGlsl())
    {
        SKIP("this build does not translate shaders into GLSL");
    }
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
    const devex::render::CapturedImage openGl = drawShadedSprites(devex::render::RenderApi::OpenGl, shader);
    checkMeshesLikeVulkan(vulkan, openGl);
    if (const std::optional<std::string> missing = withoutOpenGlEs(); !missing)
    {
        checkMeshesLikeVulkan(vulkan, drawShadedSprites(devex::render::RenderApi::OpenGl, shader, true));
    }
    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("OpenGL draws the sky and particles with the shaders of a project, and lights the scene with that sky, as Vulkan does",
          "[render][gpu][opengl][shader]")
{
    if (!devex::asset::translatesGlsl())
    {
        SKIP("this build does not translate shaders into GLSL");
    }
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
    const devex::render::CapturedImage openGl = drawShadedSky(devex::render::RenderApi::OpenGl, sky, particles);
    checkMeshesLikeVulkan(vulkan, openGl);
    if (const std::optional<std::string> missing = withoutOpenGlEs(); !missing)
    {
        checkMeshesLikeVulkan(vulkan, drawShadedSky(devex::render::RenderApi::OpenGl, sky, particles, true));
    }
    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("A machine with Vulkan 1.3 draws its games with Vulkan, chosen before any window", "[render][gpu]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    CHECK(devex::render::Renderer::chooseApi(*platform, true) == devex::render::RenderApi::Vulkan);
    CHECK(devex::render::Renderer::chooseApi(*platform, false) == devex::render::RenderApi::Vulkan);
}
