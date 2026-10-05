#pragma once

#include "EditorHosts.hpp"
#include "EditorInput.hpp"
#include "Icons.hpp"
#include "Theme.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/asset/FontData.hpp>
#include <devex/asset/ThemeData.hpp>
#include <devex/core/Time.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/UiWorld.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// The panels of the editor made with the interface of the engine, as Godot draws its editor with the
// nodes its games use.
namespace devex::tools::detail {

// A color of the editor theme, written in sRGB, as the linear color of the interface of the engine.
[[nodiscard]] math::Vec4 linearColor(math::Vec4 srgb) noexcept;

// The icon a text of one icon writes.
[[nodiscard]] Icon iconOf(IconText text) noexcept;

// What the panels share: the fonts of the editor baked into atlases of distances, its icons drawn as
// white textures that the images showing them tint, and its theme as the named styles of a theme of
// that interface.
// What the pointer carries from a panel to another, or to the view: a type, the bytes it names, and
// what it is called.
struct EditorDrag
{
    std::string type;
    std::vector<std::byte> payload;
    // Shown next to the pointer once it leaves the panel.
    std::string label;

    [[nodiscard]] bool is(std::string_view kind, std::size_t bytes) const noexcept
    {
        return type == kind && payload.size() == bytes;
    }
};

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
    // The letters of code and of the output, all as wide.
    [[nodiscard]] static asset::AssetId monoFont() noexcept;
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

    // The textures of the project and the sprites cut from them, which the previews of the panels show.
    void setAssetImages(std::function<render::TextureHandle(asset::AssetId)> textures,
                        std::function<math::Vec2(asset::AssetId)> sizes,
                        std::function<std::optional<ui::SpriteImage>(asset::AssetId)> sprites);
    // A checkerboard, drawn behind what may be transparent.
    [[nodiscard]] asset::AssetId checker();

    // A tooltip a panel too small for it asks to be shown outside its image, where the pointer rested
    // on the screen; the layer over the editor shows the last one asked for in the frame.
    struct Tooltip
    {
        std::string text;
        math::Vec2 at{0.0f, 0.0f};
    };
    void showTooltip(std::string text, math::Vec2 at);
    [[nodiscard]] std::optional<Tooltip> takeTooltip() noexcept;

    // The places, the devices and the platform of the frame, which the panels read.
    void setFrame(EditorHosts& hosts, const EditorInput& input, platform::Platform& platform) noexcept;
    [[nodiscard]] EditorHosts& hosts() const noexcept;
    [[nodiscard]] const EditorInput& input() const noexcept;
    [[nodiscard]] platform::Platform& platform() const noexcept;
    // What the pointer carries from a panel, while the button holds it; forgotten the frame after it
    // is let go.
    void carry(EditorDrag drag);
    [[nodiscard]] const EditorDrag* carried() const noexcept;
    void forgetCarried() noexcept;
    // A field that takes what is typed, where it stands on the screen: the input method of the system
    // opens next to it.
    void requestTextInput(math::Vec2 min, math::Vec2 max) noexcept;
    [[nodiscard]] std::optional<std::pair<math::Vec2, math::Vec2>> takeTextInput() noexcept;

private:
    struct BakedFont
    {
        std::shared_ptr<const asset::FontData> data;
        render::TextureHandle atlas;
    };

    void bakeFonts();
    [[nodiscard]] const BakedFont& baked(asset::AssetId font) const noexcept;

