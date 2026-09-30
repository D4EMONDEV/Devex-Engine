// The layer over the whole window of the editor: the menus of its menu bar, which are the popup menus
// of the interface of the engine as the context menus of the panels are, and the tooltips of the
// strips too thin to draw them. It is drawn only while a menu is open or a tooltip shows, and takes
// the mouse only for a menu.
#include "EditorFrame.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/ui/TextLayout.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

// The layer and the entities the code reads and changes.
struct EditorLayerUi : PanelBuilder
{
    EditorLayerUi()
        : PanelBuilder(editorLayerSurface)
    {
    }

    // An entry of a menu as it shows.
    struct Item
    {
        Button button;
        std::size_t entry = 0;
    };

    bool built = false;
    float builtFont = 0.0f;
    // The menu: its list, and the menu an entry of it opens beside it, inside the same popup so that
    // a press in one does not close the other.
    Entity menu;
    Entity list;
    Entity sub;
    std::vector<Item> items;
    std::vector<Item> subItems;
    std::vector<MenuEntry> entries;
    std::optional<std::size_t> owner;
    std::optional<std::size_t> openSub;
    // A menu asked for, shown at the next update of the layer.
    bool pending = false;
    ImVec2 pendingAt{0.0f, 0.0f};
    // Where the open menu stands, in units of the layer.
    math::Vec2 placedAt{0.0f};
    Entity tip;
    Entity tipText;
    // The window the keyboard was in before a strip or a menu took it, which takes it back once the
    // menu closes or the strip is let go.
    ImGuiWindow* working = nullptr;
    bool holdsFocus = false;

    void build();
    void giveFocusBack();
    [[nodiscard]] bool menuOpen();
    // Makes the entries of a list, and returns the size the list takes.
    math::Vec2 fill(EditorUiKit& kit, Entity parent, const std::vector<MenuEntry>& shown, std::vector<Item>& made);
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta, const std::optional<EditorUiKit::Tooltip>& tooltip);
};

void EditorLayerUi::build()
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        panel.world().closePopup(scene(), child);
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    items.clear();
    subItems.clear();
    openSub.reset();

    menu = add({}, "Menu", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {font * 12.0f, font * 2.0f},
                                  .visible = false});
    scene().add<scene::UiPopup>(menu);
    const auto column = [&](const char* name) {
        const Entity made = add(menu, name, whole(), "popup");
        scene().add<scene::UiImage>(made);
        scene().add<scene::UiLayout>(made, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                          .spacing = 1.0f,
                                                          .padding = math::Vec4{font * 0.3f},
                                                          .align = scene::TextAlign::Left});
        return made;
    };
    list = column("List");
    sub = column("Inside");
    scene().get<UiRect>(sub).visible = false;

    // The tooltip of a strip, which never takes the pointer.
    tip = add({}, "Tooltip", fixed({font * 8.0f, font * 2.0f}), "tooltip");
    scene().get<UiRect>(tip).visible = false;
    scene().add<scene::UiImage>(tip, scene::UiImage{.raycastTarget = false});
    tipText = text(tip, whole(math::Vec4{std::round(font * 0.45f)}), "", "text");
    scene().get<scene::UiText>(tipText).wrap = true;
    scene().get<scene::UiText>(tipText).verticalAlign = scene::TextVerticalAlign::Top;
}

void EditorLayerUi::giveFocusBack()
{
    if (std::exchange(holdsFocus, false) && working != nullptr && working->WasActive)
    {
        ImGui::FocusWindow(working);
    }
}

bool EditorLayerUi::menuOpen()
{
    return pending || (built && panel.world().isPopupOpen(scene(), menu));
}

