#include "EditorUi.hpp"

#include <imgui_internal.h>

#include <devex/asset/TextureData.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace devex::tools::detail {
namespace {

// The size icons are drawn at before the mip levels shrink them to the size they show at.
constexpr std::uint32_t iconPixels = 64;

[[nodiscard]] float linear(float channel) noexcept
{
    const float value = std::clamp(channel, 0.0f, 1.0f);
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

// A color as a theme writes it: vec4(r, g, b, a), linear.
[[nodiscard]] std::string written(ImVec4 srgb)
{
    const math::Vec4 color = linearColor(srgb);
    return std::format("vec4({}, {}, {}, {})", color.x, color.y, color.z, color.w);
}

[[nodiscard]] std::string written(float value)
{
    return std::format("{}", value);
}

[[nodiscard]] ImVec4 mixed(ImVec4 from, ImVec4 to, float amount) noexcept
{
    return ImVec4(from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount, from.z + (to.z - from.z) * amount,
                  from.w + (to.w - from.w) * amount);
}

// The UTF-8 of a character ImGui queued as typed.
void appendUtf8(std::string& text, unsigned int codepoint)
{
    if (codepoint < 0x80)
    {
        text.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800)
    {
        text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint < 0x10000)
    {
        text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

} // namespace

math::Vec4 linearColor(ImVec4 srgb) noexcept
{
    return math::Vec4{linear(srgb.x), linear(srgb.y), linear(srgb.z), std::clamp(srgb.w, 0.0f, 1.0f)};
}

Icon iconOf(IconText text) noexcept
{
    // Three bytes of UTF-8 for a character of the private use area.
    const std::string_view bytes = text;
    const auto byte = [&](std::size_t index) { return static_cast<char32_t>(static_cast<unsigned char>(bytes[index])); };
    const char32_t codepoint = ((byte(0) & 0x0F) << 12) | ((byte(1) & 0x3F) << 6) | (byte(2) & 0x3F);
    return static_cast<Icon>(codepoint - firstIconCodepoint);
}

EditorUiKit::EditorUiKit(render::Renderer& renderer, const IconSet& icons, std::filesystem::path fontsDirectory)
    : m_renderer(renderer)
    , m_icons(icons)
    , m_fontsDirectory(std::move(fontsDirectory))
{
}

EditorUiKit::~EditorUiKit()
{
    for (const BakedFont* font : {&m_regular, &m_bold, &m_mono})
    {
        if (font->atlas.isValid())
        {
            m_renderer.destroyTexture(font->atlas);
        }
    }
    for (const auto& [id, texture] : m_iconTextures)
    {
        if (texture.isValid())
        {
            m_renderer.destroyTexture(texture);
        }
    }
    if (m_checker.isValid())
    {
        m_renderer.destroyTexture(m_checker);
    }
}

void EditorUiKit::setAssetImages(std::function<render::TextureHandle(asset::AssetId)> textures,
                                 std::function<math::Vec2(asset::AssetId)> sizes,
                                 std::function<std::optional<ui::SpriteImage>(asset::AssetId)> sprites)
{
    m_assetTextures = std::move(textures);
    m_assetSizes = std::move(sizes);
    m_assetSprites = std::move(sprites);
}

void EditorUiKit::showTooltip(std::string text, ImVec2 at)
{
    m_tooltip = Tooltip{.text = std::move(text), .at = at};
}

std::optional<EditorUiKit::Tooltip> EditorUiKit::takeTooltip() noexcept
{
    return std::exchange(m_tooltip, std::nullopt);
}

asset::AssetId EditorUiKit::checker()
{
    const asset::AssetId id{core::Uuid::fromParts(0, 0x10005)};
    if (m_checker.isValid())
    {
        return id;
    }
    // Squares of two greys, sharp however large the image they stand behind.
    constexpr std::uint32_t size = 64;
    constexpr std::uint32_t square = 8;
    asset::Image image{.width = size, .height = size, .rgba = std::vector<std::uint8_t>(size * size * 4)};
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            const std::uint8_t grey = ((x / square) + (y / square)) % 2 == 0 ? 70 : 102;
            std::uint8_t* const pixel = &image.rgba[(y * size + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = grey;
            pixel[3] = 255;
        }
    }
    if (core::Result<asset::TextureData> built =
            asset::buildTexture(image, asset::TextureBuildOptions{.srgb = true, .mipmaps = false, .compress = false}))
    {
        built->filter = asset::TextureFilter::Nearest;
        if (const core::Result<render::TextureHandle> created = m_renderer.createTexture(*built))
        {
            m_checker = *created;
        }
    }
    return id;
}

asset::AssetId EditorUiKit::regularFont() noexcept
{
    return asset::AssetId{core::Uuid::fromParts(0, 0x10001)};
}

asset::AssetId EditorUiKit::boldFont() noexcept
{
    return asset::AssetId{core::Uuid::fromParts(0, 0x10002)};
}

asset::AssetId EditorUiKit::themeId() noexcept
{
    return asset::AssetId{core::Uuid::fromParts(0, 0x10003)};
}

asset::AssetId EditorUiKit::monoFont() noexcept
{
    return asset::AssetId{core::Uuid::fromParts(0, 0x10004)};
}

const EditorUiKit::BakedFont& EditorUiKit::baked(asset::AssetId font) const noexcept
{
    return font == boldFont() ? m_bold : font == monoFont() ? m_mono : m_regular;
}

asset::AssetId EditorUiKit::icon(Icon icon)
{
    const asset::AssetId id{core::Uuid::fromParts(0, 0x20000 + static_cast<std::uint64_t>(icon))};
    if (m_iconTextures.contains(id))
    {
        return id;
    }
    // Drawn once, white, with mip levels so that it stays smooth at the size of the text.
    render::TextureHandle texture;
    if (const core::Result<SvgImage> drawn = renderSvg(m_icons.svg(icon), iconPixels, iconPixels))
    {
        const asset::Image image{.width = drawn->width, .height = drawn->height, .rgba = drawn->rgba};
        if (const core::Result<asset::TextureData> built =
                asset::buildTexture(image, asset::TextureBuildOptions{.srgb = true, .mipmaps = true, .compress = false}))
        {
            if (const core::Result<render::TextureHandle> created = m_renderer.createTexture(*built))
            {
                texture = *created;
            }
        }
    }
    m_iconTextures.emplace(id, texture);
    return id;
}

void EditorUiKit::bakeFonts()
{
    if (m_fontsBaked)
    {
        return;
    }
    m_fontsBaked = true;
    DEVEX_PROFILE_SCOPE("Bake editor fonts");
    const auto bake = [this](const char* fileName, BakedFont& font) {
        const std::filesystem::path file = m_fontsDirectory / fileName;
        const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(file);
        core::Result<asset::FontData> baked =
            bytes ? asset::bakeFont(*bytes, core::toUtf8(file.stem()), 40.0f, 6.0f) : std::unexpected(bytes.error());
        if (!baked)
        {
            DEVEX_LOG_ERROR("Cannot bake the editor font {}: {}", fileName, baked.error());
            return;
        }
        // The atlas holds distances, not colors: one channel, read as it was written.
        asset::TextureData image{.format = asset::TextureFormat::R8Unorm};
        image.mips.push_back({.width = baked->atlasWidth,
                              .height = baked->atlasHeight,
                              .bytes = std::vector<std::byte>(reinterpret_cast<const std::byte*>(baked->atlas.data()),
                                                              reinterpret_cast<const std::byte*>(baked->atlas.data() +
                                                                                                 baked->atlas.size()))});
        if (const core::Result<render::TextureHandle> atlas = m_renderer.createTexture(image))
        {
            font.atlas = *atlas;
        }
        font.data = std::make_shared<const asset::FontData>(std::move(*baked));
    };
    bake("NotoSans-Regular.ttf", m_regular);
    bake("NotoSans-Bold.ttf", m_bold);
    bake("JetBrainsMono-Regular.ttf", m_mono);
}

std::function<ui::FontRef(asset::AssetId)> EditorUiKit::fonts()
{
    bakeFonts();
    return [this](asset::AssetId id) {
        const BakedFont& font = baked(id);
        return ui::FontRef{.data = font.data.get(), .atlas = font.atlas};
    };
}

ui::ThemeSource EditorUiKit::themes()
{
    return [this](asset::AssetId id) -> std::shared_ptr<const asset::ThemeData> {
        return id == themeId() ? m_theme : nullptr;
    };
}

ui::DrawContext EditorUiKit::drawContext()
{
    return ui::DrawContext{
        .fonts = fonts(),
        .textures =
            [this](asset::AssetId id) {
                // The icons and the checkerboard of the kit, then the textures of the project.
                if (const auto found = m_iconTextures.find(id); found != m_iconTextures.end())
                {
                    return found->second;
                }
                if (id == asset::AssetId{core::Uuid::fromParts(0, 0x10005)})
                {
                    return m_checker;
                }
                return m_assetTextures ? m_assetTextures(id) : render::TextureHandle{};
            },
        .sprites = m_assetSprites,
        .textureSize =
            [this](asset::AssetId id) {
                if (m_iconTextures.contains(id) || id == asset::AssetId{core::Uuid::fromParts(0, 0x10005)})
                {
                    return math::Vec2{static_cast<float>(iconPixels)};
                }
                return m_assetSizes ? m_assetSizes(id) : math::Vec2{0.0f};
            },
        .defaultFont = regularFont(),
    };
}

const asset::FontData* EditorUiKit::fontData(asset::AssetId font)
{
    bakeFonts();
    return baked(font).data.get();
}

float EditorUiKit::textWidth(asset::AssetId font, std::string_view text, float size)
{
    const asset::FontData* const data = fontData(font);
    if (data == nullptr)
    {
        return 0.0f;
    }
    // Measured once for a font and a size; the fonts of the kit never change once baked.
    const std::uint64_t face = font == boldFont() ? 1 : font == monoFont() ? 2 : 0;
    auto& widths = m_widths[(face << 32) | std::bit_cast<std::uint32_t>(size)];
    if (const auto known = widths.find(text); known != widths.end())
    {
        return known->second;
    }
    // Names and numbers come and go: what was kept is dropped before it grows without end.
    if (widths.size() >= 4096)
    {
        widths.clear();
    }
    const float width = ui::measureText(*data, text, ui::TextStyle{.size = size, .wrap = false}).x;
    widths.emplace(std::string(text), width);
    return width;
}

void EditorUiKit::refreshTheme(const ThemeColors& colors)
{
    const auto same = [](ImVec4 first, ImVec4 second) {
        return first.x == second.x && first.y == second.y && first.z == second.z && first.w == second.w;
    };
    if (m_theme != nullptr && same(m_themeAccent, colors.accent) && same(m_themePanel, colors.panel))
    {
        return;
    }
    m_themeAccent = colors.accent;
    m_themePanel = colors.panel;
    const ImVec4 mono = colors.dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    // A button lights up under the pointer, and takes the accent while pressed.
    const std::string hover = colors.dark ? "vec4(1.35, 1.35, 1.35, 1)" : "vec4(0.93, 0.93, 0.93, 1)";
    const std::string pressed = colors.dark ? "vec4(1.7, 1.7, 1.8, 1)" : "vec4(0.85, 0.85, 0.88, 1)";
    const auto image = [](const std::string& color, float radius) {
        return std::vector<asset::ThemeOverride>{{"UiImage", "color", color}, {"UiImage", "corner_radius", written(radius)}};
    };
    const auto text = [](ImVec4 color) { return std::vector<asset::ThemeOverride>{{"UiText", "color", written(color)}}; };
    const auto clickable = [&](std::vector<asset::ThemeOverride> values) {
        values.push_back({"UiButton", "hover_color", hover});
        values.push_back({"UiButton", "pressed_color", pressed});
        values.push_back({"UiButton", "disabled_color", "vec4(1, 1, 1, 0.45)"});
        values.push_back({"UiButton", "fade_time", "0.06"});
        return values;
    };

    auto theme = std::make_shared<asset::ThemeData>();
    const auto add = [&](const char* name, std::vector<asset::ThemeOverride> values) {
        theme->styles.push_back({name, std::move(values)});
    };
    add("window", image(written(colors.outer), 0.0f));
    add("panel", image(written(colors.panel), 4.0f));
    add("list", image(written(colors.field), 4.0f));
    add("popup", image(written(colors.popup), 5.0f));
    add("button", clickable(image(written(colors.raised), 4.0f)));
    add("flat", clickable(image(written(ImVec4(colors.raised.x, colors.raised.y, colors.raised.z, 0.0f)), 4.0f)));
    add("primary", clickable(image(written(colors.accent), 4.0f)));
    add("menu_item", clickable(image(written(colors.popup), 3.0f)));
    add("row", clickable(image(written(colors.field), 3.0f)));
    add("row_selected", clickable(image(written(mixed(colors.field, colors.accent, 0.32f)), 3.0f)));
    add("field", [&] {
        std::vector<asset::ThemeOverride> values = image(written(colors.field), 4.0f);
        values.push_back({"UiText", "color", written(colors.text)});
        values.push_back({"UiInput", "placeholder_color", written(colors.textDim)});
        values.push_back({"UiInput", "selection_color", written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.45f))});
        values.push_back({"UiInput", "caret_color", written(colors.text)});
        return values;
    }());
    add("dropdown", [&] {
        std::vector<asset::ThemeOverride> values = clickable(image(written(colors.raised), 4.0f));
        values.push_back({"UiText", "color", written(colors.text)});
        values.push_back({"UiDropdown", "list_color", written(colors.popup)});
        values.push_back({"UiDropdown", "highlight_color", written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.55f))});
        values.push_back({"UiDropdown", "arrow_color", written(colors.textDim)});
        return values;
    }());
    add("toggle", [&] {
        std::vector<asset::ThemeOverride> values = image(written(colors.field), 3.0f);
        values.push_back({"UiToggle", "check_color", written(colors.accent)});
        return values;
    }());
    // The text of a file: the colours of its cursor, of what is selected and of its gutter.
    add("code", [&] {
        std::vector<asset::ThemeOverride> values = image(written(colors.field), 4.0f);
        values.push_back({"UiText", "color", written(colors.text)});
        values.push_back({"UiTextArea", "selection_color", written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.45f))});
        values.push_back({"UiTextArea", "caret_color", written(colors.text)});
        values.push_back({"UiTextArea", "line_number_color", written(colors.textDim)});
        values.push_back({"UiTextArea", "current_line_color", written(ImVec4(mono.x, mono.y, mono.z, 0.05f))});
        values.push_back({"UiTextArea", "scrollbar_color", written(ImVec4(mono.x, mono.y, mono.z, 0.2f))});
        return values;
    }());
    add("scroll", {{"UiScroll", "scrollbar_color", written(ImVec4(mono.x, mono.y, mono.z, 0.2f))},
                   {"UiScroll", "scrollbar_size", "7"}});
    add("separator", image(written(colors.border), 0.0f));
    add("text", text(colors.text));
    add("dim", text(colors.textDim));
    add("accent", text(colors.accent));
    add("warning", text(colors.warning));
    add("error", text(colors.error));
    add("success", text(colors.success));
    add("icon", {{"UiImage", "color", written(colors.text)}});
    add("icon_dim", {{"UiImage", "color", written(colors.textDim)}});
    add("icon_accent", {{"UiImage", "color", written(colors.accent)}});
    add("icon_favorite", {{"UiImage", "color", written(colors.favorite)}});
    add("icon_warning", {{"UiImage", "color", written(colors.warning)}});
    add("icon_error", {{"UiImage", "color", written(colors.error)}});
    add("icon_success", {{"UiImage", "color", written(colors.success)}});
    // The palette that creates entities and adds components: a card with rounded corners, lists of
    // soft rows, and pills that name a category.
    add("dialog", image(written(colors.panel), 10.0f));
    add("dialog_list", image(written(colors.field), 8.0f));
    add("dialog_card", image(written(mixed(colors.panel, colors.raised, 0.55f)), 8.0f));
    // The colour of the card, so that only the pointer lights it up.
    add("side", clickable(image(written(colors.panel), 6.0f)));
    add("side_selected", clickable(image(written(mixed(colors.panel, colors.accent, 0.28f)), 6.0f)));
    add("soft_row", clickable(image(written(colors.field), 6.0f)));
    add("soft_row_selected", clickable(image(written(mixed(colors.field, colors.accent, 0.30f)), 6.0f)));
    add("chip", image(written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.18f)), 9.0f));
    // The scene tree: the names of prefab entities, the lines from children to their parent, and
    // where what is dragged would land.
    add("prefab", text(colors.prefab));
    // The buttons at the end of a row take its colour, so that only the pointer shows them.
    add("row_button", clickable(image(written(colors.field), 5.0f)));
    add("row_button_selected", clickable(image(written(mixed(colors.field, colors.accent, 0.30f)), 5.0f)));
    add("guide", image(written(ImVec4(colors.textDim.x, colors.textDim.y, colors.textDim.z, 0.28f)), 0.0f));
    add("drop_line", image(written(colors.accent), 1.0f));
    add("drop_into", image(written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.22f)), 6.0f));
    // The inspector: a card per component under a header that lights up, labels a little dimmer than
    // the values, numbers the pointer lights and drags, swatches whose colour is the value's, and the
    // mark of a value that differs from its prefab's.
    const ImVec4 card = mixed(colors.panel, colors.raised, 0.35f);
    add("card", image(written(card), 6.0f));
    add("card_header", clickable(image(written(mixed(card, colors.raised, 0.55f)), 5.0f)));
    add("group", clickable(image(written(ImVec4(card.x, card.y, card.z, 0.0f)), 4.0f)));
    add("label", text(mixed(colors.text, colors.textDim, 0.45f)));
    add("number", [&] {
        std::vector<asset::ThemeOverride> values = clickable(image(written(colors.field), 4.0f));
        values.push_back({"UiText", "color", written(colors.text)});
        values.push_back({"UiInput", "selection_color", written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.45f))});
        values.push_back({"UiInput", "caret_color", written(colors.text)});
        return values;
    }());
    add("swatch", clickable({{"UiImage", "corner_radius", "4"}}));
    add("tool", clickable(image(written(ImVec4(colors.raised.x, colors.raised.y, colors.raised.z, 0.6f)), 4.0f)));
    add("mark", image(written(colors.accent), 1.0f));
    // The frame of the editor: buttons that are clear until the pointer is on them, and tinted while
    // what they stand for is on; the tabs of the scenes, of which the one shown takes the colour of
    // the toolbar under it; and the tooltips shown over everything. A clear button has no colour for
    // the pointer to light, hence a style of its own under it.
    add("bar_button", clickable(image(written(ImVec4(mono.x, mono.y, mono.z, 0.0f)), 4.0f)));
    add("bar_hover", clickable(image(written(ImVec4(mono.x, mono.y, mono.z, 0.10f)), 4.0f)));
    add("bar_selected", clickable(image(written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.28f)), 4.0f)));
    add("tab", clickable(image(written(ImVec4(mono.x, mono.y, mono.z, 0.0f)), 5.0f)));
    add("tab_hover", clickable(image(written(ImVec4(mono.x, mono.y, mono.z, 0.06f)), 5.0f)));
    add("tab_selected", clickable(image(written(colors.panel), 5.0f)));
    add("tooltip", image(written(colors.popup), 4.0f));
    // What is selected of a text that is only read.
    add("selection", {{"UiImage", "color", written(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.4f))},
                      {"UiImage", "corner_radius", "0"}});
    m_theme = std::move(theme);
}

