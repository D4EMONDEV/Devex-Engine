// The dock of the editor, made with the interface of the engine: the places of the panels, their tabs,
// the bars that share the room between them, and where a tab dragged away goes.
#include "EditorDock.hpp"

#include "EditorFrame.hpp"
#include "EditorUi.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using serialization::TextValue;
using namespace rects;

namespace {

constexpr std::uint32_t dockSurface = 22;
// What a tab carries while it is dragged to another place.
constexpr const char* dockDrag = "dock panel";
constexpr const char* dockWindow = "##dock";
// How far a tab goes under its panel, which hides its lower corners.
constexpr float tabOverlap = 6.0f;

// A panel the dock holds: the flag that shows it, whether its tab closes it, and where it goes first.
struct DockPanel
{
    const char* name;
    bool ToolsState::* shown;
    bool closable;
    bool editorOnly;
    DockSlot slot;
};

const std::array<DockPanel, 10> dockPanels{{
    {hierarchyWindow, &ToolsState::showHierarchy, false, false, DockSlot::LeftTop},
    {assetsWindow, &ToolsState::showAssets, false, false, DockSlot::LeftBottom},
    {inspectorWindow, &ToolsState::showInspector, false, false, DockSlot::RightTop},
    {consoleWindow, &ToolsState::showConsole, false, false, DockSlot::Bottom},
    {statisticsWindow, &ToolsState::showStatistics, false, false, DockSlot::Bottom},
    {profilerWindow, &ToolsState::showProfiler, true, false, DockSlot::Bottom},
    {animationWindow, &ToolsState::showAnimation, true, true, DockSlot::Bottom},
    {animatorWindow, &ToolsState::showAnimator, true, true, DockSlot::Bottom},
    {shaderGraphWindow, &ToolsState::showShaderGraph, true, true, DockSlot::Bottom},
    {translationsWindow, &ToolsState::showTranslations, true, true, DockSlot::Bottom},
}};

constexpr std::array<const char*, dockSlotCount> slotNames{"left_top", "left_bottom", "right_top", "right_bottom", "bottom"};

[[nodiscard]] const DockPanel* dockPanel(std::string_view name) noexcept
{
    const auto found = std::ranges::find_if(dockPanels, [&](const DockPanel& panel) { return name == panel.name; });
    return found != dockPanels.end() ? &*found : nullptr;
}

[[nodiscard]] std::size_t indexOf(DockSlot slot) noexcept
{
    return static_cast<std::size_t>(slot);
}

[[nodiscard]] float numberOf(const serialization::TextSection& section, std::string_view key, float fallback)
{
    const TextValue* const value = section.findAttribute(key);
    const std::optional<double> number = value != nullptr ? serialization::asNumber(*value) : std::nullopt;
    return number ? std::clamp(static_cast<float>(*number), 0.02f, 0.95f) : fallback;
}

[[nodiscard]] DockRect rectOf(float left, float top, float right, float bottom, bool visible = true) noexcept
{
    return DockRect{.min = math::Vec2(left, top), .max = math::Vec2(std::max(right, left), std::max(bottom, top)), .visible = visible};
}

} // namespace

DockLayout DockLayout::defaults(bool editor)
{
    DockLayout layout;
    for (const DockPanel& panel : dockPanels)
    {
        if (editor || !panel.editorOnly)
        {
            layout.adopt(panel.name, editor);
        }
    }
    return layout;
}

std::optional<DockSlot> DockLayout::slotOf(std::string_view panel) const
{
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        if (std::ranges::find(panels[slot], panel) != panels[slot].end())
        {
            return static_cast<DockSlot>(slot);
        }
    }
    return std::nullopt;
}

void DockLayout::move(std::string_view panel, DockSlot to)
{
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        std::erase(panels[slot], panel);
        if (front[slot] == panel)
        {
            front[slot] = panels[slot].empty() ? std::string{} : panels[slot].front();
        }
    }
    panels[indexOf(to)].emplace_back(panel);
    front[indexOf(to)] = std::string(panel);
}

void DockLayout::bringToFront(std::string_view panel)
{
    if (const std::optional<DockSlot> slot = slotOf(panel))
    {
        front[indexOf(*slot)] = std::string(panel);
    }
}