math::Vec2 EditorLayerUi::fill(EditorUiKit& kit, Entity parent, const std::vector<MenuEntry>& shown, std::vector<Item>& made)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(parent); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    made.clear();

    const float rowHeight = std::round(font * 1.9f);
    const float iconSize = std::round(font * 1.1f);
    const float padding = font * 0.3f;
    // As wide as its widest entry: the icon, the label, and what stands at the right of it.
    float labels = 0.0f;
    float rights = 0.0f;
    for (const MenuEntry& entry : shown)
    {
        if (entry.separator)
        {
            continue;
        }
        labels = std::max(labels, kit.textWidth(EditorUiKit::regularFont(), entry.label, font));
        rights = std::max(rights, entry.shortcut.empty() ? iconSize : kit.textWidth(EditorUiKit::regularFont(), entry.shortcut, font * 0.9f));
    }
    const float width = std::round(font * 1.4f + iconSize + font * 0.45f + labels + font * 2.0f + rights + padding * 2.0f);
    float height = padding * 2.0f;
    std::size_t rows = 0;
    for (std::size_t index = 0; index < shown.size(); ++index)
    {
        const MenuEntry& entry = shown[index];
        ++rows;
        if (entry.separator)
        {
            menuSeparator(parent);
            height += 1.0f;
            continue;
        }
        // Every entry keeps the room of an icon, so that the labels line up.
        Item item{.button = button(kit, parent, entry.icon.value_or(Icon::Circle), entry.label, "menu_item", -1.0f, rowHeight, scene::TextAlign::Left),
                  .entry = index};
        scene().get<UiRect>(item.button.label) = grow(rowHeight);
        scene().get<UiRect>(item.button.label).style = "text";
        if (!entry.shortcut.empty())
        {
            const Entity keys = add(item.button.entity, "Shortcut", middle({rights + 2.0f, rowHeight}), "dim");
            scene().add<scene::UiText>(keys, scene::UiText{.text = entry.shortcut,
                                                           .font = EditorUiKit::regularFont(),
                                                           .size = font * 0.9f,
                                                           .align = scene::TextAlign::Right,
                                                           .verticalAlign = scene::TextVerticalAlign::Middle,
                                                           .wrap = false});
        }
        else if (!entry.children.empty())
        {
            icon(kit, item.button.entity, middle({iconSize, iconSize}), Icon::ChevronRight, "icon_dim");
        }
        else if (entry.checked)
        {
            icon(kit, item.button.entity, middle({iconSize, iconSize}), Icon::Check, "icon_accent");
        }
        enable(item.button, entry.enabled);
        if (!entry.icon)
        {
            // The room of an icon, without one.
            scene().get<UiRect>(item.button.icon).opacity = 0.0f;
        }
        made.push_back(item);
        height += rowHeight;
    }
    height += static_cast<float>(rows > 0 ? rows - 1 : 0);
    return math::Vec2{width, std::round(height)};
}

void EditorLayerUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta,
                           const std::optional<EditorUiKit::Tooltip>& tooltip)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        font = state.theme.fontSize;
        build();
    }
    ui::UiWorld& world = panel.world();
    const float zoom = UiPanel::zoomFor(font);
    const float unitsPerPoint = (ImGui::GetIO().DisplayFramebufferScale.x > 0.0f ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f) / zoom;
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // A menu asked for since the last frame: its entries, under where it was asked.
    if (std::exchange(pending, false))
    {
        openSub.reset();
        scene().get<UiRect>(sub).visible = false;
        const math::Vec2 size = fill(kit, list, entries, items);
        UiRect& rect = scene().get<UiRect>(menu);
        rect.offsetMax = rect.offsetMin + size;
        placedAt = math::Vec2{(pendingAt.x - origin.x) * unitsPerPoint, (pendingAt.y - origin.y) * unitsPerPoint};
        world.openPopup(scene(), menu, placedAt);
    }

    // The tooltip of a strip, next to where the pointer rested, kept inside the window.
    UiRect& tipRect = scene().get<UiRect>(tip);
    tipRect.visible = tooltip.has_value() && !world.isPopupOpen(scene(), menu);
    if (tipRect.visible)
    {
        const float padding = std::round(font * 0.45f);
        const asset::FontData* const letters = kit.fontData(EditorUiKit::regularFont());
        const math::Vec2 room{ImGui::GetContentRegionAvail().x * unitsPerPoint, ImGui::GetContentRegionAvail().y * unitsPerPoint};
        const float wrapWidth = std::max(std::min(room.x - padding * 2.0f - 8.0f, font * 56.0f), font * 4.0f);
        const math::Vec2 measured =
            letters != nullptr ? ui::measureText(*letters, tooltip->text, ui::TextStyle{.size = font, .wrap = true}, wrapWidth) : math::Vec2{0.0f};
        const math::Vec2 size{std::ceil(measured.x) + padding * 2.0f + 1.0f, std::ceil(measured.y) + padding * 2.0f};
        const math::Vec2 rested{(tooltip->at.x - origin.x) * unitsPerPoint, (tooltip->at.y - origin.y) * unitsPerPoint};
        math::Vec2 at = rested + math::Vec2{font * 0.9f, font * 1.3f};
        at.x = std::clamp(at.x, 0.0f, std::max(room.x - size.x, 0.0f));
        at.y = at.y + size.y > room.y ? std::max(rested.y - size.y - 4.0f, 0.0f) : at.y;
        tipRect.offsetMin = at;
        tipRect.offsetMax = at + size;
        scene().get<scene::UiText>(tipText).text = tooltip->text;
    }

    panel.update(kit, delta, zoom);

    // What was chosen, done with the scene on screen now. A click on an entry closes the menu by
    // itself, which an entry that only holds a menu undoes.
    const auto clicked = [&](const std::vector<Item>& shown, const std::vector<MenuEntry>& from) -> const MenuEntry* {
        for (const Item& item : shown)
        {
            if (world.wasClicked(item.button.entity) && item.entry < from.size())
            {
                return &from[item.entry];
            }
        }
        return nullptr;
    };
    const MenuEntry* picked = clicked(items, entries);
    if (picked == nullptr && openSub && *openSub < entries.size())
    {
        picked = clicked(subItems, entries[*openSub].children);
    }
    if (picked != nullptr && !picked->children.empty())
    {
        world.openPopup(scene(), menu, placedAt);
    }
    else if (picked != nullptr)
    {
        // The entries go with the menu: what was chosen is kept while it runs.
        const std::function<void(ToolsState&, scene::Scene&)> action = picked->action;
        world.closePopup(scene(), menu);
        owner.reset();
        // Before the choice runs: it may bring a window of its own to the front.
        giveFocusBack();
        if (action)
        {
            action(state, edited);
        }
        return;
    }
    if (!world.isPopupOpen(scene(), menu))
    {
        owner.reset();
        return;
    }
    // An entry that holds a menu opens it beside itself while the pointer is on it; another entry of
    // the list closes it.
    const Entity hovered = world.hovered();
    for (const Item& item : items)
    {
        if (item.button.entity != hovered)
        {
            continue;
        }
        const MenuEntry& entry = entries[item.entry];
        if (entry.children.empty())
        {
            openSub.reset();
            scene().get<UiRect>(sub).visible = false;
        }
        else if (openSub != item.entry)
        {
            openSub = item.entry;
            const math::Vec2 size = fill(kit, sub, entry.children, subItems);
            const UiRect& row = scene().get<UiRect>(item.button.entity);
            // Beside the list, its first entry level with the entry that opened it.
            const ui::LaidOutRect* const placed = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(item.button.entity);
            const ui::LaidOutRect* const root = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(menu);
            const float top = placed != nullptr && root != nullptr ? placed->min.y - root->min.y - font * 0.3f : row.offsetMin.y;
            UiRect& rect = scene().get<UiRect>(sub);
            rect = UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {-2.0f, top}, .offsetMax = {-2.0f + size.x, top + size.y},
                          .style = "popup"};
        }
    }
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        scene().get<UiRect>(items[index].button.entity).style = openSub == items[index].entry ? "row_selected" : "menu_item";
    }
}

