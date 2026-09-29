#include <devex/ui/Color.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using devex::math::Vec3;
using devex::math::Vec4;

TEST_CASE("Colours go between linear and sRGB and back", "[ui][color]")
{
    using devex::ui::linearFromSrgb;
    using devex::ui::srgbFromLinear;
    CHECK(srgbFromLinear(0.0f) == Catch::Approx(0.0f));
    CHECK(srgbFromLinear(1.0f) == Catch::Approx(1.0f));
    // Middle grey of the eye is about a fifth of the light.
    CHECK(linearFromSrgb(0.5f) == Catch::Approx(0.214f).margin(0.001f));
    for (const float value : {0.001f, 0.02f, 0.3f, 0.75f, 1.0f})
    {
        CHECK(linearFromSrgb(srgbFromLinear(value)) == Catch::Approx(value).margin(1e-5f));
    }
    const Vec4 color = srgbFromLinear(Vec4{0.214f, 1.0f, 0.0f, 0.3f});
    CHECK(color.x == Catch::Approx(0.5f).margin(0.002f));
    CHECK(color.w == Catch::Approx(0.3f));
}

TEST_CASE("Hue, saturation and value describe a colour", "[ui][color]")
{
    using devex::ui::hsvFromRgb;
    using devex::ui::rgbFromHsv;
    const Vec3 red = hsvFromRgb(Vec3{1.0f, 0.0f, 0.0f});
    CHECK(red.x == Catch::Approx(0.0f));
    CHECK(red.y == Catch::Approx(1.0f));
    CHECK(red.z == Catch::Approx(1.0f));
    CHECK(hsvFromRgb(Vec3{0.0f, 1.0f, 0.0f}).x == Catch::Approx(1.0f / 3.0f));
    CHECK(hsvFromRgb(Vec3{0.0f, 0.0f, 1.0f}).x == Catch::Approx(2.0f / 3.0f));
    const Vec3 grey = hsvFromRgb(Vec3{0.5f, 0.5f, 0.5f});
    CHECK(grey.y == Catch::Approx(0.0f));
    CHECK(grey.z == Catch::Approx(0.5f));
    for (const Vec3 rgb : {Vec3{0.2f, 0.7f, 0.4f}, Vec3{0.9f, 0.1f, 0.6f}, Vec3{0.3f, 0.3f, 0.8f}, Vec3{1.0f, 0.5f, 0.0f}})
    {
        const Vec3 back = rgbFromHsv(hsvFromRgb(rgb));
        CHECK(back.x == Catch::Approx(rgb.x).margin(1e-5f));
        CHECK(back.y == Catch::Approx(rgb.y).margin(1e-5f));
        CHECK(back.z == Catch::Approx(rgb.z).margin(1e-5f));
    }
    // A hue of 1 is red again.
    CHECK(rgbFromHsv(Vec3{1.0f, 1.0f, 1.0f}).x == Catch::Approx(1.0f));
}

TEST_CASE("Colours are written and read in hexadecimal", "[ui][color]")
{
    using devex::ui::colorFromHex;
    using devex::ui::hexFromColor;
    CHECK(hexFromColor(Vec4{1.0f, 0.5f, 0.0f, 1.0f}, false) == "FF8000");
    CHECK(hexFromColor(Vec4{0.0f, 0.0f, 1.0f, 0.5f}, true) == "0000FF80");
    const auto read = colorFromHex("#ff8000");
    REQUIRE(read.has_value());
    CHECK(read->x == Catch::Approx(1.0f));
    CHECK(read->y == Catch::Approx(128.0f / 255.0f));
    CHECK(read->w == Catch::Approx(1.0f));
    const auto shortForm = colorFromHex("F80C");
    REQUIRE(shortForm.has_value());
    CHECK(shortForm->y == Catch::Approx(136.0f / 255.0f));
    CHECK(shortForm->w == Catch::Approx(204.0f / 255.0f));
    CHECK_FALSE(colorFromHex("12345").has_value());
    CHECK_FALSE(colorFromHex("GG0000").has_value());
}