void DockLayout::adopt(std::string_view panel, bool editor)
{
    if (slotOf(panel))
    {
        return;
    }
    const DockPanel* const known = dockPanel(panel);
    if (known != nullptr && known->editorOnly && !editor)
    {
        return;
    }
    const std::size_t slot = indexOf(known != nullptr ? known->slot : DockSlot::Bottom);
    panels[slot].emplace_back(panel);
    if (front[slot].empty())
    {
        front[slot] = std::string(panel);
    }
}

serialization::TextSection DockLayout::write() const
{
    serialization::TextSection section;
    section.type = "dock";
    for (const auto& [key, value] : {std::pair<const char*, float>{"left", left},
                                     {"right", right},
                                     {"bottom", bottom},
                                     {"left_cut", leftCut},
                                     {"right_cut", rightCut}})
    {
        section.attributes.push_back({key, TextValue(std::round(static_cast<double>(value) * 10000.0) / 10000.0)});
    }
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        std::vector<TextValue> names(panels[slot].begin(), panels[slot].end());
        section.properties.push_back({slotNames[slot], serialization::makeCall("list", std::move(names))});
        if (!front[slot].empty())
        {
            section.properties.push_back({std::string(slotNames[slot]) + "_front", TextValue(front[slot])});
        }
    }
    return section;
}

std::optional<DockLayout> DockLayout::read(const serialization::TextSection& section)
{
    if (section.type != "dock")
    {
        return std::nullopt;
    }
    DockLayout layout;
    layout.left = numberOf(section, "left", layout.left);
    layout.right = numberOf(section, "right", layout.right);
    layout.bottom = numberOf(section, "bottom", layout.bottom);
    layout.leftCut = numberOf(section, "left_cut", layout.leftCut);
    layout.rightCut = numberOf(section, "right_cut", layout.rightCut);
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        const TextValue* const value = section.findProperty(slotNames[slot]);
        if (const serialization::TextCall* const list = value != nullptr ? serialization::asCall(*value, "list") : nullptr)
        {
            for (const TextValue& name : list->arguments)
            {
                // A panel is in one place only, the first one that names it.
                if (const std::string* const text = serialization::asString(name); text != nullptr && !layout.slotOf(*text))
                {
                    layout.panels[slot].push_back(*text);
                }
            }
        }
        const TextValue* const front = section.findProperty(std::string(slotNames[slot]) + "_front");
        const std::string* const chosen = front != nullptr ? serialization::asString(*front) : nullptr;
        layout.front[slot] = chosen != nullptr && std::ranges::find(layout.panels[slot], *chosen) != layout.panels[slot].end() ? *chosen
                             : layout.panels[slot].empty()                                                              ? std::string{}
                                                                                                                         : layout.panels[slot].front();
    }
    return layout;
}