UiPanel::UiPanel(std::uint32_t surface)
    : m_surface(surface)
{
    m_canvas = m_scene.createEntity("Canvas");
    m_scene.add<scene::Canvas>(m_canvas, scene::Canvas{.scaleMode = scene::CanvasScaleMode::ConstantPixels,
                                                       .theme = EditorUiKit::themeId()});
}

scene::Scene& UiPanel::scene() noexcept
{
    return m_scene;
}

ui::UiWorld& UiPanel::world() noexcept
{
    return m_world;
}

scene::Entity UiPanel::canvas() const noexcept
{
    return m_canvas;
}

math::Vec2 UiPanel::size() const noexcept
{
    return math::Vec2{static_cast<float>(m_pixels.width), static_cast<float>(m_pixels.height)} / m_zoom;
}

bool UiPanel::focused() const noexcept
{
    return m_focused;
}

bool UiPanel::hovered() const noexcept
{
    return m_hovered;
}

const ui::UiInput& UiPanel::input() const noexcept
{
    return m_input;
}

void UiPanel::setDragOut(std::function<std::optional<ImGuiDrag>(const ui::Carried&)> convert)
{
    m_dragOut = std::move(convert);
}

void UiPanel::setDragIn(std::function<std::optional<std::pair<std::string, std::string>>(const ImGuiPayload&)> convert)
{
    m_dragIn = std::move(convert);
}