    render::Renderer& m_renderer;
    const IconSet& m_icons;
    std::filesystem::path m_fontsDirectory;
    bool m_fontsBaked = false;
    BakedFont m_regular;
    BakedFont m_bold;
    BakedFont m_mono;
    std::unordered_map<asset::AssetId, render::TextureHandle> m_iconTextures;
    render::TextureHandle m_checker;
    std::function<render::TextureHandle(asset::AssetId)> m_assetTextures;
    std::function<math::Vec2(asset::AssetId)> m_assetSizes;
    std::function<std::optional<ui::SpriteImage>(asset::AssetId)> m_assetSprites;
    std::shared_ptr<asset::ThemeData> m_theme;
    // Finds a text among the widths kept without making a string of it.
    struct TextHash
    {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };
    // The widths already measured, by font and size then by text: the panels ask for the same ones at
    // every frame.
    std::unordered_map<std::uint64_t, std::unordered_map<std::string, float, TextHash, std::equal_to<>>> m_widths;
    math::Vec4 m_themeAccent{-1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4 m_themePanel{-1.0f, 0.0f, 0.0f, 0.0f};
    std::optional<Tooltip> m_tooltip;
    EditorHosts* m_hosts = nullptr;
    const EditorInput* m_input = nullptr;
    platform::Platform* m_platform = nullptr;
    std::optional<EditorDrag> m_carried;
    std::optional<std::pair<math::Vec2, math::Vec2>> m_textInput;
};


// A panel image occupies whole framebuffer pixels, even when its host uses fractional points.
struct PanelImagePlacement
{
    math::Vec2 origin{0.0f};
    math::Vec2 size{0.0f};
    math::Extent2D pixels;
};

[[nodiscard]] DEVEX_API PanelImagePlacement placePanelImage(math::Vec2 origin, math::Vec2 size, math::Vec2 viewport,
                                                           float pixelsPerPoint) noexcept;

// A panel made with the interface of the engine: a canvas of entities in a scene of its own, which an
// interface world lays out and answers, drawn into an image that the host around it shows.
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
    // Whether the host it stands in has the keyboard.
    [[nodiscard]] bool focused() const noexcept;
    // Whether the pointer is over the image of the panel.
    [[nodiscard]] bool hovered() const noexcept;
    // What the last update gave the interface world, in units of the panel.
    [[nodiscard]] const ui::UiInput& input() const noexcept;

    // What the drags of the panel become once they leave it: what the other panels and the view take,
    // or nothing when they stay in the panel.
    void setDragOut(std::function<std::optional<EditorDrag>(const ui::Carried&)> convert);
    // What the drags of other panels that come over the panel become: the type and data its drop
    // targets read, or nothing for those it does not take.
    void setDragIn(std::function<std::optional<std::pair<std::string, std::string>>(const EditorDrag&)> convert);
    // Whether the arrows, Enter and Space move the focus between the buttons and press them, as in a
    // menu; a panel that answers the keys itself, as a tree does, turns it off.
    void setKeyboardNavigation(bool enabled) noexcept;
    // Whether the tooltips show outside the image of the panel, in the layer over the editor: a strip
    // such as a menu bar has no room for them.
    void setTooltipsOutside(bool outside) noexcept;
    // Sees what the interface world is about to be given, and may take keys from it: a list of
    // completions takes the arrows and Enter from the text under it.
    void setInputFilter(std::function<void(ui::UiInput&)> filter);

    // Inside the host it stands in: takes the room left in it, or only `height` points of it, gives
    // the interface world the mouse and the keys the host receives, and shows the image of the
    // panel. `zoom` is how many pixels one unit of the panel takes.
    void update(EditorUiKit& kit, core::Duration delta, float zoom, float height = 0.0f);
    // Where a point of the panel, in its units, is on the screen, in points of the window; and the
    // point of the panel a place of the screen is.
    [[nodiscard]] math::Vec2 screenOf(math::Vec2 units) const noexcept;
    [[nodiscard]] math::Vec2 unitsOf(math::Vec2 screen) const noexcept;
    // Adds the image of the panel to the frame, over a color, when the last update showed it.
    void render(EditorUiKit& kit, render::RenderWorld& world, math::Vec4 background);

    // How many pixels a unit takes in a panel whose text is `font` units, its letters as large as
    // the text of the theme.
    [[nodiscard]] static float zoomFor(float font) noexcept;

private:
    void carryOut(EditorUiKit& kit, const math::Vec2& origin, float pixelsPerPoint);

