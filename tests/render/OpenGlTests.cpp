// The OpenGL renderer: its shaders translated for OpenGL ES 3.0 and OpenGL 3.3, and the 2D scene with its interface
// drawn as Vulkan draws it, with OpenGL 3.3 and with OpenGL ES 3.0 where the driver of the computer offers it.
#include "RenderComparison.hpp"
#include "render/opengl/GlShaders.hpp"

#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <format>
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

} // namespace

TEST_CASE("Every shader of OpenGL is translated for OpenGL ES 3.0 and OpenGL 3.3", "[render][opengl]")
{
    for (const std::string_view entry : {"skyVertex", "skyFragment", "spriteVertex", "spriteFragment", "uiVertex", "uiFragment",
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
    {
        // A driver without OpenGL ES on a computer has nothing to compare.
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 64, .height = 64, .openGl = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = false, .api = devex::render::RenderApi::OpenGl,
                                                                             .openGlEs = true});
        if (!renderer)
        {
            SKIP(std::format("{}", renderer.error()));
        }
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
