#include "tools/Theme.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using devex::tools::detail::ThemePreset;
using devex::tools::detail::ThemeSettings;

namespace {

[[nodiscard]] float brightness(ImVec4 color)
{
    return (color.x + color.y + color.z) / 3.0f;
}

} // namespace

TEST_CASE("Theme settings are written and read back", "[tools][theme]")
{
    ThemeSettings settings;
    settings.applyPreset(ThemePreset::BlueGray);
    settings.accentColor = {1.0f, 0.5f, 0.0f};
    settings.interfaceScale = 1.25f;
    settings.fontSize = 16.0f;

    const devex::serialization::TextSection section = devex::tools::detail::writeThemeSettings(settings);
    CHECK(section.type == "theme");
    const ThemeSettings read = devex::tools::detail::readThemeSettings(section);
    CHECK(read.preset == ThemePreset::BlueGray);
    CHECK(read.baseColor.r == Catch::Approx(settings.baseColor.r).margin(1.0 / 255.0));
    CHECK(read.accentColor.g == Catch::Approx(0.5f).margin(1.0 / 255.0));
    CHECK(read.interfaceScale == 1.25f);
    CHECK(read.fontSize == 16.0f);
}

TEST_CASE("Invalid theme settings fall back to usable values", "[tools][theme]")
{
    const auto document = devex::serialization::parseText(
        R"([theme preset="neon" base_color="#12" accent_color="#zzzzzz" contrast=9 interface_scale=40 font_size=2])");
    REQUIRE(document.has_value());
    const ThemeSettings settings = devex::tools::detail::readThemeSettings(document->sections.front());
    const ThemeSettings defaults;
    CHECK(settings.preset == ThemePreset::Gray);
    CHECK(settings.baseColor == defaults.baseColor);
    CHECK(settings.accentColor == defaults.accentColor);
    CHECK(settings.contrast == 1.0f);
    CHECK(settings.interfaceScale == 3.0f);
    CHECK(settings.fontSize == 8.0f);
}

TEST_CASE("The interface follows the display unless its scale is set", "[tools][theme]")
{
    ThemeSettings settings;
    CHECK(devex::tools::detail::effectiveInterfaceScale(settings, 1.5f) == 1.5f);
    settings.interfaceScale = 1.25f;
    CHECK(devex::tools::detail::effectiveInterfaceScale(settings, 1.5f) == 1.25f);
    settings.interfaceScale = 0.0f;
    CHECK(devex::tools::detail::effectiveInterfaceScale(settings, 0.0f) == 0.5f);
}

TEST_CASE("Theme colors derive from the base, accent and contrast", "[tools][theme]")
{
    for (const ThemePreset preset : devex::tools::detail::themePresets)
    {
        ThemeSettings settings;
        settings.applyPreset(preset);
        const devex::tools::detail::ThemeColors colors = devex::tools::detail::deriveThemeColors(settings);
        CAPTURE(devex::tools::detail::toString(preset));
        CHECK(colors.dark == (preset != ThemePreset::Light));
        // Text stands out from the panels in every preset.
        CHECK(std::abs(brightness(colors.text) - brightness(colors.panel)) > 0.5f);
        // Fields differ from the panels they sit on.
        CHECK(brightness(colors.field) != brightness(colors.panel));
        CHECK(devex::tools::detail::parseThemePreset(devex::tools::detail::toString(preset)) == preset);
    }

    ThemeSettings gray;
    const devex::tools::detail::ThemeColors grayColors = devex::tools::detail::deriveThemeColors(gray);
    CHECK(brightness(grayColors.outer) < brightness(grayColors.panel));
    gray.contrast = 0.0f;
    const devex::tools::detail::ThemeColors flat = devex::tools::detail::deriveThemeColors(gray);
    CHECK(brightness(flat.outer) == Catch::Approx(brightness(flat.panel)));
}

TEST_CASE("Font sizes in points become ImGui line heights", "[tools][theme]")
{
    CHECK(devex::tools::detail::regularFontPixels(14.0f) == 19.0f);
    CHECK(devex::tools::detail::monoFontPixels(13.0f) == 17.0f);
}
