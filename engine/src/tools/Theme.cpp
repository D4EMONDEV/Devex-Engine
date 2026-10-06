#include "Theme.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Light2DComponents.hpp>
#include <devex/scene/ParticleComponents.hpp>
#include <devex/scene/SpriteComponents.hpp>
#include <devex/scene/TilemapComponents.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/scene/AudioComponents.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/scene/Physics2DComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>

namespace devex::tools::detail {
namespace {

using serialization::TextValue;

// The whole line of a font, from ascender to descender: the ratio to its em size.
constexpr float notoSansLineHeight = 1.362f;
constexpr float jetBrainsMonoLineHeight = 1.32f;

ThemeColors g_colors = deriveThemeColors(ThemeSettings{});
ThemeMetrics g_metrics = deriveThemeMetrics(ThemeSettings{}, 1.0f);

[[nodiscard]] math::Vec4 hex(std::uint32_t rgb, float alpha = 1.0f) noexcept
{
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

[[nodiscard]] math::Vec4 mix(math::Vec4 from, math::Vec4 to, float amount) noexcept
{
    const auto channel = [amount](float a, float b) { return std::clamp(a + (b - a) * amount, 0.0f, 1.0f); };
    return {channel(from.x, to.x), channel(from.y, to.y), channel(from.z, to.z), channel(from.w, to.w)};
}

[[nodiscard]] math::Vec4 withAlpha(math::Vec4 color, float alpha) noexcept
{
    return {color.x, color.y, color.z, alpha};
}

[[nodiscard]] math::Vec4 opaque(math::Vec3 color) noexcept
{
    return {color.r, color.g, color.b, 1.0f};
}

[[nodiscard]] float luminance(math::Vec4 color) noexcept
{
    return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
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
    const math::Vec4 color = hex(value);
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
    constexpr std::array<std::string_view, 65> engineComponents{
        "Transform",       "MeshRenderer",        "Camera",       "DirectionalLight",
        "PointLight",      "SpotLight",           "Environment",  "RigidBody",
        "BoxCollider",     "SphereCollider",      "CapsuleCollider", "CylinderCollider",
        "MeshCollider",    "CharacterController", "AudioSource",  "AudioListener",
        "SkinnedMeshRenderer", "Animator",        "Canvas",       "UiRect",
        "UiImage",         "UiText",              "UiButton",     "UiLayout",
        "UiInput",         "UiScroll",            "UiSlider",     "UiToggle",
        "UiBinding",       "Tweener",             "ParticleEmitter", "TrailRenderer",
        "SpriteRenderer",  "SpriteAnimator",      "Tilemap",      "RigidBody2D",
        "BoxCollider2D",   "CircleCollider2D",    "CapsuleCollider2D", "PolygonCollider2D",
        "TilemapCollider2D", "CharacterController2D", "NavMeshSurface", "NavMeshAgent", "NavMeshObstacle",
        "UiPopup",         "UiContextMenu",       "UiTooltip",    "UiDropdown",
        "UiSplitter",      "UiFoldout",           "UiVirtualList", "UiTable",
        "UiTableRow",      "UiDragSource",        "UiDropTarget", "UiNumberField",
        "UiColorPicker",   "UiPlot",              "UiLine",       "UiTextArea",
        "PointLight2D",    "DirectionalLight2D",  "LightOccluder2D", "CanvasModulate"};
    return std::ranges::find(engineComponents, name) != engineComponents.end();
}

} // namespace

bool isGameComponent(std::string_view name) noexcept
{
    return !isEngineComponent(name);
}

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
    if (const TextValue* const value = section.findAttribute("update_continuously"))
    {
        settings.updateContinuously = serialization::asBool(*value).value_or(false);
    }
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
        {"update_continuously", TextValue(settings.updateContinuously)},
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
    const math::Vec4 base = opaque(settings.baseColor);
    const math::Vec4 black = hex(0x000000);
    const math::Vec4 white = hex(0xFFFFFF);
    const float contrast = settings.contrast;

