#pragma once

#include <devex/core/Export.hpp>

#include "Icons.hpp"

#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/serialization/Text.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace devex::scene {
class Scene;
}

namespace devex::tools::detail {

enum class ThemePreset : std::uint8_t
{
    // Neutral grays with a blue accent.
    Gray,
    // The blue-tinted grays of Godot's classic theme.
    BlueGray,
    // Pure black panels, for OLED displays.
    Black,
    Light,
};

inline constexpr std::array themePresets{ThemePreset::Gray, ThemePreset::BlueGray, ThemePreset::Black,
                                         ThemePreset::Light};

// "Gray", "Blue gray"... for menus; "gray", "blue_gray"... in settings files.
[[nodiscard]] DEVEX_API const char* displayName(ThemePreset preset) noexcept;
[[nodiscard]] DEVEX_API std::string_view toString(ThemePreset preset) noexcept;
[[nodiscard]] DEVEX_API std::optional<ThemePreset> parseThemePreset(std::string_view text) noexcept;

// The appearance of the editor, as the user set it. Colors are sRGB, from 0 to 1.
struct DEVEX_API ThemeSettings
{
    ThemePreset preset = ThemePreset::Gray;
    // The color of panels, from which the other surfaces are derived.
    math::Vec3 baseColor{0.149f, 0.149f, 0.149f};
    // Selections, focus and active tabs.
    math::Vec3 accentColor{0.310f, 0.584f, 0.918f};
    // How much darker (or, below zero, lighter) the backgrounds around panels and fields are.
    float contrast = 0.25f;
    // Size of the interface, where 1 is 100 %; 0 follows the scale of the display.
    float interfaceScale = 0.0f;
    // Sizes of the fonts in points, as in text editors, before the interface scale.
    float fontSize = 14.0f;
    float codeFontSize = 13.0f;

    // The colors and contrast of a preset, with the other settings left as they are.
    void applyPreset(ThemePreset newPreset) noexcept;

    bool operator==(const ThemeSettings&) const = default;
};

// Reads the attributes of a [theme] section of the editor's settings, keeping the defaults of
// missing or invalid ones.
[[nodiscard]] DEVEX_API ThemeSettings readThemeSettings(const serialization::TextSection& section);
[[nodiscard]] DEVEX_API serialization::TextSection writeThemeSettings(const ThemeSettings& settings);

// The interface scale in effect: the setting, or the display's scale when it follows the display.
[[nodiscard]] DEVEX_API float effectiveInterfaceScale(const ThemeSettings& settings, float displayScale) noexcept;

// Colors derived from the settings, in sRGB, for what panels draw themselves.
struct DEVEX_API ThemeColors
{
    bool dark = true;
    math::Vec4 accent{0.0f};
    math::Vec4 text{0.0f};
    math::Vec4 textDim{0.0f};
    // Behind the panels: menu bar, status bar and the gaps between docked panels.
    math::Vec4 outer{0.0f};
    math::Vec4 panel{0.0f};
    // Text fields and lists.
    math::Vec4 field{0.0f};
    // Buttons, a little lighter than the panel; and what popups and menus stand on.
    math::Vec4 raised{0.0f};
    math::Vec4 popup{0.0f};
    math::Vec4 border{0.0f};
    math::Vec4 success{0.0f};
    math::Vec4 warning{0.0f};
    math::Vec4 error{0.0f};
    math::Vec4 axisX{0.0f};
    math::Vec4 axisY{0.0f};
    math::Vec4 axisZ{0.0f};

    // Icon colors by kind of object, like Godot's node colors.
    math::Vec4 entity{0.0f};
    math::Vec4 light{0.0f};
    math::Vec4 camera{0.0f};
    math::Vec4 environment{0.0f};
    math::Vec4 gameCode{0.0f};
    math::Vec4 physics{0.0f};
    math::Vec4 audio{0.0f};
    math::Vec4 animation{0.0f};
    math::Vec4 interface{0.0f};
    math::Vec4 folder{0.0f};
    math::Vec4 scene{0.0f};
    // Entities from prefabs, as in Unity.
    math::Vec4 prefab{0.0f};
    math::Vec4 material{0.0f};
    math::Vec4 texture{0.0f};
    math::Vec4 neutral{0.0f};
    math::Vec4 favorite{0.0f};

    // Syntax colors of the text editor, by TokenKind.
    math::Vec4 codeKeyword{0.0f};
    math::Vec4 codeType{0.0f};
    math::Vec4 codeComment{0.0f};
    math::Vec4 codeString{0.0f};
    math::Vec4 codeNumber{0.0f};
    math::Vec4 codeDirective{0.0f};
    math::Vec4 codePunctuation{0.0f};
};

[[nodiscard]] DEVEX_API ThemeColors deriveThemeColors(const ThemeSettings& settings);

// The sizes of the frame of the editor in points, at the interface scale, on the spacing of Godot's
// editor theme: a base margin of 4 the others follow from.
struct DEVEX_API ThemeMetrics
{
    // The interface scale in effect.
    float scale = 1.0f;
    // A line of the regular font, from ascender to descender.
    float lineHeight = 19.0f;
    float margin = 4.0f;
    float menuBarHeight = 34.0f;
    float statusBarHeight = 30.0f;
    // The strip of tabs above a docked panel.
    float tabHeight = 31.0f;
    // Between the places of the dock, and inside the edges of a docked panel.
    float dockGap = 4.0f;
    float panelPadding = 4.0f;
    // The title of a modal window.
    float titleHeight = 25.0f;
};

[[nodiscard]] DEVEX_API ThemeMetrics deriveThemeMetrics(const ThemeSettings& settings, float displayScale) noexcept;

// Makes the colors and sizes of the settings those of the editor, for a display scale.
DEVEX_API void applyTheme(const ThemeSettings& settings, float displayScale);

// The colors and sizes of the theme last applied.
[[nodiscard]] DEVEX_API const ThemeColors& themeColors() noexcept;
[[nodiscard]] DEVEX_API const ThemeMetrics& themeMetrics() noexcept;

// The height of a line of a font of the editor in pixels, for its size in points.
[[nodiscard]] DEVEX_API float regularFontPixels(float points) noexcept;
[[nodiscard]] DEVEX_API float monoFontPixels(float points) noexcept;

// The icon and color that stand for an entity in lists, from its components.
struct DEVEX_API EntityIcon
{
    IconText icon;
    math::Vec4 color{0.0f};
};
[[nodiscard]] DEVEX_API EntityIcon entityIcon(const scene::Scene& scene, scene::Entity entity);
[[nodiscard]] DEVEX_API EntityIcon componentIcon(std::string_view componentName);
// Whether a component comes from the code of the game rather than from the engine.
[[nodiscard]] DEVEX_API bool isGameComponent(std::string_view name) noexcept;

} // namespace devex::tools::detail