DockPlaces placeDock(const DockLayout& layout, const std::function<bool(std::string_view)>& shown, math::Vec2 min, math::Vec2 max, float gap,
                     float tabHeight)
{
    DockPlaces places;
    places.gap = gap;
    places.tabHeight = tabHeight;
    places.area = rectOf(min.x, min.y, max.x, max.y);
    // The panel in front of each place: the one chosen, or the first one shown.
    std::array<bool, dockSlotCount> open{};
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        for (const std::string& panel : layout.panels[slot])
        {
            if (shown(panel) && (places.front[slot].empty() || panel == layout.front[slot]))
            {
                places.front[slot] = panel;
            }
        }
        open[slot] = !places.front[slot].empty();
    }
    const auto isOpen = [&](DockSlot slot) { return open[indexOf(slot)]; };

    // The room inside a margin as wide as the gaps between the places.
    const float x0 = min.x + gap;
    const float y0 = min.y + gap * 0.5f;
    const float x1 = max.x - gap;
    const float y1 = max.y - gap;
    const float width = std::max(x1 - x0, 1.0f);
    const float height = std::max(y1 - y0, 1.0f);
    const float least = tabHeight * 4.0f;
    const bool leftOpen = isOpen(DockSlot::LeftTop) || isOpen(DockSlot::LeftBottom);
    const bool rightOpen = isOpen(DockSlot::RightTop) || isOpen(DockSlot::RightBottom);
    const bool bottomOpen = isOpen(DockSlot::Bottom);
    const auto side = [&](float part) { return std::round(std::clamp(part * width, std::min(least, width * 0.3f), width * 0.42f)); };
    const float left = leftOpen ? side(layout.left) : 0.0f;
    const float right = rightOpen ? side(layout.right) : 0.0f;
    const float middleLeft = x0 + (leftOpen ? left + gap : 0.0f);
    const float middleRight = x1 - (rightOpen ? right + gap : 0.0f);
    const float below = bottomOpen ? std::round(std::clamp(layout.bottom * height, std::min(tabHeight * 3.0f, height * 0.3f), height * 0.8f)) : 0.0f;
    const float centerBottom = y1 - (bottomOpen ? below + gap : 0.0f);
    places.center = rectOf(middleLeft, y0, middleRight, centerBottom);

    // A side cut in two where both its places show a panel; otherwise the one that does takes it.
    const auto column = [&](DockSlot top, DockSlot lower, float from, float to, float cut, DockBar bar) {
        const bool both = isOpen(top) && isOpen(lower);
        const float split = both ? std::round(std::clamp(cut * (height - gap), tabHeight * 2.0f, std::max(height - gap - tabHeight * 2.0f, tabHeight * 2.0f)))
                                 : 0.0f;
        if (both)
        {
            places.slots[indexOf(top)] = rectOf(from, y0, to, y0 + split);
            places.slots[indexOf(lower)] = rectOf(from, y0 + split + gap, to, y1);
            places.bars[static_cast<std::size_t>(bar)] = rectOf(from, y0 + split, to, y0 + split + gap);
        }
        else if (isOpen(top) || isOpen(lower))
        {
            places.slots[indexOf(isOpen(top) ? top : lower)] = rectOf(from, y0, to, y1);
        }
        // Where a tab dropped would open the other place: its third of the side, or the side of the
        // screens where the side is closed.
        const float third = height / 3.0f;
        const float strip = std::min(width * 0.12f, least);
        const bool towardsLeft = top == DockSlot::LeftTop;
        const float edgeFrom = towardsLeft ? middleLeft : middleRight - strip;
        const float edgeTo = towardsLeft ? middleLeft + strip : middleRight;
        const float middleY = y0 + (centerBottom - y0) * 0.5f;
        for (const DockSlot slot : {top, lower})
        {
            const bool upper = slot == top;
            DockRect& target = places.targets[indexOf(slot)];
            if (isOpen(slot))
            {
                target = places.slots[indexOf(slot)];
            }
            else if (isOpen(top) || isOpen(lower))
            {
                target = upper ? rectOf(from, y0, to, y0 + third) : rectOf(from, y1 - third, to, y1);
            }
            else
            {
                target = upper ? rectOf(edgeFrom, y0, edgeTo, middleY) : rectOf(edgeFrom, middleY, edgeTo, centerBottom);
            }
        }
    };
    column(DockSlot::LeftTop, DockSlot::LeftBottom, x0, x0 + left, layout.leftCut, DockBar::LeftCut);
    column(DockSlot::RightTop, DockSlot::RightBottom, x1 - right, x1, layout.rightCut, DockBar::RightCut);
    if (leftOpen)
    {
        places.bars[static_cast<std::size_t>(DockBar::Left)] = rectOf(x0 + left, y0, x0 + left + gap, y1);
    }
    if (rightOpen)
    {
        places.bars[static_cast<std::size_t>(DockBar::Right)] = rectOf(x1 - right - gap, y0, x1 - right, y1);
    }
    if (bottomOpen)
    {
        places.slots[indexOf(DockSlot::Bottom)] = rectOf(middleLeft, y1 - below, middleRight, y1);
        places.bars[static_cast<std::size_t>(DockBar::Bottom)] = rectOf(middleLeft, centerBottom, middleRight, y1 - below);
        places.targets[indexOf(DockSlot::Bottom)] = places.slots[indexOf(DockSlot::Bottom)];
    }
    else
    {
        places.targets[indexOf(DockSlot::Bottom)] = rectOf(middleLeft, centerBottom - std::min(height * 0.2f, least), middleRight, centerBottom);
    }
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        const DockRect& whole = places.slots[slot];
        places.contents[slot] = rectOf(whole.min.x, whole.min.y + tabHeight, whole.max.x, whole.max.y, whole.visible);
    }
    return places;
}

