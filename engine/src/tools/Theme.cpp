#include "Theme.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/AudioComponents.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <imgui_freetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>

namespace devex::tools::detail {
namespace {

using serialization::TextValue;

// ImGui sizes a font by its whole line, from ascender to descender: the ratio to the em size.
constexpr float notoSansLineHeight = 1.362f;
constexpr float jetBrainsMonoLineHeight = 1.32f;

ThemeColors g_colors = deriveThemeColors(ThemeSettings{});
bool g_linearColors = true;

[[nodiscard]] ImVec4 hex(std::uint32_t rgb, float alpha = 1.0f) noexcept
{
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

[[nodiscard]] ImVec4 mix(ImVec4 from, ImVec4 to, float amount) noexcept
{
    const auto channel = [amount](float a, float b) { return std::clamp(a + (b - a) * amount, 0.0f, 1.0f); };
    return {channel(from.x, to.x), channel(from.y, to.y), channel(from.z, to.z), channel(from.w, to.w)};
}

[[nodiscard]] ImVec4 withAlpha(ImVec4 color, float alpha) noexcept
{
    return {color.x, color.y, color.z, alpha};
}

[[nodiscard]] ImVec4 toImVec4(math::Vec3 color) noexcept
{
    return {color.r, color.g, color.b, 1.0f};
}

[[nodiscard]] float luminance(ImVec4 color) noexcept
{
    return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

[[nodiscard]] ImVec4 toLinear(ImVec4 srgb) noexcept
{
    const auto channel = [](float value) {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    return {channel(srgb.x), channel(srgb.y), channel(srgb.z), srgb.w};
}

[[nodiscard]] float numberOr(const serialization::TextSection& section, std::string_view key, float fallback)
{
    const TextValue* const value = section.findAttribute(key);
    const std::optional<double> number = value != nullptr ? serialization::asNumber(*value) : std::nullopt;
    return number && std::isfinite(*number) ? static_cast<float>(*number) : fallback;
}

// Colors are written as "#rrggbb".
[[nodiscard]] std::string formatColor(math::Vec3 color)
{
    const auto byte = [](float value) { return static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
    return std::format("#{:02x}{:02x}{:02x}", byte(color.r), byte(color.g), byte(color.b));
}

[[nodiscard]] std::optional<math::Vec3> parseColor(std::string_view text) noexcept
{
    if (text.size() != 7 || text.front() != '#')
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const char digit : text.substr(1))
    {
        const int nibble = digit >= '0' && digit <= '9'   ? digit - '0'
                           : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                           : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                                          : -1;
        if (nibble < 0)
        {
            return std::nullopt;
        }
        value = value << 4 | static_cast<std::uint32_t>(nibble);
    }
    const ImVec4 color = hex(value);
    return math::Vec3{color.x, color.y, color.z};
}

[[nodiscard]] math::Vec3 colorOr(const serialization::TextSection& section, std::string_view key, math::Vec3 fallback)
{
    const TextValue* const value = section.findAttribute(key);
    const std::string* const text = value != nullptr ? serialization::asString(*value) : nullptr;
    return text != nullptr ? parseColor(*text).value_or(fallback) : fallback;
}

[[nodiscard]] bool isEngineComponent(std::string_view name) noexcept
{
    constexpr std::array<std::string_view, 18> engineComponents{
        "Transform",       "MeshRenderer",     "Camera",       "DirectionalLight",    "PointLight",
        "SpotLight",       "Environment",      "RigidBody",    "BoxCollider",         "SphereCollider",
        "CapsuleCollider", "CylinderCollider", "MeshCollider", "CharacterController", "AudioSource",
        "AudioListener",   "SkinnedMeshRenderer", "Animator"};
    return std::ranges::find(engineComponents, name) != engineComponents.end();
}

} // namespace

const char* displayName(ThemePreset preset) noexcept
{
    switch (preset)
    {
    case ThemePreset::Gray:
        return "Gray";
    case ThemePreset::BlueGray:
        return "Blue gray";
    case ThemePreset::Black:
        return "Black (OLED)";
    case ThemePreset::Light:
        return "Light";
    }
    return "Gray";
}

std::string_view toString(ThemePreset preset) noexcept
{
    switch (preset)
    {
    case ThemePreset::Gray:
        return "gray";
    case ThemePreset::BlueGray:
        return "blue_gray";
    case ThemePreset::Black:
        return "black";
    case ThemePreset::Light:
        return "light";
    }
    return "gray";
}

std::optional<ThemePreset> parseThemePreset(std::string_view text) noexcept
{
    for (const ThemePreset preset : themePresets)
    {
        if (toString(preset) == text)
        {
            return preset;
        }
    }
    return std::nullopt;
}

void ThemeSettings::applyPreset(ThemePreset newPreset) noexcept
{
    preset = newPreset;
    switch (newPreset)
    {
    case ThemePreset::Gray:
        baseColor = {0.149f, 0.149f, 0.149f};
        accentColor = {0.310f, 0.584f, 0.918f};
        contrast = 0.25f;
        break;
    case ThemePreset::BlueGray:
        baseColor = {0.212f, 0.239f, 0.290f};
        accentColor = {0.439f, 0.729f, 0.980f};
        contrast = 0.3f;
        break;
    case ThemePreset::Black:
        baseColor = {0.0f, 0.0f, 0.0f};
        accentColor = {0.271f, 0.639f, 1.0f};
        contrast = 0.0f;
        break;
    case ThemePreset::Light:
        baseColor = {0.925f, 0.929f, 0.937f};
        accentColor = {0.180f, 0.404f, 0.820f};
        contrast = 0.08f;
        break;
    }
}

ThemeSettings readThemeSettings(const serialization::TextSection& section)
{
    ThemeSettings settings;
    const TextValue* const presetValue = section.findAttribute("preset");
    const std::string* const presetText = presetValue != nullptr ? serialization::asString(*presetValue) : nullptr;
    settings.applyPreset(presetText != nullptr ? parseThemePreset(*presetText).value_or(ThemePreset::Gray)
                                               : ThemePreset::Gray);
    settings.baseColor = colorOr(section, "base_color", settings.baseColor);
    settings.accentColor = colorOr(section, "accent_color", settings.accentColor);
    settings.contrast = std::clamp(numberOr(section, "contrast", settings.contrast), -1.0f, 1.0f);
    settings.interfaceScale = numberOr(section, "interface_scale", settings.interfaceScale);
    if (settings.interfaceScale != 0.0f)
    {
        settings.interfaceScale = std::clamp(settings.interfaceScale, 0.5f, 3.0f);
    }
    settings.fontSize = std::clamp(numberOr(section, "font_size", settings.fontSize), 8.0f, 32.0f);
    settings.codeFontSize = std::clamp(numberOr(section, "code_font_size", settings.codeFontSize), 8.0f, 32.0f);
    return settings;
}

serialization::TextSection writeThemeSettings(const ThemeSettings& settings)
{
    serialization::TextSection section;
    section.type = "theme";
    section.attributes = {
        {"preset", TextValue(std::string(toString(settings.preset)))},
        {"base_color", TextValue(formatColor(settings.baseColor))},
        {"accent_color", TextValue(formatColor(settings.accentColor))},
        {"contrast", TextValue(static_cast<double>(settings.contrast))},
        {"interface_scale", TextValue(static_cast<double>(settings.interfaceScale))},
        {"font_size", TextValue(static_cast<double>(settings.fontSize))},
        {"code_font_size", TextValue(static_cast<double>(settings.codeFontSize))},
    };
    return section;
}

float effectiveInterfaceScale(const ThemeSettings& settings, float displayScale) noexcept
{
    const float scale = settings.interfaceScale > 0.0f ? settings.interfaceScale : displayScale;
    return std::clamp(std::isfinite(scale) ? scale : 1.0f, 0.5f, 3.0f);
}

ThemeColors deriveThemeColors(const ThemeSettings& settings)
{
    const ImVec4 base = toImVec4(settings.baseColor);
    const ImVec4 black = hex(0x000000);
    const ImVec4 white = hex(0xFFFFFF);
    const float contrast = settings.contrast;

    ThemeColors colors;
    colors.dark = luminance(base) < 0.5f;
    const ImVec4 mono = colors.dark ? white : black;
    colors.accent = toImVec4(settings.accentColor);
    colors.text = mix(mono, base, colors.dark ? 0.18f : 0.12f);
    colors.textDim = mix(mono, base, 0.5f);
    colors.panel = base;
    colors.outer = mix(base, black, contrast * 1.6f);
    // On a black base, darker backgrounds do not exist: fields stand out by being lighter.
    colors.field = colors.dark ? mix(mix(base, black, contrast), mono, luminance(base) < 0.02f ? 0.07f : 0.0f)
                               : mix(base, white, 0.75f);
    colors.border = withAlpha(mono, colors.dark ? 0.09f : 0.14f);

    if (colors.dark)
    {
        colors.success = hex(0x74d68a);
        colors.warning = hex(0xf2c46b);
        colors.error = hex(0xf27272);
    }
    else
    {
        colors.success = hex(0x2f9a4a);
        colors.warning = hex(0xb57a14);
        colors.error = hex(0xcc3d3d);
    }
    colors.axisX = hex(0xf05a5a);
    colors.axisY = hex(0x7ccf48);
    colors.axisZ = hex(0x4f8ff0);

    const std::array<std::pair<ImVec4*, std::uint32_t>, 15> icons{{
        {&colors.physics, 0x72d6c6},
        {&colors.audio, 0xf49ac1},
        {&colors.animation, 0xc3a6f5},
        {&colors.entity, 0xfc7f7f},
        {&colors.light, 0xffd166},
        {&colors.camera, 0xc49af2},
        {&colors.environment, 0x6fd1f7},
        {&colors.gameCode, 0x8eef97},
        {&colors.folder, 0x7fb5f5},
        {&colors.scene, 0xe6e6e6},
        {&colors.prefab, 0x72b4ff},
        {&colors.material, 0xffa86b},
        {&colors.texture, 0xa5efac},
        {&colors.neutral, 0xbdbdbd},
        {&colors.favorite, 0xffcc4d},
    }};
    for (const auto& [color, rgb] : icons)
    {
        // Pale colors read on dark backgrounds only.
        *color = colors.dark ? hex(rgb) : mix(hex(rgb), black, 0.4f);
    }

    // Syntax colors, in the spirit of the icon colors: pale on dark, deepened on light.
    const std::array<std::pair<ImVec4*, std::uint32_t>, 7> code{{
        {&colors.codeKeyword, 0x88b9f2},
        {&colors.codeType, 0x6fd1c0},
        {&colors.codeComment, 0x7f9163},
        {&colors.codeString, 0xd99a6c},
        {&colors.codeNumber, 0xb5cea8},
        {&colors.codeDirective, 0xc9a0e0},
        {&colors.codePunctuation, 0xb8b8b8},
    }};
    for (const auto& [color, rgb] : code)
    {
        *color = colors.dark ? hex(rgb) : mix(hex(rgb), black, 0.45f);
    }
    return colors;
}

void applyTheme(const ThemeSettings& settings, float displayScale, bool linearColors)
{
    const ThemeColors colors = deriveThemeColors(settings);
    const float scale = effectiveInterfaceScale(settings, displayScale);
    const ImVec4 mono = colors.dark ? hex(0xFFFFFF) : hex(0x000000);
    const ImVec4 none{0.0f, 0.0f, 0.0f, 0.0f};
    const ImVec4 raised = mix(colors.panel, mono, colors.dark ? 0.075f : 0.06f);
    const ImVec4 popup = colors.dark ? mix(colors.panel, hex(0x000000), std::max(settings.contrast, 0.0f) * 0.5f)
                                     : mix(colors.panel, hex(0xFFFFFF), 0.6f);

    ImGuiStyle style;
    style.WindowPadding = {8.0f, 8.0f};
    style.FramePadding = {7.0f, 4.0f};
    style.CellPadding = {6.0f, 3.0f};
    style.ItemSpacing = {7.0f, 5.0f};
    style.ItemInnerSpacing = {5.0f, 4.0f};
    style.IndentSpacing = 24.0f;
    style.ScrollbarSize = 11.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 3.0f;
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 0.0f;
    style.TabBarOverlineSize = 2.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = {14.0f, 3.0f};
    style.DockingSeparatorSize = 5.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.WindowTitleAlign = {0.5f, 0.5f};
    style.ColorButtonPosition = ImGuiDir_Right;
    style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    style.TreeLinesSize = 1.0f;

    ImVec4* const c = style.Colors;
    c[ImGuiCol_Text] = colors.text;
    c[ImGuiCol_TextDisabled] = colors.textDim;
    c[ImGuiCol_WindowBg] = colors.panel;
    c[ImGuiCol_ChildBg] = none;
    c[ImGuiCol_PopupBg] = popup;
    c[ImGuiCol_Border] = colors.border;
    c[ImGuiCol_BorderShadow] = none;
    c[ImGuiCol_FrameBg] = colors.field;
    c[ImGuiCol_FrameBgHovered] = mix(colors.field, mono, 0.05f);
    c[ImGuiCol_FrameBgActive] = mix(colors.field, mono, 0.08f);
    c[ImGuiCol_TitleBg] = colors.outer;
    c[ImGuiCol_TitleBgActive] = colors.outer;
    c[ImGuiCol_TitleBgCollapsed] = colors.outer;
    c[ImGuiCol_MenuBarBg] = colors.outer;
    c[ImGuiCol_ScrollbarBg] = none;
    c[ImGuiCol_ScrollbarGrab] = withAlpha(mono, 0.16f);
    c[ImGuiCol_ScrollbarGrabHovered] = withAlpha(mono, 0.26f);
    c[ImGuiCol_ScrollbarGrabActive] = withAlpha(colors.accent, 0.8f);
    c[ImGuiCol_CheckMark] = colors.accent;
    c[ImGuiCol_SliderGrab] = withAlpha(colors.accent, 0.85f);
    c[ImGuiCol_SliderGrabActive] = colors.accent;
    c[ImGuiCol_Button] = raised;
    c[ImGuiCol_ButtonHovered] = mix(raised, mono, 0.07f);
    c[ImGuiCol_ButtonActive] = mix(raised, colors.accent, 0.35f);
    c[ImGuiCol_Header] = withAlpha(colors.accent, 0.30f);
    c[ImGuiCol_HeaderHovered] = withAlpha(mono, 0.07f);
    c[ImGuiCol_HeaderActive] = withAlpha(colors.accent, 0.40f);
    c[ImGuiCol_Separator] = colors.border;
    c[ImGuiCol_SeparatorHovered] = withAlpha(colors.accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = colors.accent;
    c[ImGuiCol_ResizeGrip] = none;
    c[ImGuiCol_ResizeGripHovered] = withAlpha(colors.accent, 0.5f);
    c[ImGuiCol_ResizeGripActive] = colors.accent;
    c[ImGuiCol_InputTextCursor] = colors.text;
    c[ImGuiCol_TabHovered] = withAlpha(mono, 0.08f);
    c[ImGuiCol_Tab] = none;
    c[ImGuiCol_TabSelected] = colors.panel;
    c[ImGuiCol_TabSelectedOverline] = colors.accent;
    c[ImGuiCol_TabDimmed] = none;
    c[ImGuiCol_TabDimmedSelected] = colors.panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = none;
    c[ImGuiCol_DockingPreview] = withAlpha(colors.accent, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = colors.outer;
    c[ImGuiCol_PlotLines] = colors.accent;
    c[ImGuiCol_PlotLinesHovered] = colors.warning;
    c[ImGuiCol_PlotHistogram] = colors.accent;
    c[ImGuiCol_PlotHistogramHovered] = colors.warning;
    c[ImGuiCol_TableHeaderBg] = mix(colors.panel, colors.outer, 0.5f);
    c[ImGuiCol_TableBorderStrong] = colors.border;
    c[ImGuiCol_TableBorderLight] = withAlpha(mono, 0.05f);
    c[ImGuiCol_TableRowBg] = none;
    c[ImGuiCol_TableRowBgAlt] = withAlpha(mono, 0.025f);
    c[ImGuiCol_TextLink] = colors.accent;
    c[ImGuiCol_TextSelectedBg] = withAlpha(colors.accent, 0.35f);
    c[ImGuiCol_TreeLines] = withAlpha(mono, 0.14f);
    c[ImGuiCol_DragDropTarget] = colors.accent;
    c[ImGuiCol_DragDropTargetBg] = withAlpha(colors.accent, 0.10f);
    c[ImGuiCol_UnsavedMarker] = colors.text;
    c[ImGuiCol_NavCursor] = colors.accent;
    c[ImGuiCol_NavWindowingHighlight] = withAlpha(hex(0xFFFFFF), 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = withAlpha(hex(0x000000), 0.2f);
    c[ImGuiCol_ModalWindowDimBg] = withAlpha(hex(0x000000), 0.45f);
    if (linearColors)
    {
        for (int index = 0; index < ImGuiCol_COUNT; ++index)
        {
            c[index] = toLinear(c[index]);
        }
    }

    style.ScaleAllSizes(scale);
    style.FontSizeBase = regularFontPixels(settings.fontSize);
    style.FontScaleDpi = scale;
    ImGui::GetStyle() = style;

    g_colors = colors;
    g_linearColors = linearColors;
}

const ThemeColors& themeColors() noexcept
{
    return g_colors;
}

ImVec4 uiColor(ImVec4 srgb) noexcept
{
    return g_linearColors ? toLinear(srgb) : srgb;
}

ImU32 uiColorU32(ImVec4 srgb) noexcept
{
    return ImGui::GetColorU32(uiColor(srgb));
}

float regularFontPixels(float points) noexcept
{
    return std::round(points * notoSansLineHeight);
}

float monoFontPixels(float points) noexcept
{
    return std::round(points * jetBrainsMonoLineHeight);
}

EditorFonts loadEditorFonts(const std::filesystem::path& fontsDirectory, IconSet& icons)
{
    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    // Glyphs keep their shapes, snapped to pixels vertically only, as on Windows.
    atlas.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

    const auto add = [&](const char* fileName) -> ImFont* {
        const std::filesystem::path file = fontsDirectory / fileName;
        ImFont* font = nullptr;
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error))
        {
            ImFontConfig config;
            std::snprintf(config.Name, sizeof(config.Name), "%s", fileName);
            font = atlas.AddFontFromFileTTF(core::toUtf8(file).c_str(), 0.0f, &config);
        }
        if (font == nullptr)
        {
            DEVEX_LOG_WARNING("Cannot load the font {}: the editor uses ImGui's font instead", core::toUtf8(file));
            font = atlas.AddFontDefault();
        }
        ImFontConfig iconSource = iconFontSource(icons);
        iconSource.DstFont = font;
        atlas.AddFont(&iconSource);
        return font;
    };

    EditorFonts fonts;
    fonts.regular = add("NotoSans-Regular.ttf");
    fonts.bold = add("NotoSans-Bold.ttf");
    fonts.mono = add("JetBrainsMono-Regular.ttf");
    ImGui::GetIO().FontDefault = fonts.regular;
    return fonts;
}

EntityIcon entityIcon(const scene::Scene& scene, scene::Entity entity)
{
    const ThemeColors& colors = themeColors();
    if (const scene::PrefabInstance* const instance = scene.tryGet<scene::PrefabInstance>(entity))
    {
        return {icons::Package, instance->resolved ? colors.prefab : colors.error};
    }
    if (scene.has<scene::Camera>(entity))
    {
        return {icons::Video, colors.camera};
    }
    if (scene.has<scene::DirectionalLight>(entity))
    {
        return {icons::Sun, colors.light};
    }
    if (scene.has<scene::SpotLight>(entity))
    {
        return {icons::Flashlight, colors.light};
    }
    if (scene.has<scene::PointLight>(entity))
    {
        return {icons::Lightbulb, colors.light};
    }
    if (scene.has<scene::Environment>(entity))
    {
        return {icons::CloudSun, colors.environment};
    }
    if (scene.has<scene::Animator>(entity))
    {
        return {icons::Film, colors.animation};
    }
    if (scene.has<scene::SkinnedMeshRenderer>(entity))
    {
        return {icons::Bone, colors.animation};
    }
    if (scene.has<scene::AudioSource>(entity) && !scene.has<scene::MeshRenderer>(entity))
    {
        return {icons::Volume, colors.audio};
    }
    if (scene.has<scene::AudioListener>(entity))
    {
        return {icons::Ear, colors.audio};
    }
    if (scene.has<scene::CharacterController>(entity))
    {
        return {icons::PersonStanding, colors.physics};
    }
    if (scene.has<scene::MeshRenderer>(entity))
    {
        return {icons::Box, colors.entity};
    }
    if (scene.has<scene::RigidBody>(entity))
    {
        return {icons::Weight, colors.physics};
    }
    if (scene.has<scene::BoxCollider>(entity) || scene.has<scene::SphereCollider>(entity) ||
        scene.has<scene::CapsuleCollider>(entity) || scene.has<scene::CylinderCollider>(entity) ||
        scene.has<scene::MeshCollider>(entity))
    {
        return {icons::SquareDashed, colors.physics};
    }
    // Components of game code, loaded or kept as text.
    if (scene.has<scene::PreservedComponents>(entity))
    {
        return {icons::Puzzle, colors.gameCode};
    }
    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        if (!isEngineComponent(type.name()) && type.find(scene, entity) != nullptr)
        {
            return {icons::Puzzle, colors.gameCode};
        }
    }
    if (scene.firstChild(entity).isValid())
    {
        return {icons::Package, colors.entity};
    }
    if (scene.has<scene::Transform>(entity))
    {
        return {icons::Axis, colors.entity};
    }
    return {icons::Circle, colors.neutral};
}

EntityIcon componentIcon(std::string_view componentName)
{
    const ThemeColors& colors = themeColors();
    if (componentName == "Transform")
    {
        return {icons::Move3d, colors.entity};
    }
    if (componentName == "MeshRenderer")
    {
        return {icons::Box, colors.entity};
    }
    if (componentName == "Camera")
    {
        return {icons::Video, colors.camera};
    }
    if (componentName == "DirectionalLight")
    {
        return {icons::Sun, colors.light};
    }
    if (componentName == "PointLight")
    {
        return {icons::Lightbulb, colors.light};
    }
    if (componentName == "SpotLight")
    {
        return {icons::Flashlight, colors.light};
    }
    if (componentName == "Environment")
    {
        return {icons::CloudSun, colors.environment};
    }
    if (componentName == "RigidBody")
    {
        return {icons::Weight, colors.physics};
    }
    if (componentName == "BoxCollider")
    {
        return {icons::SquareDashed, colors.physics};
    }
    if (componentName == "SphereCollider")
    {
        return {icons::CircleDashed, colors.physics};
    }
    if (componentName == "CapsuleCollider" || componentName == "CylinderCollider")
    {
        return {icons::Cylinder, colors.physics};
    }
    if (componentName == "MeshCollider")
    {
        return {icons::Shapes, colors.physics};
    }
    if (componentName == "CharacterController")
    {
        return {icons::PersonStanding, colors.physics};
    }
    if (componentName == "Animator")
    {
        return {icons::Film, colors.animation};
    }
    if (componentName == "SkinnedMeshRenderer")
    {
        return {icons::Bone, colors.animation};
    }
    if (componentName == "AudioSource")
    {
        return {icons::Volume, colors.audio};
    }
    if (componentName == "AudioListener")
    {
        return {icons::Ear, colors.audio};
    }
    return {icons::Puzzle, colors.gameCode};
}

} // namespace devex::tools::detail
