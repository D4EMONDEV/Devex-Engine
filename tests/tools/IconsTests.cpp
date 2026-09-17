#include "tools/Icons.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>

using devex::tools::detail::Icon;
using devex::tools::detail::IconSet;
using devex::tools::detail::IconText;

TEST_CASE("Icons are characters of the private use area written in UTF-8", "[tools][icons]")
{
    constexpr IconText first{static_cast<Icon>(0)};
    CHECK(std::string_view(first) == "\xEE\x80\x80");
    constexpr IconText logo{Icon::Logo};
    const auto codepoint = static_cast<char32_t>(0xE000 + static_cast<unsigned>(Icon::Logo));
    const std::string_view bytes = logo;
    REQUIRE(bytes.size() == 3);
    CHECK(static_cast<unsigned char>(bytes[0]) == (0xE0 | (codepoint >> 12)));
    CHECK(static_cast<unsigned char>(bytes[1]) == (0x80 | ((codepoint >> 6) & 0x3F)));
    CHECK(static_cast<unsigned char>(bytes[2]) == (0x80 | (codepoint & 0x3F)));
    CHECK(devex::tools::detail::withIcon(devex::tools::detail::icons::Save, "Save") ==
          std::string(std::string_view(devex::tools::detail::icons::Save)) + "  Save");
}

TEST_CASE("SVG documents are drawn with currentColor as white", "[tools][icons]")
{
    constexpr std::string_view circle =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">)"
        R"(<circle cx="12" cy="12" r="8" fill="currentColor"/></svg>)";
    const auto image = devex::tools::detail::renderSvg(circle, 32, 32);
    REQUIRE(image.has_value());
    REQUIRE(image->rgba.size() == 32u * 32u * 4u);
    const auto pixel = [&](std::size_t x, std::size_t y, std::size_t channel) { return image->rgba[(y * 32 + x) * 4 + channel]; };
    CHECK(pixel(16, 16, 3) == 255);
    CHECK(pixel(16, 16, 0) == 255);
    CHECK(pixel(0, 0, 3) == 0);

    CHECK_FALSE(devex::tools::detail::renderSvg("not an svg", 16, 16).has_value());
}

TEST_CASE("Every editor icon has its SVG file among the resources", "[tools][icons]")
{
    const IconSet icons = IconSet::load(std::filesystem::path(DEVEX_TEST_RESOURCES_DIRECTORY) / "icons");
    for (std::size_t index = 0; index < static_cast<std::size_t>(Icon::Count); ++index)
    {
        const auto icon = static_cast<Icon>(index);
        CAPTURE(devex::tools::detail::iconFileName(icon));
        CHECK(icons.contains(icon));
    }
    CHECK(IconSet::isColored(Icon::Logo));
    CHECK_FALSE(IconSet::isColored(Icon::Play));

    // The logo keeps its own colors.
    const auto logo = devex::tools::detail::renderSvg(icons.svg(Icon::Logo), 24, 24);
    REQUIRE(logo.has_value());
    const std::size_t center = (4 * 24 + 4) * 4;
    CHECK(logo->rgba[center + 3] == 255);
    CHECK(logo->rgba[center + 2] > logo->rgba[center]);
}