void dragDockBar(DockLayout& layout, const DockPlaces& places, DockBar bar, math::Vec2 point, math::Vec2 grab)
{
    const float gap = places.gap;
    const float x0 = places.area.min.x + gap;
    const float y0 = places.area.min.y + gap * 0.5f;
    const float x1 = places.area.max.x - gap;
    const float y1 = places.area.max.y - gap;
    const float width = std::max(x1 - x0, 1.0f);
    const float height = std::max(y1 - y0, 1.0f);
    const auto part = [](float value) { return std::clamp(value, 0.02f, 0.95f); };
    switch (bar)
    {
    case DockBar::Left:
        layout.left = part((point.x - grab.x - x0) / width);
        break;
    case DockBar::Right:
        layout.right = part((x1 - (point.x - grab.x + gap)) / width);
        break;
    case DockBar::Bottom:
        layout.bottom = part((y1 - (point.y - grab.y + gap)) / height);
        break;
    case DockBar::LeftCut:
        layout.leftCut = part((point.y - grab.y - y0) / std::max(height - gap, 1.0f));
        break;
    case DockBar::RightCut:
        layout.rightCut = part((point.y - grab.y - y0) / std::max(height - gap, 1.0f));
        break;
    }
}

// The places of the dock and their tabs, drawn behind the windows of the panels, and in front of
// them while a tab is carried, to show where it would go.
struct EditorDockUi : PanelBuilder
{
    EditorDockUi()
        : PanelBuilder(dockSurface)
    {
    }

    struct Tab
    {
        Entity entity;
        Entity label;
        Entity close;
        Entity overline;
        std::string name;
        DockSlot slot = DockSlot::Bottom;
        float width = 0.0f;
    };

    bool built = false;
    float builtFont = 0.0f;
    Entity background;
    // The tabs, under the zones that show where a carried one goes.
    Entity tabLayer;
    std::array<Entity, dockSlotCount> strips{};
    std::array<Entity, dockSlotCount> zones{};
    std::array<Entity, dockBarCount> bars{};
    std::vector<Tab> tabs;
    std::string tabsKey;
    std::optional<DockBar> dragged;
    math::Vec2 grab{0.0f, 0.0f};

    void build();
    void fillTabs(ToolsState& state, EditorUiKit& kit);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
    [[nodiscard]] bool carrying()
    {
        const ui::Carried* const carried = panel.world().carried();
        return carried != nullptr && carried->type == dockDrag;
    }
};

void EditorDockUi::build()
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    tabs.clear();
    tabsKey.clear();
    panel.setKeyboardNavigation(false);
    const auto piece = [&](const char* name) {
        const Entity made = add({}, name, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .visible = false});
        scene().add<scene::UiImage>(made, scene::UiImage{.raycastTarget = false});
        return made;
    };
    background = piece("Background");
    for (Entity& strip : strips)
    {
        strip = piece("Strip");
    }
    for (Entity& bar : bars)
    {
        bar = piece("Bar");
    }
    tabLayer = add({}, "Tabs", whole());
    // Last, over the tabs: where a carried tab goes.
    for (Entity& zone : zones)
    {
        zone = piece("Zone");
        scene().get<scene::UiImage>(zone).raycastTarget = true;
        scene().add<scene::UiDropTarget>(zone, scene::UiDropTarget{.accepts = {dockDrag}});
    }
}