    scene::Scene m_scene;
    ui::UiWorld m_world;
    scene::Entity m_canvas;
    std::uint32_t m_surface = 0;
    math::Extent2D m_pixels;
    float m_zoom = 1.0f;
    bool m_shown = false;
    bool m_focused = false;
    bool m_hovered = false;
    bool m_connected = false;
    bool m_navigation = true;
    bool m_tooltipsOutside = false;
    math::Vec2 m_origin{0.0f, 0.0f};
    float m_pixelsPerPoint = 1.0f;
    ui::UiInput m_input;
    std::function<std::optional<EditorDrag>(const ui::Carried&)> m_dragOut;
    std::function<std::optional<std::pair<std::string, std::string>>(const EditorDrag&)> m_dragIn;
    std::function<void(ui::UiInput&)> m_filter;
    // Where the last frame was drawn, kept to reuse its storage.
    render::RenderWorld m_scratch;
};

// Rectangles as the panels lay them out, in units.
namespace rects {
[[nodiscard]] scene::UiRect fixed(math::Vec2 size) noexcept;
// A fixed size, in the middle of the height of a row.
[[nodiscard]] scene::UiRect middle(math::Vec2 size) noexcept;
// What is left of a row, at a height in the middle of it.
[[nodiscard]] scene::UiRect grow(float height) noexcept;
// As wide as its column, at a height.
[[nodiscard]] scene::UiRect wide(float height) noexcept;
[[nodiscard]] scene::UiRect whole(math::Vec4 inset = math::Vec4{0.0f}) noexcept;
} // namespace rects

// A button of a panel: its entity, its icon and its label.
struct PanelButton
{
    scene::Entity entity;
    scene::Entity icon;
    scene::Entity label;
};

// The pieces the panels of the editor are made of, as entities of their panel, sized in units of the
// size of their text.
class PanelBuilder
{
public:
    explicit PanelBuilder(std::uint32_t surface);

    UiPanel panel;
    float font = 14.0f;

    [[nodiscard]] scene::Scene& scene() noexcept;

    scene::Entity add(scene::Entity parent, const char* name, scene::UiRect rect, std::string_view style = {});
    scene::Entity text(scene::Entity parent, scene::UiRect rect, std::string value, std::string_view style,
                       bool bold = false, scene::TextAlign align = scene::TextAlign::Left, float size = 0.0f);
    scene::Entity icon(EditorUiKit& kit, scene::Entity parent, scene::UiRect rect, Icon glyph,
                       std::string_view style = "icon");
    // A button with an icon and a label, as wide as they need, or the width given; a negative width
    // makes it as wide as its column.
    PanelButton button(EditorUiKit& kit, scene::Entity parent, std::optional<Icon> glyph, std::string_view label,
                       std::string_view style = "button", float width = 0.0f, float height = 0.0f,
                       scene::TextAlign align = scene::TextAlign::Center);
    // Changes the label of a button, which keeps its width.
    void relabel(EditorUiKit& kit, const PanelButton& target, std::string_view label);
    void enable(const PanelButton& target, bool enabled);
    void tooltip(scene::Entity entity, std::string value);
    scene::Entity field(scene::Entity parent, scene::UiRect rect, std::string value, std::string placeholder,
                        std::string action = {});
    // A field that filters a list, with a magnifying glass at its left.
    scene::Entity searchField(EditorUiKit& kit, scene::Entity parent, scene::UiRect rect, std::string value,
                              std::string placeholder);
    // A dialog in the middle of the panel, over a veil.
    scene::Entity dialog(const char* name, math::Vec2 size);
    // The two buttons at the bottom of a dialog, on its right.
    std::pair<PanelButton, PanelButton> dialogButtons(EditorUiKit& kit, scene::Entity dialog, Icon glyph,
                                                      std::string_view confirm, float width);
    // A menu that opens under the pointer, its entries, and the lines between them.
    scene::Entity menu(const char* name, float width);
    PanelButton menuItem(EditorUiKit& kit, scene::Entity menu, std::optional<Icon> glyph, std::string_view label,
                         std::string_view shortcut = {});
    scene::Entity menuSeparator(scene::Entity menu);
    // Makes a menu as tall as its visible entries, and no wider than the panel.
    void fitMenu(scene::Entity menu, float width);
    // Tooltips in the colors and the size of the editor.
    void styleTooltips(const ThemeColors& colors);
};

} // namespace devex::tools::detail