void UiPanel::setKeyboardNavigation(bool enabled) noexcept
{
    m_navigation = enabled;
}

void UiPanel::setInputFilter(std::function<void(ui::UiInput&)> filter)
{
    m_filter = std::move(filter);
}

void UiPanel::setTooltipsOutside(bool outside) noexcept
{
    m_tooltipsOutside = outside;
    m_world.setTooltipsDrawn(!outside);
}

ImVec2 UiPanel::screenOf(math::Vec2 units) const noexcept
{
    const float pointsPerUnit = m_zoom / m_pixelsPerPoint;
    return ImVec2(m_origin.x + units.x * pointsPerUnit, m_origin.y + units.y * pointsPerUnit);
}

math::Vec2 UiPanel::unitsOf(ImVec2 screen) const noexcept
{
    const float unitsPerPoint = m_pixelsPerPoint / m_zoom;
    return math::Vec2{(screen.x - m_origin.x) * unitsPerPoint, (screen.y - m_origin.y) * unitsPerPoint};
}

float UiPanel::zoomFor(float font) noexcept
{
    const float pixelsPerPoint =
        ImGui::GetIO().DisplayFramebufferScale.x > 0.0f ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f;
    // ImGui sizes its fonts by their whole line, the interface of the engine by their em: a unit
    // is a point of text, scaled as ImGui scales its own.
    return pixelsPerPoint * ImGui::GetFontSize() / std::max(regularFontPixels(font), 1.0f);
}