void EditorDockUi::fillTabs(ToolsState& state, EditorUiKit& kit)
{
    for (const Tab& tab : tabs)
    {
        scene().destroyEntity(tab.entity);
    }
    tabs.clear();
    const float closeSize = std::round(font * 1.2f);
    const float spacing = font * 0.35f;
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        for (const std::string& name : state.dock.panels[slot])
        {
            const DockPanel* const known = dockPanel(name);
            if (known == nullptr || !(state.*known->shown))
            {
                continue;
            }
            Tab tab{.name = name, .slot = static_cast<DockSlot>(slot)};
            const float labelWidth = std::ceil(kit.textWidth(EditorUiKit::regularFont(), name, font)) + 2.0f;
            const float padding = std::round(font * 0.8f);
            tab.width = std::round(padding * 2.0f + labelWidth + (known->closable ? spacing + closeSize : 0.0f));
            tab.entity = add(tabLayer, "Tab", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}}, "tab");
            scene().add<scene::UiImage>(tab.entity);
            scene().add<scene::UiButton>(tab.entity);
            scene().add<scene::UiDragSource>(tab.entity, scene::UiDragSource{.type = dockDrag, .data = name, .label = name});
            const Entity content = add(tab.entity, "Content", whole(math::Vec4{padding, 0.0f, padding - (known->closable ? spacing : 0.0f), tabOverlap}));
            scene().add<scene::UiLayout>(content, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = spacing, .align = scene::TextAlign::Left});
            tab.label = text(content, middle({labelWidth, font * 1.6f}), name, "dim");
            if (known->closable)
            {
                tab.close = add(content, "Close", middle({closeSize, closeSize}), "bar_button");
                scene().add<scene::UiImage>(tab.close);
                scene().add<scene::UiButton>(tab.close);
                icon(kit, tab.close, whole(math::Vec4{std::round(closeSize * 0.2f)}), Icon::Close, "icon_dim");
                tooltip(tab.close, "Close");
            }
            tab.overline = add(tab.entity, "Overline",
                               UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {5.0f, 0.0f}, .offsetMax = {-5.0f, 2.0f}, .visible = false},
                               "mark");
            scene().add<scene::UiImage>(tab.overline, scene::UiImage{.raycastTarget = false});
            tabs.push_back(std::move(tab));
        }
    }
}

void EditorDockUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        font = state.theme.fontSize;
        build();
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    const bool editor = state.mode == ToolsMode::Editor;
    const DockPlaces& places = state.dockPlaces;
    const float pixelsPerPoint = editorScreen().pixelsPerPoint;
    const float zoom = UiPanel::zoomFor(font);
    const float unitsPerPoint = pixelsPerPoint / zoom;
    const auto units = [&](math::Vec2 point) {
        return math::Vec2{std::round((point.x - places.area.min.x) * unitsPerPoint), std::round((point.y - places.area.min.y) * unitsPerPoint)};
    };
    const auto place = [&](Entity entity, const DockRect& rect) {
        UiRect& placed = scene().get<UiRect>(entity);
        placed.visible = rect.visible;
        placed.offsetMin = units(rect.min);
        placed.offsetMax = units(rect.max);
    };

    // The tabs are made again when the panels shown change.
    std::string key;
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        for (const std::string& name : state.dock.panels[slot])
        {
            const DockPanel* const known = dockPanel(name);
            if (known != nullptr && state.*known->shown)
            {
                key += name;
                key += '|';
            }
        }
        key += '#';
    }
    if (key != tabsKey)
    {
        fillTabs(state, kit);
        tabsKey = std::move(key);
    }

    // The gaps between the places show the color around the panels, as in Godot; over a game, only
    // the places do, and the game shows between them.
    // While a tab is carried the dock stands in front of the panels, which show through it.
    const bool carried = carrying();
    place(background, places.area);
    scene().get<UiRect>(background).visible = editor && !carried;
    scene().get<scene::UiImage>(background).color = linearColor(colors.outer);
    const Entity hovered = world.hovered();
    std::array<float, dockSlotCount> used{};
    const float tall = std::round(places.tabHeight * unitsPerPoint);
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        const DockRect& whole = places.slots[slot];
        place(strips[slot], DockRect{.min = whole.min, .max = math::Vec2(whole.max.x, whole.min.y + places.tabHeight), .visible = whole.visible && !editor});
        scene().get<scene::UiImage>(strips[slot]).color = linearColor(colors.outer);
    }
    for (const Tab& tab : tabs)
    {
        const std::size_t slot = indexOf(tab.slot);
        const DockRect& whole = places.slots[slot];
        const math::Vec2 origin = units(whole.min);
        const float room = std::round((whole.max.x - whole.min.x) * unitsPerPoint);
        UiRect& rect = scene().get<UiRect>(tab.entity);
        // Tabs that do not fit wait at the end of the strip; the one in front always shows.
        const bool front = places.front[slot] == tab.name;
        rect.visible = whole.visible && (used[slot] + tab.width <= room || front);
        rect.offsetMin = origin + math::Vec2{std::min(used[slot], std::max(room - tab.width, 0.0f)), 0.0f};
        rect.offsetMax = rect.offsetMin + math::Vec2{tab.width, tall + tabOverlap};
        used[slot] += rect.visible ? tab.width + 2.0f : 0.0f;
        rect.style = front ? "tab_selected" : hovered == tab.entity ? "tab_hover" : "tab";
        scene().get<UiRect>(tab.label).style = front ? "text" : "dim";
        scene().get<UiRect>(tab.overline).visible = front && state.hosts.isFocused(tab.name);
        if (tab.close.isValid())
        {
            scene().get<UiRect>(tab.close).style = barStyle(world, tab.close, false);
        }
    }

    // The bars light up under the pointer, and while they are dragged.
    const math::Vec2 mouse(state.input.mouse().x, state.input.mouse().y);
    std::optional<DockBar> pointed;
    for (std::size_t bar = 0; bar < dockBarCount; ++bar)
    {
        place(bars[bar], places.bars[bar]);
        if (!dragged && panel.hovered() && places.bars[bar].contains(mouse))
        {
            pointed = static_cast<DockBar>(bar);
        }
        const bool lit = dragged == static_cast<DockBar>(bar) || pointed == static_cast<DockBar>(bar);
        scene().get<scene::UiImage>(bars[bar]).color = linearColor(math::Vec4(colors.accent.x, colors.accent.y, colors.accent.z, lit ? 0.45f : 0.0f));
    }

    // Where a carried tab would go: the places, and where the empty ones would open.
    for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
    {
        place(zones[slot], places.targets[slot]);
        scene().get<UiRect>(zones[slot]).visible = carried && places.targets[slot].visible;
        scene().get<scene::UiImage>(zones[slot]).color =
            linearColor(math::Vec4(colors.accent.x, colors.accent.y, colors.accent.z, places.slots[slot].visible ? 0.0f : 0.12f));
        scene().get<scene::UiDropTarget>(zones[slot]).highlightColor = linearColor(math::Vec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.25f));
    }

    panel.update(kit, delta, zoom);

    // A click on a tab brings its panel to the front and gives it the keyboard; its cross hides it.
    for (const Tab& tab : tabs)
    {
        if (tab.close.isValid() && world.wasClicked(tab.close))
        {
            if (const DockPanel* const known = dockPanel(tab.name))
            {
                state.*known->shown = false;
            }
        }
        else if (world.wasClicked(tab.entity))
        {
            focusPanel(state, tab.name);
        }
    }
    if (const ui::Drop* const drop = world.dropped(); drop != nullptr && drop->type == dockDrag)
    {
        for (std::size_t slot = 0; slot < dockSlotCount; ++slot)
        {
            if (drop->target == zones[slot] && state.dock.slotOf(drop->data) != static_cast<DockSlot>(slot))
            {
                state.dock.move(drop->data, static_cast<DockSlot>(slot));
                focusPanel(state, drop->data);
            }
        }
    }

    // A bar follows the pointer while the button holds it.
    if (!dragged && pointed && state.input.clicked(Mouse::Left))
    {
        dragged = pointed;
        grab = math::Vec2(mouse.x - places.bars[static_cast<std::size_t>(*pointed)].min.x, mouse.y - places.bars[static_cast<std::size_t>(*pointed)].min.y);
    }
    if (dragged)
    {
        if (state.input.down(Mouse::Left))
        {
            dragDockBar(state.dock, places, *dragged, mouse, grab);
        }
        else
        {
            dragged.reset();
            state.dockChanged = true;
        }
    }
    if (const std::optional<DockBar> shownBar = dragged ? dragged : pointed)
    {
        const bool across = *shownBar == DockBar::Left || *shownBar == DockBar::Right;
        state.input.cursor = across ? platform::Cursor::ResizeHorizontal : platform::Cursor::ResizeVertical;
    }
}

