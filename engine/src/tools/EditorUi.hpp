#pragma once

#include "Icons.hpp"
#include "Theme.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/asset/FontData.hpp>
#include <devex/asset/ThemeData.hpp>
#include <devex/core/Time.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/ui/UiWorld.hpp>

#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

// The panels of the editor made with the interface of the engine, as Godot draws its editor with the
// nodes its games use. They live beside the ImGui panels until the dock itself moves over.
namespace devex::tools::detail {

// A color of the editor theme, written in sRGB as ImGui's are, as the linear color of the interface
// of the engine.
[[nodiscard]] math::Vec4 linearColor(ImVec4 srgb) noexcept;

// What the panels share: the fonts of the editor baked into atlases of distances, its icons drawn as
// white textures that the images showing them tint, and its theme as the named styles of a theme of
// that interface.
class EditorUiKit
{
public:
    EditorUiKit(render::Renderer& renderer, const IconSet& icons, std::filesystem::path fontsDirectory);
    ~EditorUiKit();

    EditorUiKit(const EditorUiKit&) = delete;
    EditorUiKit& operator=(const EditorUiKit&) = delete;

    // Identifiers only the kit resolves: the fonts, the theme, and the icons.
    [[nodiscard]] static asset::AssetId regularFont() noexcept;
    [[nodiscard]] static asset::AssetId boldFont() noexcept;
    [[nodiscard]] static asset::AssetId themeId() noexcept;
    [[nodiscard]] asset::AssetId icon(Icon icon);

    // What an interface world and its drawing ask for.
    [[nodiscard]] std::function<ui::FontRef(asset::AssetId)> fonts();
    [[nodiscard]] ui::ThemeSource themes();
    [[nodiscard]] ui::DrawContext drawContext();
    // The letters of a font, to measure a text before placing it; null until the fonts are baked.
    [[nodiscard]] const asset::FontData* fontData(asset::AssetId font);
    // The width of a line of text, in the units of the panels.
    [[nodiscard]] float textWidth(asset::AssetId font, std::string_view text, float size);

    // The styles follow the colors of the editor, made again when they change.
    void refreshTheme(const ThemeColors& colors);

private:
    struct BakedFont
    {
        std::shared_ptr<const asset::FontData> data;
        render::TextureHandle atlas;
    };

    void bakeFonts();

    render::Renderer& m_renderer;
    const IconSet& m_icons;
    std::filesystem::path m_fontsDirectory;
    bool m_fontsBaked = false;
    BakedFont m_regular;
    BakedFont m_bold;
    std::unordered_map<asset::AssetId, render::TextureHandle> m_iconTextures;
    std::shared_ptr<asset::ThemeData> m_theme;
    ImVec4 m_themeAccent{-1.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 m_themePanel{-1.0f, 0.0f, 0.0f, 0.0f};
};

// A panel made with the interface of the engine: a canvas of entities in a scene of its own, which an
// interface world lays out and answers, drawn into an image that the ImGui window around it shows.
// The panel works in units of the size of the editor's text, so that it follows the interface scale.
class UiPanel
{
public:
    explicit UiPanel(std::uint32_t surface);

    [[nodiscard]] scene::Scene& scene() noexcept;
    [[nodiscard]] ui::UiWorld& world() noexcept;
    [[nodiscard]] scene::Entity canvas() const noexcept;
    // The size of the panel in its units, as the last update laid it out.
    [[nodiscard]] math::Vec2 size() const noexcept;
    // Whether the ImGui window it stands in has the keyboard.
    [[nodiscard]] bool focused() const noexcept;

    // Inside the ImGui window it fills: takes the room left in it, gives the interface world the
    // mouse and the keys the window receives, and shows the image of the panel. `zoom` is how many
    // pixels one unit of the panel takes.
    void update(EditorUiKit& kit, core::Duration delta, float zoom);
    // Adds the image of the panel to the frame, over a color, when the last update showed it.
    void render(EditorUiKit& kit, render::RenderWorld& world, math::Vec4 background);

private:
    scene::Scene m_scene;
    ui::UiWorld m_world;
    scene::Entity m_canvas;
    std::uint32_t m_surface = 0;
    math::Extent2D m_pixels;
    float m_zoom = 1.0f;
    bool m_shown = false;
    bool m_focused = false;
    bool m_connected = false;
    // Where the last frame was drawn, kept to reuse its storage.
    render::RenderWorld m_scratch;
};

} // namespace devex::tools::detail
