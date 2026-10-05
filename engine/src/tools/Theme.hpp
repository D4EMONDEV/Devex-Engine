#pragma once

#include <devex/core/Export.hpp>

#include "Icons.hpp"

#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/serialization/Text.hpp>

#include <imgui.h>

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
    ImVec4 accent;
    ImVec4 text;
    ImVec4 textDim;
    // Behind the panels: menu bar, status bar and the gaps between docked panels.
    ImVec4 outer;
    ImVec4 panel;
    // Text fields and lists.
    ImVec4 field;
    // Buttons, a little lighter than the panel; and what popups and menus stand on.
    ImVec4 raised;
    ImVec4 popup;
    ImVec4 border;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 error;
    ImVec4 axisX;
    ImVec4 axisY;
    ImVec4 axisZ;

    // Icon colors by kind of object, like Godot's node colors.
    ImVec4 entity;
    ImVec4 light;
    ImVec4 camera;
    ImVec4 environment;
    ImVec4 gameCode;
    ImVec4 physics;
    ImVec4 audio;
    ImVec4 animation;
    ImVec4 interface;
    ImVec4 folder;
    ImVec4 scene;
    // Entities from prefabs, as in Unity.
    ImVec4 prefab;
    ImVec4 material;
    ImVec4 texture;
    ImVec4 neutral;
    ImVec4 favorite;

    // Syntax colors of the text editor, by TokenKind.
    ImVec4 codeKeyword;
    ImVec4 codeType;
    ImVec4 codeComment;
    ImVec4 codeString;
    ImVec4 codeNumber;
    ImVec4 codeDirective;
    ImVec4 codePunctuation;
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
    ImVec4 color;
};
[[nodiscard]] DEVEX_API EntityIcon entityIcon(const scene::Scene& scene, scene::Entity entity);
[[nodiscard]] DEVEX_API EntityIcon componentIcon(std::string_view componentName);
// Whether a component comes from the code of the game rather than from the engine.
[[nodiscard]] DEVEX_API bool isGameComponent(std::string_view name) noexcept;

} // namespace devex::tools::detail
