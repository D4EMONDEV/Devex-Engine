#pragma once

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
[[nodiscard]] const char* displayName(ThemePreset preset) noexcept;
[[nodiscard]] std::string_view toString(ThemePreset preset) noexcept;
[[nodiscard]] std::optional<ThemePreset> parseThemePreset(std::string_view text) noexcept;

// The appearance of the editor, as the user set it. Colors are sRGB, from 0 to 1.
struct ThemeSettings
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
[[nodiscard]] ThemeSettings readThemeSettings(const serialization::TextSection& section);
[[nodiscard]] serialization::TextSection writeThemeSettings(const ThemeSettings& settings);

// The interface scale in effect: the setting, or the display's scale when it follows the display.
[[nodiscard]] float effectiveInterfaceScale(const ThemeSettings& settings, float displayScale) noexcept;

// Colors derived from the settings, in sRGB, for what panels draw themselves.
struct ThemeColors
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

[[nodiscard]] ThemeColors deriveThemeColors(const ThemeSettings& settings);

// Applies the settings to ImGui's style for a display scale. Colors are converted to linear values
// when ImGui draws into sRGB targets. Must run between frames, since it changes font sizes.
void applyTheme(const ThemeSettings& settings, float displayScale, bool linearColors);

// The colors of the theme last applied.
[[nodiscard]] const ThemeColors& themeColors() noexcept;
// A color given in sRGB, as ImGui style colors are, converted for the renderer when needed.
[[nodiscard]] ImVec4 uiColor(ImVec4 srgb) noexcept;
[[nodiscard]] ImU32 uiColorU32(ImVec4 srgb) noexcept;

// The fonts of the editor, all of them able to draw the icons.
struct EditorFonts
{
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    // For the console and code.
    ImFont* mono = nullptr;
};

// Loads Noto Sans and JetBrains Mono from the fonts directory, merged with the icons, into ImGui's
// atlas; missing files fall back to ImGui's own font with a warning. The icon set must outlive the
// ImGui context.
[[nodiscard]] EditorFonts loadEditorFonts(const std::filesystem::path& fontsDirectory, IconSet& icons);

// The ImGui size of a font of the editor for a size in points: ImGui sizes cover the whole line.
[[nodiscard]] float regularFontPixels(float points) noexcept;
[[nodiscard]] float monoFontPixels(float points) noexcept;

// The icon and color that stand for an entity in lists, from its components.
struct EntityIcon
{
    IconText icon;
    ImVec4 color;
};
[[nodiscard]] EntityIcon entityIcon(const scene::Scene& scene, scene::Entity entity);
[[nodiscard]] EntityIcon componentIcon(std::string_view componentName);

} // namespace devex::tools::detail