void drawEditorDock(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Dock");
    const bool editor = state.mode == ToolsMode::Editor;
    if (state.resetLayout)
    {
        state.dock = DockLayout::defaults(editor);
        state.resetLayout = false;
        state.dockChanged = true;
    }
    for (const DockPanel& panel : dockPanels)
    {
        state.dock.adopt(panel.name, editor);
    }
    const auto shown = [&](std::string_view name) {
        const DockPanel* const known = dockPanel(name);
        return known != nullptr && state.*known->shown;
    };
    const auto placeAll = [&] {
        const ThemeMetrics& metrics = themeMetrics();
        state.dockPlaces = placeDock(state.dock, shown, state.workMin, state.workMax, metrics.dockGap, metrics.tabHeight);
    };
    placeAll();

    EditorUiKit& kit = editorUiKit(state);
    if (!state.dockUi)
    {
        state.dockUi = std::make_shared<EditorDockUi>();
    }
    EditorDockUi& dock = *state.dockUi;
    const DockRect& area = state.dockPlaces.area;
    // Behind the panels, or in front of them while a tab is carried over them; over a game, the
    // screens let the pointer through to it. The dock never takes the keyboard.
    HostOptions options{.focusable = false};
    if (!editor)
    {
        options.hole = std::pair{state.dockPlaces.center.min, state.dockPlaces.center.max};
    }
    state.hosts.begin(dockWindow, area.min, area.max, dock.carrying() ? HostLayer::Strips : HostLayer::Dock, options);
    dock.update(state, kit, core::Duration(state.input.delta()));
    state.hosts.end();
    // What the tabs and the bars changed shows this frame.
    placeAll();
    const bool held = state.input.down(Mouse::Left) || state.input.down(Mouse::Right) || state.input.down(Mouse::Middle);
    if (state.dockChanged && !held)
    {
        state.dockChanged = false;
        if (editor)
        {
            saveEditorSettings(state);
        }
    }
}

void renderEditorDock(ToolsState& state, render::RenderWorld& world)
{
    if (state.dockUi && state.uiKit)
    {
        // Clear where the places leave room for the panels and the game.
        state.dockUi->panel.render(*state.uiKit, world, math::Vec4{0.0f});
    }
}

bool beginDockedPanel(ToolsState& state, const char* name, bool bare)
{
    const DockPlaces& places = state.dockPlaces;
    DockRect where;
    if (std::string_view(name) == viewportWindow || std::string_view(name) == textEditorWindow)
    {
        // The screens share the middle: the one the menu bar shows fills it.
        if (state.mode != ToolsMode::Editor || std::string_view(windowOf(state.mainScreen)) != name)
        {
            return false;
        }
        where = places.center;
    }
    else
    {
        const std::optional<DockSlot> slot = state.dock.slotOf(name);
        if (!slot || places.front[indexOf(*slot)] != name)
        {
            return false;
        }
        where = places.contents[indexOf(*slot)];
    }
    if (!where.visible || where.max.x - where.min.x < 1.0f || where.max.y - where.min.y < 1.0f)
    {
        return false;
    }
    if (state.panelToFocus == name)
    {
        state.hosts.focus(name);
        state.panelToFocus.clear();
    }
    // On the colour of the panels and inside a margin, or edge to edge on the colour around them.
    const ThemeColors& colors = themeColors();
    state.hosts.begin(name, where.min, where.max, HostLayer::Panels,
                      HostOptions{.background = linearColor(bare ? colors.outer : colors.panel),
                                  .padding = bare ? 0.0f : themeMetrics().panelPadding});
    return true;
}

void endDockedPanel(ToolsState& state)
{
    state.hosts.end();
}

void focusPanel(ToolsState& state, std::string_view name)
{
    if (const DockPanel* const known = dockPanel(name))
    {
        state.*known->shown = true;
    }
    if (const std::optional<DockSlot> slot = state.dock.slotOf(name); slot && state.dock.front[indexOf(*slot)] != name)
    {
        state.dock.bringToFront(name);
        state.dockChanged = true;
    }
    state.panelToFocus = std::string(name);
}

} // namespace devex::tools::detail
