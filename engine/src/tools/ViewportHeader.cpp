// What stands above the view of the scene, made with the interface of the engine: the tabs of the open
// scenes, as Godot shows them, and under them the toolbar of the view, which gives way to a word on
// the game while it plays.
#include "EditorFrame.hpp"
#include "SettingsUi.hpp"

#include <devex/core/Path.hpp>
#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

// What a tab carries while it is dragged to another place among the tabs.
constexpr const char* tabDrag = "scene tab";
// How far the tabs go under the toolbar, which hides the lower corners of the one shown.
constexpr float tabOverlap = 6.0f;
// Who opened the menu of the layer over the editor: the menu bar counts from 0, the Script screen
// from 100.
constexpr std::size_t tabMenuOwner = 200;

// The menu of a scene tab, as Godot's: closing it, the others, those at its right or all of them,
// and finding its file. It holds the tabs by their ids, which stay the same when tabs move.
[[nodiscard]] std::vector<MenuEntry> tabMenu(const std::vector<std::uint64_t>& ids, std::size_t index, bool active, std::string resource)
{
    const auto closing = [](std::vector<std::uint64_t> closed) {
        return [closed = std::move(closed)](ToolsState& tools, scene::Scene& edited) {
            requestAction(tools, edited, {.kind = PendingAction::Kind::CloseTab, .tabs = closed});
        };
    };
    std::vector<std::uint64_t> others = ids;
    others.erase(others.begin() + static_cast<std::ptrdiff_t>(index));
    std::vector<MenuEntry> entries;
    entries.push_back({.icon = Icon::Close, .label = "Close Tab", .shortcut = active ? "Ctrl+W" : "", .action = closing({ids[index]})});
    entries.push_back({.label = "Close Other Tabs", .enabled = !others.empty(), .action = closing(std::move(others))});
    entries.push_back({.label = "Close Tabs to the Right",
                       .enabled = index + 1 < ids.size(),
                       .action = closing(std::vector<std::uint64_t>(ids.begin() + static_cast<std::ptrdiff_t>(index) + 1, ids.end()))});
    entries.push_back({.label = "Close All Tabs", .action = closing(ids)});
    entries.push_back(MenuEntry::line());
    const bool saved = !resource.empty();
    entries.push_back({.icon = Icon::FolderOpen,
                       .label = "Show in FileSystem",
                       .enabled = saved,
                       .action = [resource = std::move(resource)](ToolsState& tools, scene::Scene&) { revealInFileSystem(tools, resource); }});
    return entries;
}

} // namespace

struct ViewportHeaderUi : PanelBuilder
{
    ViewportHeaderUi()
        : PanelBuilder(viewportHeaderSurface)
    {
    }

    struct Tab
    {
        Entity entity;
        Entity glyph;
        Entity label;
        Entity unsaved;
        Entity close;
        Entity overline;
        std::uint64_t id = 0;
        // Where it stands in the row of the tabs.
        float start = 0.0f;
        float width = 0.0f;
    };

    bool built = false;
    float builtFont = 0.0f;
    // The tabs: a strip the wheel moves when they do not all fit, and the row inside it.
    Entity strip;
    Entity tabRow;
    std::vector<Tab> tabs;
    Entity newTab;
    float tabsWidth = 0.0f;
    // What the tabs were made from, and the tab last brought into view.
    std::string tabsKey;
    std::optional<std::uint64_t> revealed;

    Entity bar;
    Entity tools;
    Entity viewSide;
    Entity banner;
    Entity bannerIcon;
    Entity bannerText;
    std::array<Button, 4> toolButtons{};
    Button space;
    Button snap;
    Button grid;
    Button iconsShown;
    Button colliders;
    Button frame;
    Button interfaces;
    Button help;
    Entity speed;
    Entity speedText;
    Entity exposure;
    std::optional<bool> shownLocal;
    std::optional<bool> shownTwoD;

    [[nodiscard]] float tabsHeight() const noexcept
    {
        return std::round(font * 2.2f);
    }
    [[nodiscard]] float barHeight() const noexcept
    {
        return std::round(font * 2.45f);
    }
    [[nodiscard]] float tabsTop() const noexcept
    {
        return std::round(font * 0.35f);
    }
    [[nodiscard]] float sideMargin() const noexcept
    {
        return std::round(font * 0.5f);
    }
    // The button that adds a scene, which stays in view at the end of the tabs.
    [[nodiscard]] float newTabSize() const noexcept
    {
        return std::round(font * 1.35f) + 4.0f;
    }