void UiPanel::update(EditorUiKit& kit, core::Duration delta, float zoom, float height)
{
    DEVEX_PROFILE_SCOPE("Panel update");
    const ImGuiIO& io = ImGui::GetIO();
    const float pixelsPerPoint = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
    const ImVec2 available(std::max(ImGui::GetContentRegionAvail().x, 1.0f),
                           std::max(height > 0.0f ? height : ImGui::GetContentRegionAvail().y, 1.0f));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    m_origin = origin;
    m_pixelsPerPoint = pixelsPerPoint;
    m_zoom = std::max(zoom, 0.1f);
    m_pixels = {static_cast<std::uint32_t>(std::max(std::floor(available.x * pixelsPerPoint), 1.0f)),
                static_cast<std::uint32_t>(std::max(std::floor(available.y * pixelsPerPoint), 1.0f))};
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(render::Renderer::uiSurfaceTexture(m_surface))), available);
    m_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!m_connected)
    {
        m_world.setFonts(kit.fonts(), EditorUiKit::regularFont());
        m_world.setThemes(kit.themes());
        m_connected = true;
    }

    // The mouse, in units of the panel from its top left corner; the keys while the window has
    // the keyboard.
    ui::UiInput input;
    input.pointer = math::Vec2{(io.MousePos.x - origin.x) * pixelsPerPoint, (io.MousePos.y - origin.y) * pixelsPerPoint} / m_zoom;
    input.pointerDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    input.pointerPressed = m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    input.pointerReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    input.pointerMoved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    input.secondaryPressed = m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    input.wheel = m_hovered ? io.MouseWheel : 0.0f;
    if (!m_hovered && !input.pointerDown && !input.pointerReleased)
    {
        // Far outside every element, so that nothing is hovered while the pointer is elsewhere.
        input.pointer = math::Vec2{-1.0e6f, -1.0e6f};
    }
    if (m_focused)
    {
        const auto stroke = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
        const bool editing = m_world.isEditing();
        for (const ImWchar character : io.InputQueueCharacters)
        {
            if (character >= 0x20 && character != 0x7F)
            {
                appendUtf8(input.typed, character);
            }
        }
        if (m_navigation)
        {
            input.moveX = (stroke(ImGuiKey_RightArrow) ? 1 : 0) - (stroke(ImGuiKey_LeftArrow) ? 1 : 0);
            input.moveY = (stroke(ImGuiKey_DownArrow) ? 1 : 0) - (stroke(ImGuiKey_UpArrow) ? 1 : 0);
        }
        input.submitPressed = (m_navigation || editing) &&
                              (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                               (m_navigation && !editing && ImGui::IsKeyPressed(ImGuiKey_Space, false)));
        input.cancelPressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        input.backspacePressed = stroke(ImGuiKey_Backspace);
        input.deletePressed = stroke(ImGuiKey_Delete);
        input.leftPressed = stroke(ImGuiKey_LeftArrow);
        input.rightPressed = stroke(ImGuiKey_RightArrow);
        input.upPressed = stroke(ImGuiKey_UpArrow);
        input.downPressed = stroke(ImGuiKey_DownArrow);
        input.homePressed = stroke(ImGuiKey_Home);
        input.endPressed = stroke(ImGuiKey_End);
        input.selecting = io.KeyShift;
        input.copyPressed = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false);
        input.cutPressed = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false);
        input.pastePressed = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false);
        input.selectAllPressed = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false);
        // What an area of text answers beside these.
        input.pageUpPressed = stroke(ImGuiKey_PageUp);
        input.pageDownPressed = stroke(ImGuiKey_PageDown);
        input.tabPressed = stroke(ImGuiKey_Tab);
        input.wordModifier = io.KeyCtrl;
        input.undoPressed = io.KeyCtrl && !io.KeyShift && stroke(ImGuiKey_Z);
        input.redoPressed = io.KeyCtrl && (stroke(ImGuiKey_Y) || (io.KeyShift && stroke(ImGuiKey_Z)));
        if (input.pastePressed)
        {
            if (const char* const clipboard = ImGui::GetClipboardText())
            {
                input.clipboard = clipboard;
            }
        }
    }

    // An ImGui drag that comes over the panel, which its targets may take; not the panel's own,
    // which ImGui carries once it leaves.
    const ui::Carried* const own = m_world.carried();
    if (const ImGuiPayload* const payload = ImGui::GetDragDropPayload();
        payload != nullptr && m_dragIn && (own == nullptr || !own->source.isValid()))
    {
        if (std::optional<std::pair<std::string, std::string>> carried = m_dragIn(*payload))
        {
            m_world.carryFromOutside(std::move(carried->first), std::move(carried->second));
        }
    }
    if (m_filter)
    {
        m_filter(input);
    }
    m_world.update(m_scene, size(), input, delta);
    m_input = input;
    // A tooltip the image has no room for goes to the layer over the editor.
    if (m_tooltipsOutside)
    {
        if (const std::optional<ui::UiWorld::ShownTooltip> shown = m_world.shownTooltip(m_scene))
        {
            kit.showTooltip(std::string(shown->text), screenOf(shown->at));
        }
    }
    carryToImGui(kit, origin, pixelsPerPoint);
    if (const std::string& copied = m_world.clipboardRequest(); !copied.empty())
    {
        ImGui::SetClipboardText(copied.c_str());
    }
    // While a field takes what is typed, the system sends the letters, as it does for an ImGui field,
    // and shows its input method next to the field.
    if (m_focused && m_world.isEditing() && !m_world.canvases().empty())
    {
        ImGuiContext& context = *ImGui::GetCurrentContext();
        // As an ImGui field does: the shortcuts of the editor leave the letters to the field.
        context.WantTextInputNextFrame = 1;
        context.PlatformImeData.WantVisible = true;
        context.PlatformImeData.WantTextInput = true;
        context.PlatformImeData.InputLineHeight = ImGui::GetFontSize();
        context.PlatformImeData.ViewportId = ImGui::GetWindowViewport()->ID;
        if (const ui::LaidOutRect* const field = m_world.canvases().front().layout.find(m_world.editedField()))
        {
            context.PlatformImeData.InputPos =
                ImVec2(origin.x + field->min.x * m_zoom / pixelsPerPoint, origin.y + field->max.y * m_zoom / pixelsPerPoint);
        }
        else if (const std::optional<ui::UiWorld::CaretPlace> caret = m_world.textCaretPlace(m_scene, m_world.editedTextArea()))
        {
            // Under the cursor of an area of text.
            context.PlatformImeData.InputPos = ImVec2(origin.x + caret->position.x * m_zoom / pixelsPerPoint,
                                                      origin.y + (caret->position.y + caret->height) * m_zoom / pixelsPerPoint);
        }
    }
    m_shown = true;
}