    ThemeColors colors;
    colors.dark = luminance(base) < 0.5f;
    const math::Vec4 mono = colors.dark ? white : black;
    colors.accent = opaque(settings.accentColor);
    colors.text = mix(mono, base, colors.dark ? 0.18f : 0.12f);
    colors.textDim = mix(mono, base, 0.5f);
    colors.panel = base;
    colors.outer = mix(base, black, contrast * 1.6f);
    // On a black base, darker backgrounds do not exist: fields stand out by being lighter.
    colors.field = colors.dark ? mix(mix(base, black, contrast), mono, luminance(base) < 0.02f ? 0.07f : 0.0f)
                               : mix(base, white, 0.75f);
    colors.border = withAlpha(mono, colors.dark ? 0.09f : 0.14f);
    colors.raised = mix(colors.panel, mono, colors.dark ? 0.075f : 0.06f);
    colors.popup = colors.dark ? mix(colors.panel, black, std::max(contrast, 0.0f) * 0.5f) : mix(colors.panel, white, 0.6f);

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

    const std::array<std::pair<math::Vec4*, std::uint32_t>, 16> icons{{
        {&colors.physics, 0x72d6c6},
        {&colors.audio, 0xf49ac1},
        {&colors.animation, 0xc3a6f5},
        {&colors.interface, 0x9fd3ff},
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
    const std::array<std::pair<math::Vec4*, std::uint32_t>, 7> code{{
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

ThemeMetrics deriveThemeMetrics(const ThemeSettings& settings, float displayScale) noexcept
{
    ThemeMetrics metrics;
    const float scale = effectiveInterfaceScale(settings, displayScale);
    const float line = regularFontPixels(settings.fontSize) * scale;
    const float margin = 4.0f * scale;
    metrics.scale = scale;
    metrics.lineHeight = line;
    metrics.margin = margin;
    // The flat buttons of the menus, with Godot's top bar separation around them.
    metrics.menuBarHeight = std::round(line + margin * 1.8f + margin * 2.0f);
    // The flat buttons of the bottom panels and a margin.
    metrics.statusBarHeight = std::round(line + margin * 1.8f + margin);
    // Godot's tabs: a line and one and a half margins above and below.
    metrics.tabHeight = std::round(line + margin * 3.0f);
    metrics.dockGap = std::round(margin);
    metrics.panelPadding = std::round(margin);
    metrics.titleHeight = std::round(line + margin * 1.5f);
    return metrics;
}

void applyTheme(const ThemeSettings& settings, float displayScale)
{
    g_colors = deriveThemeColors(settings);
    g_metrics = deriveThemeMetrics(settings, displayScale);
}

const ThemeColors& themeColors() noexcept
{
    return g_colors;
}

const ThemeMetrics& themeMetrics() noexcept
{
    return g_metrics;
}

float regularFontPixels(float points) noexcept
{
    return std::round(points * notoSansLineHeight);
}

float monoFontPixels(float points) noexcept
{
    return std::round(points * jetBrainsMonoLineHeight);
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
    if (scene.has<scene::PointLight>(entity) || scene.has<scene::PointLight2D>(entity))
    {
        return {icons::Lightbulb, colors.light};
    }
    if (scene.has<scene::DirectionalLight2D>(entity))
    {
        return {icons::Sun, colors.light};
    }
    if (scene.has<scene::CanvasModulate>(entity))
    {
        return {icons::Palette, colors.environment};
    }
    if (scene.has<scene::Environment>(entity))
    {
        return {icons::CloudSun, colors.environment};
    }
    if (scene.has<scene::Animator>(entity))
    {
        return {icons::Film, colors.animation};
    }
    if (scene.has<scene::ParticleEmitter>(entity))
    {
        return {icons::Sparkles, colors.light};
    }
    if (scene.has<scene::SpriteRenderer>(entity))
    {
        return {icons::Image, colors.texture};
    }
    if (scene.has<scene::Tilemap>(entity))
    {
        return {icons::Grid, colors.texture};
    }
    if (scene.has<scene::LightOccluder2D>(entity))
    {
        return {icons::Shapes, colors.light};
    }
    if (scene.has<scene::Canvas>(entity))
    {
        return {icons::Monitor, colors.interface};
    }
    if (scene.has<scene::UiButton>(entity))
    {
        return {icons::Pointer, colors.interface};
    }
    if (scene.has<scene::UiText>(entity))
    {
        return {icons::Type, colors.interface};
    }
    if (scene.has<scene::UiLayout>(entity))
    {
        return {icons::LayoutDashboard, colors.interface};
    }
    if (scene.has<scene::UiImage>(entity) || scene.has<scene::UiRect>(entity))
    {
        return {icons::Square, colors.interface};
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
    if (scene.has<scene::CharacterController>(entity) || scene.has<scene::CharacterController2D>(entity))
    {
        return {icons::PersonStanding, colors.physics};
    }
    if (scene.has<scene::NavMeshSurface>(entity) || scene.has<scene::NavMeshAgent>(entity))
    {
        return {icons::Footprints, colors.physics};
    }
    if (scene.has<scene::MeshRenderer>(entity))
    {
        return {icons::Box, colors.entity};
    }
    if (scene.has<scene::RigidBody>(entity) || scene.has<scene::RigidBody2D>(entity))
    {
        return {icons::Weight, colors.physics};
    }
    if (scene.has<scene::BoxCollider>(entity) || scene.has<scene::SphereCollider>(entity) ||
        scene.has<scene::CapsuleCollider>(entity) || scene.has<scene::CylinderCollider>(entity) ||
        scene.has<scene::MeshCollider>(entity) || scene.has<scene::BoxCollider2D>(entity) ||
        scene.has<scene::CircleCollider2D>(entity) || scene.has<scene::CapsuleCollider2D>(entity) ||
        scene.has<scene::PolygonCollider2D>(entity))
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
    if (componentName == "DirectionalLight" || componentName == "DirectionalLight2D")
    {
        return {icons::Sun, colors.light};
    }
    if (componentName == "PointLight" || componentName == "PointLight2D")
    {
        return {icons::Lightbulb, colors.light};
    }
    if (componentName == "LightOccluder2D")
    {
        return {icons::Shapes, colors.light};
    }
    if (componentName == "CanvasModulate")
    {
        return {icons::Palette, colors.environment};
    }
    if (componentName == "SpotLight")
    {
        return {icons::Flashlight, colors.light};
    }
    if (componentName == "Environment")
    {
        return {icons::CloudSun, colors.environment};
    }
    if (componentName == "RigidBody" || componentName == "RigidBody2D")
    {
        return {icons::Weight, colors.physics};
    }
    if (componentName == "BoxCollider" || componentName == "BoxCollider2D")
    {
        return {icons::SquareDashed, colors.physics};
    }
    if (componentName == "SphereCollider" || componentName == "CircleCollider2D")
    {
        return {icons::CircleDashed, colors.physics};
    }
    if (componentName == "CapsuleCollider" || componentName == "CylinderCollider" || componentName == "CapsuleCollider2D")
    {
        return {icons::Cylinder, colors.physics};
    }
    if (componentName == "MeshCollider" || componentName == "PolygonCollider2D")
    {
        return {icons::Shapes, colors.physics};
    }
    if (componentName == "CharacterController" || componentName == "CharacterController2D")
    {
        return {icons::PersonStanding, colors.physics};
    }
    if (componentName == "Animator")
    {
        return {icons::Film, colors.animation};
    }
    if (componentName == "Tweener")
    {
        return {icons::Activity, colors.animation};
    }
    if (componentName == "ParticleEmitter")
    {
        return {icons::Sparkles, colors.light};
    }
    if (componentName == "TrailRenderer")
    {
        return {icons::Sparkles, colors.animation};
    }
    if (componentName == "SpriteRenderer")
    {
        return {icons::Image, colors.texture};
    }
    if (componentName == "SpriteAnimator")
    {
        return {icons::Clapperboard, colors.animation};
    }
    if (componentName == "Tilemap")
    {
        return {icons::Grid, colors.texture};
    }
    if (componentName == "TilemapCollider2D")
    {
        return {icons::Grid, colors.physics};
    }
    if (componentName == "NavMeshSurface" || componentName == "NavMeshAgent")
    {
        return {icons::Footprints, colors.physics};
    }
    if (componentName == "NavMeshObstacle")
    {
        return {icons::SquareDashed, colors.physics};
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
    if (componentName == "Canvas")
    {
        return {icons::Monitor, colors.interface};
    }
    if (componentName == "UiRect")
    {
        return {icons::Square, colors.interface};
    }
    if (componentName == "UiImage")
    {
        return {icons::Image, colors.interface};
    }
    if (componentName == "UiText")
    {
        return {icons::Type, colors.interface};
    }
    if (componentName == "UiButton")
    {
        return {icons::Pointer, colors.interface};
    }
    if (componentName == "UiLayout")
    {
        return {icons::LayoutDashboard, colors.interface};
    }
    return {icons::Puzzle, colors.gameCode};
}

} // namespace devex::tools::detail