    void build(EditorUiKit& kit);
    void fillTabs(ToolsState& state, EditorUiKit& kit, const ActiveDocument& live, bool editing);
    // The tab an element is, or is inside of.
    [[nodiscard]] std::optional<std::size_t> tabOf(Entity element);
    void light(const Button& target, bool on);
    // Writes a text and makes its rectangle as wide as it.
    void fit(EditorUiKit& kit, Entity entity, std::string_view value);
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
};

void ViewportHeaderUi::build(EditorUiKit& kit)
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
    shownLocal.reset();
    shownTwoD.reset();
    panel.setKeyboardNavigation(false);
    panel.setTooltipsOutside(true);

    const float tabsTall = tabsHeight();
    const float side = sideMargin();
    strip = add({}, "Tabs",
                UiRect{.anchorMin = {0.0f, 0.0f},
                       .anchorMax = {1.0f, 0.0f},
                       .offsetMin = {side, tabsTop()},
                       .offsetMax = {-side - newTabSize() - 2.0f, tabsTall + tabOverlap},
                       .clipChildren = true});
    scene().add<scene::UiScroll>(strip, scene::UiScroll{.horizontal = true, .vertical = false, .speed = font * 6.0f, .scrollbar = false});
    tabRow = add(strip, "Row", fixed({1.0f, tabsTall - tabsTop()}));
    scene().add<scene::UiLayout>(tabRow, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = 2.0f, .align = scene::TextAlign::Left});
    newTab = add({}, "New", fixed({newTabSize(), newTabSize()}), "bar_button");
    scene().add<scene::UiImage>(newTab);
    scene().add<scene::UiButton>(newTab);
    icon(kit, newTab, whole(math::Vec4{std::round(newTabSize() * 0.24f)}), Icon::Plus, "icon_dim");
    tooltip(newTab, "New scene (Ctrl+N)");

    // The toolbar, on the colour of the panels, over what the tabs leave under it.
    bar = add({}, "Toolbar", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, tabsTall}, .offsetMax = {0.0f, tabsTall + barHeight()}});
    scene().add<scene::UiImage>(bar);
    const float size = std::round(font * 1.75f);
    const float iconSize = std::round(font * 1.1f);
    const auto tool = [&](Entity parent, Icon glyph, const char* hint) {
        Button made;
        made.entity = add(parent, "Button", middle({size, size}), "bar_button");
        scene().add<scene::UiImage>(made.entity);
        scene().add<scene::UiButton>(made.entity);
        made.icon = icon(kit, made.entity, whole(math::Vec4{std::round(size * 0.2f)}), glyph, "icon");
        if (hint != nullptr)
        {
            tooltip(made.entity, hint);
        }
        return made;
    };
    const auto gap = [&](Entity parent) {
        const Entity room = add(parent, "Gap", middle({std::round(font * 0.9f), size}));
        const Entity rule = add(room, "Line", UiRect{.anchorMin = {0.5f, 0.2f}, .anchorMax = {0.5f, 0.8f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {1.0f, 0.0f}},
                                "separator");
        scene().add<scene::UiImage>(rule, scene::UiImage{.raycastTarget = false});
    };

    tools = add(bar, "Tools", whole(math::Vec4{side, 0.0f, side, 0.0f}));
    scene().add<scene::UiLayout>(tools, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = 2.0f, .align = scene::TextAlign::Left});
    toolButtons[0] = tool(tools, Icon::Pointer, "Select (Q)");
    toolButtons[1] = tool(tools, Icon::Move, "Move (W)");
    toolButtons[2] = tool(tools, Icon::Rotate, "Rotate (E)");
    toolButtons[3] = tool(tools, Icon::Scale, "Scale (R)");
    gap(tools);
    space = tool(tools, Icon::Globe, nullptr);
    snap = tool(tools, Icon::Magnet, "Snap by 0.5 m, 15° or 0.1 (Ctrl switches it while dragging)");
    gap(tools);
    grid = tool(tools, Icon::Grid, "Grid");
    iconsShown = tool(tools, Icon::Eye, "Light and camera icons");
    colliders = tool(tools, Icon::Scan, "Collision shapes of every entity (the selection always shows its own)");
    frame = tool(tools, Icon::Crosshair, "Frame the selection (F)");
    // The interfaces of a 3D scene show over its 3D screen, as the game will draw them; a menu that
    // covers the whole screen hides the scene, hence the button.
    interfaces = tool(tools, Icon::LayoutDashboard, "Interfaces over the scene, as the game draws them (they are edited in the 2D screen)");

    // What the view moves by and exposes for, and how it is driven, at the right.
    viewSide = add(bar, "View", whole(math::Vec4{side, 0.0f, side, 0.0f}));
    scene().add<scene::UiLayout>(viewSide, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.7f, .align = scene::TextAlign::Right});
    speed = add(viewSide, "Speed", middle({font * 5.0f, size}));
    icon(kit, speed, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -iconSize * 0.5f}, .offsetMax = {iconSize, iconSize * 0.5f}},
         Icon::Gauge, "icon_dim");
    speedText = text(speed, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {iconSize + font * 0.4f, 0.0f}, .offsetMax = {0.0f, 0.0f}}, "",
                     "dim");
    exposure = text(viewSide, middle({font * 4.0f, size}), "", "dim");
    tooltip(exposure, "Exposure of the editor camera");
    help = tool(viewSide, Icon::CircleHelp, nullptr);
    scene().get<UiRect>(help.icon).style = "icon_dim";

    // While the game plays: what it is doing, in the place of the tools.
    banner = add(bar, "Playing", whole(math::Vec4{side + font * 0.3f, 0.0f, side, 0.0f}));
    scene().add<scene::UiLayout>(banner, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.5f, .align = scene::TextAlign::Left});
    bannerIcon = icon(kit, banner, middle({iconSize, iconSize}), Icon::Play, "icon_accent");
    bannerText = text(banner, middle({font * 40.0f, size}), "", "accent");
}