void UiPanel::carryToImGui(EditorUiKit& kit, const ImVec2& origin, float pixelsPerPoint)
{
    // What the panel carries is an ImGui drag as well for as long as the button is held, so that the
    // windows around take it where they take their own; the panel draws its label while the pointer
    // is over it, and the layer over the editor does once it leaves, next to the pointer.
    const ui::Carried* const carried = m_world.carried();
    if (carried == nullptr || !carried->source.isValid() || !m_dragOut || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        return;
    }
    const std::optional<ImGuiDrag> drag = m_dragOut(*carried);
    if (!drag)
    {
        return;
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern | ImGuiDragDropFlags_SourceNoPreviewTooltip))
    {
        ImGui::SetDragDropPayload(drag->type.c_str(), drag->payload.data(), drag->payload.size());
        ImGui::EndDragDropSource();
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 end(origin.x + static_cast<float>(m_pixels.width) / pixelsPerPoint,
                     origin.y + static_cast<float>(m_pixels.height) / pixelsPerPoint);
    const bool inside = mouse.x >= origin.x && mouse.y >= origin.y && mouse.x < end.x && mouse.y < end.y;
    if (!inside && !drag->label.empty())
    {
        kit.showTooltip(drag->label, mouse);
    }
}

void UiPanel::render(EditorUiKit& kit, render::RenderWorld& world, math::Vec4 background)
{
    DEVEX_PROFILE_SCOPE("Panel image");
    if (!std::exchange(m_shown, false))
    {
        return;
    }
    // Laid out in units, drawn in pixels.
    m_scratch.uiVertices.clear();
    m_scratch.uiIndices.clear();
    m_scratch.uiDraws.clear();
    m_world.build(m_scene, kit.drawContext(), m_scratch);
    ui::placeDrawList(m_scratch, ui::DrawListMark{}, math::Vec2{0.0f}, m_zoom);
    render::UiSurface surface{.id = m_surface, .size = m_pixels, .clearColor = background};
    surface.vertices = m_scratch.uiVertices;
    surface.indices = m_scratch.uiIndices;
    surface.draws = m_scratch.uiDraws;
    world.uiSurfaces.push_back(std::move(surface));
}

