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

EditorUiKit::EditorUiKit(render::Renderer& renderer, const IconSet& icons, std::filesystem::path fontsDirectory)
    : m_renderer(renderer)
    , m_icons(icons)
    , m_fontsDirectory(std::move(fontsDirectory))
{
}

EditorUiKit::~EditorUiKit()
{
    for (const BakedFont* font : {&m_regular, &m_bold})
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
}

std::function<ui::FontRef(asset::AssetId)> EditorUiKit::fonts()
{
    bakeFonts();
    return [this](asset::AssetId id) {
        const BakedFont& font = id == boldFont() ? m_bold : m_regular;
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
                const auto found = m_iconTextures.find(id);
                return found != m_iconTextures.end() ? found->second : render::TextureHandle{};
            },
        .textureSize = [](asset::AssetId) { return math::Vec2{static_cast<float>(iconPixels)}; },
        .defaultFont = regularFont(),
    };
}

const asset::FontData* EditorUiKit::fontData(asset::AssetId font)
{
    bakeFonts();
    return (font == boldFont() ? m_bold : m_regular).data.get();
}

float EditorUiKit::textWidth(asset::AssetId font, std::string_view text, float size)
{
    const asset::FontData* const data = fontData(font);
    return data != nullptr ? ui::measureText(*data, text, ui::TextStyle{.size = size, .wrap = false}).x : 0.0f;
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

void UiPanel::update(EditorUiKit& kit, core::Duration delta, float zoom)
{
    const ImGuiIO& io = ImGui::GetIO();
    const float pixelsPerPoint = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    m_zoom = std::max(zoom, 0.1f);
    m_pixels = {static_cast<std::uint32_t>(std::max(std::floor(available.x * pixelsPerPoint), 1.0f)),
                static_cast<std::uint32_t>(std::max(std::floor(available.y * pixelsPerPoint), 1.0f))};
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(render::Renderer::uiSurfaceTexture(m_surface))), available);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
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
    input.pointerPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    input.pointerReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    input.pointerMoved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    input.secondaryPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    input.wheel = hovered ? io.MouseWheel : 0.0f;
    if (!hovered && !input.pointerDown)
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
        input.moveX = (stroke(ImGuiKey_RightArrow) ? 1 : 0) - (stroke(ImGuiKey_LeftArrow) ? 1 : 0);
        input.moveY = (stroke(ImGuiKey_DownArrow) ? 1 : 0) - (stroke(ImGuiKey_UpArrow) ? 1 : 0);
        input.submitPressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                              (!editing && ImGui::IsKeyPressed(ImGuiKey_Space, false));
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
        if (input.pastePressed)
        {
            if (const char* const clipboard = ImGui::GetClipboardText())
            {
                input.clipboard = clipboard;
            }
        }
    }
    m_world.update(m_scene, size(), input, delta);
    if (const std::string& copied = m_world.clipboardRequest(); !copied.empty())
    {
        ImGui::SetClipboardText(copied.c_str());
    }
    // While a field takes what is typed, the system sends the letters, as it does for an ImGui field,
    // and shows its input method next to the field.
    if (m_focused && m_world.isEditing() && !m_world.canvases().empty())
    {
        ImGuiContext& context = *ImGui::GetCurrentContext();
        context.PlatformImeData.WantVisible = true;
        context.PlatformImeData.WantTextInput = true;
        context.PlatformImeData.InputLineHeight = ImGui::GetFontSize();
        context.PlatformImeData.ViewportId = ImGui::GetWindowViewport()->ID;
        if (const ui::LaidOutRect* const field = m_world.canvases().front().layout.find(m_world.editedField()))
        {
            context.PlatformImeData.InputPos =
                ImVec2(origin.x + field->min.x * m_zoom / pixelsPerPoint, origin.y + field->max.y * m_zoom / pixelsPerPoint);
        }
    }
    m_shown = true;
}

void UiPanel::render(EditorUiKit& kit, render::RenderWorld& world, math::Vec4 background)
{
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

} // namespace devex::tools::detail