namespace {

// Whether a window is one of the frame of the editor rather than one a user works in.
[[nodiscard]] bool isFrameWindow(const ImGuiWindow& window) noexcept
{
    for (const char* const name : {"##menu bar", "##status bar", "##editor menus", "##editor tooltips"})
    {
        if (std::strcmp(window.Name, name) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void openEditorMenu(ToolsState& state, std::vector<MenuEntry> entries, ImVec2 at, std::size_t owner)
{
    if (!state.editorLayerUi)
    {
        state.editorLayerUi = std::make_shared<EditorLayerUi>();
    }
    EditorLayerUi& layer = *state.editorLayerUi;
    layer.holdsFocus = true;
    layer.entries = std::move(entries);
    layer.owner = owner;
    layer.pending = true;
    layer.pendingAt = at;
}

std::optional<std::size_t> editorMenuOwner(const ToolsState& state)
{
    return state.editorLayerUi && state.editorLayerUi->menuOpen() ? state.editorLayerUi->owner : std::nullopt;
}

void closeEditorMenu(ToolsState& state)
{
    if (state.editorLayerUi && state.editorLayerUi->built)
    {
        EditorLayerUi& layer = *state.editorLayerUi;
        layer.pending = false;
        layer.panel.world().closePopup(layer.scene(), layer.menu);
        layer.owner.reset();
        // The keyboard goes back where it was when the layer is next drawn.
    }
}

void drawEditorLayer(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Editor layer");
    if (!state.uiKit)
    {
        return;
    }
    EditorUiKit& kit = *state.uiKit;
    const std::optional<EditorUiKit::Tooltip> tooltip = kit.takeTooltip();
    if (!state.editorLayerUi)
    {
        state.editorLayerUi = std::make_shared<EditorLayerUi>();
    }
    EditorLayerUi& layer = *state.editorLayerUi;
    const bool menu = layer.menuOpen();
    // The strips and the menus borrow the keyboard: a click on the menu bar does not take it away
    // from the panel one was working in.
    if (ImGuiWindow* const focused = ImGui::GetCurrentContext()->NavWindow; focused != nullptr && !isFrameWindow(*focused))
    {
        layer.working = focused;
    }
    else if (focused != nullptr && !menu && !ImGui::IsAnyMouseDown())
    {
        layer.holdsFocus = true;
    }
    if (!menu)
    {
        layer.giveFocusBack();
    }
    if (!menu && !tooltip)
    {
        return;
    }

    // Over the whole window and every other one; without a menu, the mouse goes through it.
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;
    if (menu)
    {
        if (layer.pending)
        {
            ImGui::SetNextWindowFocus();
        }
    }
    else
    {
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool shown = ImGui::Begin(menu ? "##editor menus" : "##editor tooltips", nullptr, flags);
    ImGui::PopStyleVar(2);
    if (shown)
    {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        layer.update(state, kit, scene, core::Duration(ImGui::GetIO().DeltaTime), tooltip);
    }
    ImGui::End();
}

void renderEditorLayer(ToolsState& state, render::RenderWorld& world)
{
    if (state.editorLayerUi && state.uiKit)
    {
        // Clear around the menus and the tooltips, which the editor shows through.
        state.editorLayerUi->panel.render(*state.uiKit, world, math::Vec4{0.0f});
    }
}

} // namespace devex::tools::detail