namespace rects {

scene::UiRect fixed(math::Vec2 size) noexcept
{
    return scene::UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = size};
}

scene::UiRect middle(math::Vec2 size) noexcept
{
    return scene::UiRect{.anchorMin = {0.0f, 0.5f},
                         .anchorMax = {0.0f, 0.5f},
                         .offsetMin = {0.0f, -size.y * 0.5f},
                         .offsetMax = {size.x, size.y * 0.5f}};
}

scene::UiRect grow(float height) noexcept
{
    return scene::UiRect{.anchorMin = {0.0f, 0.5f},
                         .anchorMax = {1.0f, 0.5f},
                         .offsetMin = {0.0f, -height * 0.5f},
                         .offsetMax = {0.0f, height * 0.5f}};
}

scene::UiRect wide(float height) noexcept
{
    return scene::UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, height}};
}

scene::UiRect whole(math::Vec4 inset) noexcept
{
    return scene::UiRect{.anchorMin = {0.0f, 0.0f},
                         .anchorMax = {1.0f, 1.0f},
                         .offsetMin = {inset.x, inset.y},
                         .offsetMax = {-inset.z, -inset.w}};
}

} // namespace rects

PanelBuilder::PanelBuilder(std::uint32_t surface)
    : panel(surface)
{
}