void ViewportHeaderUi::fillTabs(ToolsState& state, EditorUiKit& kit, const ActiveDocument& live, bool editing)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(tabRow); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    tabs.clear();

    const float tall = tabsHeight() - tabsTop();
    const float iconSize = std::round(font * 1.1f);
    const float closeSize = std::round(font * 1.35f);
    const float dot = std::round(font * 0.5f);
    const float spacing = font * 0.45f;
    const float left = std::round(font * 0.75f);
    const float right = std::round(editing ? font * 0.4f : font * 0.75f);
    const bool movable = editing && state.tabs.size() > 1;
    float total = 0.0f;
    for (std::size_t index = 0; index < state.tabs.size(); ++index)
    {
        const std::filesystem::path& path = state.tabs.path(index, live);
        const std::string name = tabName(path);
        const bool modified = state.tabs.isModified(index, live) && (editing || index != state.tabs.active());
        const float labelWidth = std::ceil(kit.textWidth(EditorUiKit::regularFont(), name, font)) + 2.0f;
        Tab tab{.id = state.tabs.id(index), .start = total};
        tab.width = std::round(left + iconSize + spacing + labelWidth + (modified ? spacing + dot : 0.0f) +
                               (editing ? spacing + closeSize : 0.0f) + right);
        // As tall as the row and a little more, which the toolbar covers.
        tab.entity = add(tabRow, "Tab", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {tab.width, tabOverlap}},
                         "tab");
        scene().add<scene::UiImage>(tab.entity);
        scene().add<scene::UiButton>(tab.entity);
        const Entity content = add(tab.entity, "Content", whole(math::Vec4{left, 0.0f, right, tabOverlap}));
        scene().add<scene::UiLayout>(content, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = spacing, .align = scene::TextAlign::Left});
        tab.glyph = icon(kit, content, middle({iconSize, iconSize}), Icon::Clapperboard, "icon_dim");
        tab.label = text(content, middle({labelWidth, tall}), name, "dim");
        if (modified)
        {
            // A dot for what is not saved, as the tabs of a text editor show it.
            tab.unsaved = add(content, "Unsaved", middle({dot, dot}));
            scene().add<scene::UiImage>(tab.unsaved, scene::UiImage{.cornerRadius = dot * 0.5f, .raycastTarget = false});
        }
        if (editing)
        {
            tab.close = add(content, "Close", middle({closeSize, closeSize}), "bar_button");
            scene().add<scene::UiImage>(tab.close);
            scene().add<scene::UiButton>(tab.close);
            icon(kit, tab.close, whole(math::Vec4{std::round(closeSize * 0.2f)}), Icon::Close, "icon_dim");
        }
        tab.overline = add(tab.entity, "Overline",
                           UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {5.0f, 0.0f}, .offsetMax = {-5.0f, 2.0f}, .visible = false},
                           "mark");
        scene().add<scene::UiImage>(tab.overline, scene::UiImage{.raycastTarget = false});
        if (!path.empty())
        {
            tooltip(tab.entity, core::toUtf8(path));
        }
        if (movable)
        {
            scene().add<scene::UiDragSource>(tab.entity, scene::UiDragSource{.type = tabDrag, .data = std::to_string(tab.id), .label = name});
            scene().add<scene::UiDropTarget>(tab.entity, scene::UiDropTarget{.accepts = {tabDrag}});
        }
        total += tab.width + 2.0f;
        tabs.push_back(tab);
    }
    tabsWidth = std::max(total - 2.0f, 0.0f);
    scene().get<UiRect>(tabRow).offsetMax.x = std::max(tabsWidth, 1.0f);
}