scene::Scene& PanelBuilder::scene() noexcept
{
    return panel.scene();
}

scene::Entity PanelBuilder::add(scene::Entity parent, const char* name, scene::UiRect rect, std::string_view style)
{
    const scene::Entity entity = scene().createEntity(name);
    static_cast<void>(scene().setParent(entity, parent.isValid() ? parent : panel.canvas()));
    rect.style = style;
    scene().add<scene::UiRect>(entity, rect);
    return entity;
}

scene::Entity PanelBuilder::text(scene::Entity parent, scene::UiRect rect, std::string value, std::string_view style,
                                 bool bold, scene::TextAlign align, float size)
{
    const scene::Entity entity = add(parent, "Text", rect, style);
    scene().add<scene::UiText>(entity, scene::UiText{.text = std::move(value),
                                                     .font = bold ? EditorUiKit::boldFont() : EditorUiKit::regularFont(),
                                                     .size = size > 0.0f ? size : font,
                                                     .align = align,
                                                     .verticalAlign = scene::TextVerticalAlign::Middle,
                                                     .wrap = false});
    return entity;
}

scene::Entity PanelBuilder::icon(EditorUiKit& kit, scene::Entity parent, scene::UiRect rect, Icon glyph,
                                 std::string_view style)
{
    const scene::Entity entity = add(parent, "Icon", rect, style);
    scene().add<scene::UiImage>(entity, scene::UiImage{.texture = kit.icon(glyph), .raycastTarget = false});
    return entity;
}

PanelButton PanelBuilder::button(EditorUiKit& kit, scene::Entity parent, std::optional<Icon> glyph, std::string_view label,
                                 std::string_view style, float width, float height, scene::TextAlign align)
{
    const float tall = height > 0.0f ? height : font * 2.0f;
    const float iconSize = font * 1.1f;
    const float labelWidth = label.empty() ? 0.0f : kit.textWidth(EditorUiKit::regularFont(), label, font);
    const float needed = font * 1.4f + (glyph ? iconSize + (label.empty() ? 0.0f : font * 0.45f) : 0.0f) + labelWidth;
    PanelButton made;
    made.entity = add(parent, "Button", width < 0.0f ? rects::wide(tall) : rects::middle({width > 0.0f ? width : needed, tall}),
                      style);
    scene().add<scene::UiImage>(made.entity);
    scene().add<scene::UiButton>(made.entity);
    scene().add<scene::UiLayout>(made.entity, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                              .spacing = font * 0.45f,
                                                              .padding = {font * 0.7f, 0.0f, font * 0.7f, 0.0f},
                                                              .align = align});
    if (glyph)
    {
        made.icon = icon(kit, made.entity, rects::middle({iconSize, iconSize}), *glyph);
    }
    if (!label.empty())
    {
        made.label = text(made.entity, rects::middle({labelWidth + 2.0f, tall}), std::string(label), "text");
    }
    return made;
}

void PanelBuilder::relabel(EditorUiKit& kit, const PanelButton& target, std::string_view label)
{
    scene::UiText& shown = scene().get<scene::UiText>(target.label);
    if (shown.text != label)
    {
        shown.text = std::string(label);
        scene::UiRect& rect = scene().get<scene::UiRect>(target.label);
        rect.offsetMax.x = rect.offsetMin.x + kit.textWidth(EditorUiKit::regularFont(), label, font) + 2.0f;
    }
}

void PanelBuilder::enable(const PanelButton& target, bool enabled)
{
    scene().get<scene::UiButton>(target.entity).interactable = enabled;
    const float opacity = enabled ? 1.0f : 0.45f;
    for (const scene::Entity part : {target.icon, target.label})
    {
        if (part.isValid())
        {
            scene().get<scene::UiRect>(part).opacity = opacity;
        }
    }
}

void PanelBuilder::tooltip(scene::Entity entity, std::string value)
{
    if (scene::UiTooltip* const existing = scene().tryGet<scene::UiTooltip>(entity))
    {
        if (existing->text != value)
        {
            existing->text = std::move(value);
        }
        return;
    }
    scene().add<scene::UiTooltip>(entity, scene::UiTooltip{.text = std::move(value), .delay = 0.45f});
}

scene::Entity PanelBuilder::field(scene::Entity parent, scene::UiRect rect, std::string value, std::string placeholder,
                                  std::string action)
{
    const scene::Entity entity = add(parent, "Field", rect, "field");
    scene().add<scene::UiImage>(entity);
    scene().add<scene::UiText>(entity, scene::UiText{.text = std::move(value),
                                                     .font = EditorUiKit::regularFont(),
                                                     .size = font,
                                                     .verticalAlign = scene::TextVerticalAlign::Middle,
                                                     .wrap = false});
    scene().add<scene::UiInput>(entity, scene::UiInput{.placeholder = std::move(placeholder),
                                                       .padding = {font * 0.6f, 0.0f},
                                                       .action = std::move(action)});
    return entity;
}

scene::Entity PanelBuilder::searchField(EditorUiKit& kit, scene::Entity parent, scene::UiRect rect, std::string value,
                                        std::string placeholder)
{
    const scene::Entity entity = field(parent, rect, std::move(value), std::move(placeholder));
    scene().get<scene::UiInput>(entity).padding.x = font * 2.0f;
    icon(kit, entity,
         scene::UiRect{.anchorMin = {0.0f, 0.5f},
                       .anchorMax = {0.0f, 0.5f},
                       .offsetMin = {font * 0.6f, -font * 0.5f},
                       .offsetMax = {font * 1.6f, font * 0.5f}},
         Icon::Search, "icon_dim");
    return entity;
}

scene::Entity PanelBuilder::dialog(const char* name, math::Vec2 size)
{
    const scene::Entity entity = add({}, name,
                                     scene::UiRect{.anchorMin = {0.5f, 0.5f},
                                                   .anchorMax = {0.5f, 0.5f},
                                                   .offsetMin = {-size.x * 0.5f, -size.y * 0.5f},
                                                   .offsetMax = {size.x * 0.5f, size.y * 0.5f},
                                                   .visible = false},
                                     "popup");
    scene().add<scene::UiImage>(entity);
    scene().add<scene::UiPopup>(entity, scene::UiPopup{.kind = scene::UiPopupKind::Modal});
    scene().add<scene::UiLayout>(entity, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                         .spacing = font * 0.55f,
                                                         .padding = math::Vec4{font * 1.2f},
                                                         .align = scene::TextAlign::Left});
    return entity;
}

std::pair<PanelButton, PanelButton> PanelBuilder::dialogButtons(EditorUiKit& kit, scene::Entity dialog, Icon glyph,
                                                                std::string_view confirm, float width)
{
    const scene::Entity line = add(dialog, "Buttons", rects::wide(font * 2.0f));
    scene().add<scene::UiLayout>(line, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                       .spacing = font * 0.5f,
                                                       .align = scene::TextAlign::Right});
    const PanelButton first = button(kit, line, glyph, confirm, "primary", width);
    const PanelButton second = button(kit, line, std::nullopt, "Cancel", "button", width * 0.75f);
    return {first, second};
}

scene::Entity PanelBuilder::menu(const char* name, float width)
{
    const scene::Entity entity = add({}, name,
                                     scene::UiRect{.anchorMin = {0.0f, 0.0f},
                                                   .anchorMax = {0.0f, 0.0f},
                                                   .offsetMin = {0.0f, 0.0f},
                                                   .offsetMax = {width, font * 2.0f},
                                                   .visible = false},
                                     "popup");
    scene().add<scene::UiImage>(entity);
    scene().add<scene::UiPopup>(entity);
    scene().add<scene::UiLayout>(entity, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                         .spacing = 1.0f,
                                                         .padding = math::Vec4{font * 0.3f},
                                                         .align = scene::TextAlign::Left});
    return entity;
}

PanelButton PanelBuilder::menuItem(EditorUiKit& kit, scene::Entity menu, std::optional<Icon> glyph, std::string_view label,
                                   std::string_view shortcut)
{
    PanelButton made = button(kit, menu, glyph, label, "menu_item", -1.0f, font * 1.9f, scene::TextAlign::Left);
    if (!shortcut.empty())
    {
        // The label takes what the row leaves, which puts the keys against its right edge.
        scene().get<scene::UiRect>(made.label) = rects::grow(font * 1.9f);
        scene().get<scene::UiRect>(made.label).style = "text";
        const scene::Entity keys = add(made.entity, "Shortcut", rects::middle({font * 5.5f, font * 1.9f}), "dim");
        scene().add<scene::UiText>(keys, scene::UiText{.text = std::string(shortcut),
                                                       .font = EditorUiKit::regularFont(),
                                                       .size = font * 0.9f,
                                                       .align = scene::TextAlign::Right,
                                                       .verticalAlign = scene::TextVerticalAlign::Middle,
                                                       .wrap = false});
    }
    return made;
}

scene::Entity PanelBuilder::menuSeparator(scene::Entity menu)
{
    const scene::Entity separator = add(menu, "Separator", rects::wide(1.0f), "separator");
    scene().add<scene::UiImage>(separator, scene::UiImage{.raycastTarget = false});
    return separator;
}

void PanelBuilder::fitMenu(scene::Entity menu, float width)
{
    // The layout skips hidden entries: the menu takes the height of those it shows.
    float height = font * 0.6f;
    std::size_t shown = 0;
    for (scene::Entity child = scene().firstChild(menu); child.isValid(); child = scene().nextSibling(child))
    {
        const scene::UiRect& rect = scene().get<scene::UiRect>(child);
        if (rect.visible)
        {
            height += rect.offsetMax.y - rect.offsetMin.y;
            ++shown;
        }
    }
    height += static_cast<float>(shown > 0 ? shown - 1 : 0);
    scene::UiRect& rect = scene().get<scene::UiRect>(menu);
    const float room = panel.size().x > 0.0f ? panel.size().x : width;
    rect.offsetMax = rect.offsetMin + math::Vec2{std::min(width, room), height};
}

void PanelBuilder::styleTooltips(const ThemeColors& colors)
{
    panel.world().setTooltipStyle(ui::TooltipStyle{.background = linearColor(colors.popup),
                                                   .text = linearColor(colors.text),
                                                   .font = EditorUiKit::regularFont(),
                                                   .size = font,
                                                   .padding = font * 0.45f,
                                                   .cornerRadius = 4.0f});
}

} // namespace devex::tools::detail