std::optional<std::size_t> ViewportHeaderUi::tabOf(Entity element)
{
    if (!element.isValid() || !scene().isAlive(element))
    {
        return std::nullopt;
    }
    for (Entity above = element; above.isValid() && above != tabRow; above = scene().parent(above))
    {
        for (std::size_t index = 0; index < tabs.size(); ++index)
        {
            if (tabs[index].entity == above)
            {
                return index;
            }
        }
    }
    return std::nullopt;
}

void ViewportHeaderUi::light(const Button& target, bool on)
{
    scene().get<UiRect>(target.entity).style = barStyle(panel.world(), target.entity, on);
    scene().get<UiRect>(target.icon).style = on ? "icon_accent" : "icon";
}

void ViewportHeaderUi::fit(EditorUiKit& kit, Entity entity, std::string_view value)
{
    scene::UiText& shown = scene().get<scene::UiText>(entity);
    if (shown.text != value)
    {
        shown.text = std::string(value);
        UiRect& rect = scene().get<UiRect>(entity);
        rect.offsetMax.x = rect.offsetMin.x + std::ceil(kit.textWidth(EditorUiKit::regularFont(), value, shown.size)) + 2.0f;
    }
}

void ViewportHeaderUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        font = state.theme.fontSize;
        build(kit);
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    const bool editing = state.playState == PlayState::Editing;
    const ActiveDocument live = activeDocument(state, edited);
    const std::optional<std::size_t> active = state.tabs.active();

    // The tabs are made again when the scenes, their names or what they have to save change.
    std::string key = editing ? "editing" : "playing";
    for (std::size_t index = 0; index < state.tabs.size(); ++index)
    {
        const bool modified = state.tabs.isModified(index, live) && (editing || index != active);
        std::format_to(std::back_inserter(key), "|{}{}{}", state.tabs.id(index), modified ? '*' : ':', core::toUtf8(state.tabs.path(index, live)));
    }
    if (key != tabsKey)
    {
        fillTabs(state, kit, live, editing);
        tabsKey = std::move(key);
    }
    const std::optional<std::size_t> pointed = tabOf(world.hovered());
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
        const Tab& tab = tabs[index];
        const bool shown = index == active;
        scene().get<UiRect>(tab.entity).style = shown ? "tab_selected" : pointed == index ? "tab_hover" : "tab";
        scene().get<UiRect>(tab.label).style = shown ? "text" : "dim";
        scene().get<UiRect>(tab.glyph).style = shown ? "icon_accent" : "icon_dim";
        scene().get<UiRect>(tab.overline).visible = shown;
        if (tab.unsaved.isValid())
        {
            scene().get<scene::UiImage>(tab.unsaved).color = linearColor(shown ? colors.text : colors.textDim);
        }
        if (tab.close.isValid())
        {
            scene().get<UiRect>(tab.close).style = barStyle(world, tab.close, false);
        }
    }
    // The tabs never leave a hole at their end, and the one that comes to the screen comes into view;
    // the button that adds a scene follows the last tab, as far as the edge.
    const float room = panel.size().x - sideMargin() * 2.0f - newTabSize() - 2.0f;
    {
        UiRect& rect = scene().get<UiRect>(newTab);
        rect.visible = editing;
        rect.style = barStyle(world, newTab, false);
        const math::Vec2 at{sideMargin() + std::min(tabsWidth, std::max(room, 0.0f)) + 2.0f,
                            tabsTop() + std::round((tabsHeight() - tabsTop() - newTabSize()) * 0.5f)};
        rect.offsetMin = at;
        rect.offsetMax = at + math::Vec2{newTabSize()};
    }
    if (room > 1.0f)
    {
        scene::UiScroll& scroll = scene().get<scene::UiScroll>(strip);
        const std::optional<std::uint64_t> shownId = active && *active < tabs.size() ? std::optional(tabs[*active].id) : std::nullopt;
        if (shownId != revealed)
        {
            revealed = shownId;
            if (shownId)
            {
                const Tab& tab = tabs[*active];
                scroll.offset.x = std::min(std::max(scroll.offset.x, tab.start + tab.width - room), tab.start);
            }
        }
        scroll.offset.x = std::clamp(scroll.offset.x, 0.0f, std::max(tabsWidth - room, 0.0f));
    }

    // The toolbar, or what the game is doing.
    scene().get<scene::UiImage>(bar).color = linearColor(colors.panel);
    scene().get<UiRect>(tools).visible = editing;
    scene().get<UiRect>(viewSide).visible = editing;
    scene().get<UiRect>(banner).visible = !editing;
    const bool twoD = state.camera.isTwoD();
    const bool local = state.gizmo.space == GizmoSpace::Local;
    const std::array<EditorTool, 4> kinds{EditorTool::Select, EditorTool::Move, EditorTool::Rotate, EditorTool::Scale};
    if (editing)
    {
        for (std::size_t index = 0; index < toolButtons.size(); ++index)
        {
            light(toolButtons[index], state.tool == kinds[index]);
        }
        if (shownLocal != local)
        {
            shownLocal = local;
            scene().get<scene::UiImage>(space.icon).texture = kit.icon(local ? Icon::Box : Icon::Globe);
            tooltip(space.entity, local ? "Handles follow the entity's axes (X). Scaling is always local."
                                        : "Handles follow the world axes (X). Scaling is always local.");
        }
        light(space, local);
        light(snap, state.snap);
        light(grid, state.showGrid);
        light(iconsShown, state.showIcons);
        light(colliders, state.showColliders);
        light(frame, false);
        enable(frame, edited.findEntity(state.selection.active()).isValid());
        scene().get<UiRect>(interfaces.entity).visible = !twoD && edited.kind() == scene::SceneKind::ThreeD;
        light(interfaces, state.showInterfaces);

        if (std::string moves = twoD ? std::format("{:.3g} m", state.camera.orthographicSize() * 2.0f) : std::format("{:.1f} m/s", state.camera.speed());
            scene().get<scene::UiText>(speedText).text != moves)
        {
            // The text fills what its icon leaves: the two are as wide as the icon and what is written.
            UiRect& rect = scene().get<UiRect>(speed);
            rect.offsetMax.x = rect.offsetMin.x + scene().get<UiRect>(speedText).offsetMin.x +
                               std::ceil(kit.textWidth(EditorUiKit::regularFont(), moves, font)) + 2.0f;
            scene().get<scene::UiText>(speedText).text = std::move(moves);
        }
        fit(kit, exposure, std::format("EV {:.1f}", state.renderer.stats().ev100));
        if (shownTwoD != twoD)
        {
            shownTwoD = twoD;
            tooltip(speed, twoD ? "Height of the 2D view: the mouse wheel zooms at the mouse" : "Flying speed: the mouse wheel changes it while flying");
            tooltip(help.entity, twoD ? "Right or middle drag: slide    Wheel: zoom at the mouse\n"
                                        "Click: select, Shift or Ctrl + click: add or remove, left drag: select in a rectangle\n"
                                        "Interfaces: click an element to select it, again to reach the one under it,\n"
                                        "drag it or its handles to move or resize it\n"
                                        "F: frame the selection    H: hide it    Delete: delete it    Ctrl: snap"
                                      : "Right drag: look, with W A S D to fly, Q E to go down and up, Shift to go faster\n"
                                        "Alt + left drag: orbit    Middle drag: pan    Wheel: move forward\n"
                                        "Click: select, Shift or Ctrl + click: add or remove, left drag: select in a rectangle\n"
                                        "F: frame the selection    H: hide it    Delete: delete it    Ctrl: snap");
        }
        scene().get<UiRect>(help.entity).style = barStyle(world, help.entity, false);
    }
    else
    {
        const bool paused = state.playState == PlayState::Paused;
        scene().get<scene::UiImage>(bannerIcon).texture = kit.icon(paused ? Icon::Pause : Icon::Play);
        scene::UiText& word = scene().get<scene::UiText>(bannerText);
        const std::string_view said = paused ? "Paused" : "Playing: click the view to give the game the keyboard";
        if (word.text != said)
        {
            word.text = std::string(said);
        }
    }

    // As tall as the tabs and the toolbar, whatever the window leaves under it for the view.
    const float zoom = UiPanel::zoomFor(font);
    const float pixelsPerPoint = editorScreen().pixelsPerPoint;
    panel.update(kit, delta, zoom, (std::ceil((tabsHeight() + barHeight()) * zoom) + 0.01f) / pixelsPerPoint);

    if (editing)
    {
        for (std::size_t index = 0; index < toolButtons.size(); ++index)
        {
            if (world.wasClicked(toolButtons[index].entity))
            {
                state.tool = kinds[index];
            }
        }
        if (world.wasClicked(space.entity))
        {
            state.gizmo.space = local ? GizmoSpace::World : GizmoSpace::Local;
        }
        const auto turned = [&](const Button& target, bool& value) {
            if (world.wasClicked(target.entity))
            {
                value = !value;
            }
        };
        turned(snap, state.snap);
        turned(grid, state.showGrid);
        turned(iconsShown, state.showIcons);
        turned(colliders, state.showColliders);
        turned(interfaces, state.showInterfaces);
        if (world.wasClicked(frame.entity))
        {
            frameSelection(state, edited);
        }
    }

    // The tabs: a click shows a scene, its cross or the middle button closes it, and a tab dropped on
    // another takes its place.
    std::optional<std::size_t> shown;
    std::optional<std::uint64_t> closed;
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
        if (world.wasClicked(tabs[index].entity) && index != active)
        {
            shown = index;
        }
        if (tabs[index].close.isValid() && world.wasClicked(tabs[index].close))
        {
            closed = tabs[index].id;
        }
    }
    if (editing && panel.hovered() && state.input.clicked(Mouse::Middle))
    {
        if (const std::optional<std::size_t> under = tabOf(world.hovered()))
        {
            closed = tabs[*under].id;
        }
    }
    // The second button opens the menu of the tab under the pointer, where it was pressed.
    if (editing && panel.hovered() && state.input.clicked(Mouse::Right))
    {
        if (const std::optional<std::size_t> under = tabOf(world.hovered()); under && *under < state.tabs.size())
        {
            std::vector<std::uint64_t> ids;
            ids.reserve(tabs.size());
            for (const Tab& tab : tabs)
            {
                ids.push_back(tab.id);
            }
            const std::filesystem::path& file = state.tabs.path(*under, live);
            std::string resource = file.empty() || state.database == nullptr ? std::string{} : state.database->project().resourcePath(file);
            openEditorMenu(state, tabMenu(ids, *under, *under == active, std::move(resource)), state.input.mouse(), tabMenuOwner);
        }
    }
    const bool added = editing && world.wasClicked(newTab);
    std::optional<std::pair<std::size_t, std::size_t>> moved;
    if (const ui::Drop* const drop = world.dropped(); drop != nullptr && editing && drop->type == tabDrag)
    {
        std::uint64_t id = 0;
        const std::from_chars_result read = std::from_chars(drop->data.data(), drop->data.data() + drop->data.size(), id);
        const std::optional<std::size_t> from = read.ec == std::errc{} ? state.tabs.findById(id) : std::nullopt;
        const std::optional<std::size_t> to = tabOf(drop->target);
        if (from && to && *from != *to)
        {
            moved = std::pair{*from, *to};
        }
    }

    if (moved)
    {
        state.tabs.move(moved->first, moved->second);
    }
    else if (added)
    {
        newSceneTab(state, edited);
    }
    else if (closed)
    {
        requestAction(state, edited, {.kind = PendingAction::Kind::CloseTab, .tabs = {*closed}});
    }
    else if (shown && editing)
    {
        activateSceneTab(state, edited, *shown);
    }
}

void drawViewportHeader(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Viewport header");
    EditorUiKit& kit = editorUiKit(state);
    if (!state.viewportHeaderUi)
    {
        state.viewportHeaderUi = std::make_shared<ViewportHeaderUi>();
    }
    // Nothing between the header and the view under it.
    state.viewportHeaderUi->update(state, kit, scene, core::Duration(state.input.delta()));
}

void renderViewportHeader(ToolsState& state, render::RenderWorld& world)
{
    if (state.viewportHeaderUi && state.uiKit)
    {
        state.viewportHeaderUi->panel.render(*state.uiKit, world, linearColor(themeColors().outer));
    }
}

} // namespace devex::tools::detail
