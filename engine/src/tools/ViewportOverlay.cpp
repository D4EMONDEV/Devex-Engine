// What the editor draws over the image of the viewport, made with the interface of the engine: the
// rectangle a drag selects in, the frame of the game while it plays or while interfaces are edited,
// the outline, the handles and the anchors of the element of an interface that is selected, and a
// word on what the screen shows. It takes no input: the viewport under it answers the mouse.
#include "EditorFrame.hpp"
#include "SettingsUi.hpp"

#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;

namespace {

constexpr std::uint32_t viewportOverlaySurface = 21;

} // namespace

struct ViewportOverlayUi : PanelBuilder
{
    ViewportOverlayUi()
        : PanelBuilder(viewportOverlaySurface)
    {
    }

    bool built = false;
    Entity gameFrame;
    Entity playFrame;
    Entity selectingFill;
    Entity selectingLine;
    Entity element;
    std::vector<Entity> handles;
    std::vector<Entity> anchors;
    Entity hintBox;
    Entity hintText;

    // A line of the overlay, over its whole image: its points are pixels of the viewport.
    Entity line(float width)
    {
        const Entity made = add({}, "Line", whole());
        scene().add<scene::UiLine>(made, scene::UiLine{.width = width, .closed = true});
        return made;
    }
    void build();
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

void ViewportOverlayUi::build()
{
    built = true;
    panel.setKeyboardNavigation(false);
    gameFrame = line(1.5f);
    playFrame = line(2.0f);
    selectingFill = add({}, "Selecting", fixed({1.0f, 1.0f}));
    scene().add<scene::UiImage>(selectingFill, scene::UiImage{.raycastTarget = false});
    selectingLine = line(1.0f);
    element = line(1.5f);
    hintBox = add({}, "Hint", fixed({1.0f, 1.0f}));
    scene().add<scene::UiImage>(hintBox, scene::UiImage{.raycastTarget = false});
    hintText = text(hintBox, whole(), "", "text", false, scene::TextAlign::Center);
}

void ViewportOverlayUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built)
    {
        build();
    }
    const ViewportMarks& marks = state.viewportMarks;
    const float ppp = state.pixelsPerPoint > 0.0f ? state.pixelsPerPoint : 1.0f;
    // The marks are in points of the screen; the overlay counts in pixels of the viewport.
    const auto pixel = [&](math::Vec2 point) { return math::Vec2{(point.x - state.viewportOrigin.x) * ppp, (point.y - state.viewportOrigin.y) * ppp}; };
    // A rectangle as a line around it; `inside` keeps the whole line within it, as the frame of the
    // view, whose edges would cut half of it.
    const auto box = [&](Entity entity, const std::optional<std::pair<math::Vec2, math::Vec2>>& corners, math::Vec4 color, float width, bool inside = false) {
        UiRect& rect = scene().get<UiRect>(entity);
        rect.visible = corners.has_value();
        if (!corners)
        {
            return;
        }
        const float inset = inside ? width * ppp * 0.5f : 0.0f;
        const math::Vec2 min = pixel(corners->first) + math::Vec2{inset};
        const math::Vec2 max = pixel(corners->second) - math::Vec2{inset};
        scene::UiLine& drawn = scene().get<scene::UiLine>(entity);
        drawn.points.assign({min, math::Vec2{max.x, min.y}, max, math::Vec2{min.x, max.y}});
        drawn.color = linearColor(color);
        drawn.width = width * ppp;
    };
    const math::Vec4 accent = colors.accent;
    box(gameFrame, marks.gameFrame, math::Vec4(accent.x, accent.y, accent.z, 0.6f), 1.5f);
    // The game is framed in the accent colour while it runs.
    const math::Vec2 view{static_cast<float>(state.viewportPixels.width), static_cast<float>(state.viewportPixels.height)};
    box(playFrame, marks.playing ? std::optional(std::pair{math::Vec2(state.viewportOrigin.x, state.viewportOrigin.y),
                                                           math::Vec2(state.viewportOrigin.x + view.x / ppp, state.viewportOrigin.y + view.y / ppp)})
                                 : std::nullopt,
        accent, 2.0f, true);
    box(selectingLine, marks.selecting, accent, 1.0f);
    {
        UiRect& fill = scene().get<UiRect>(selectingFill);
        fill.visible = marks.selecting.has_value();
        if (marks.selecting)
        {
            fill.offsetMin = pixel(marks.selecting->first);
            fill.offsetMax = pixel(marks.selecting->second);
            scene().get<scene::UiImage>(selectingFill).color = linearColor(math::Vec4(accent.x, accent.y, accent.z, 0.15f));
        }
    }
    box(element, marks.element, accent, 1.5f);

    // The handles, squares a little rounded, and the anchors, rings: each taken from a reserve.
    const float handle = marks.handleRadius * ppp;
    for (std::size_t index = 0; index < marks.handles.size(); ++index)
    {
        if (index == handles.size())
        {
            const Entity made = add({}, "Handle", fixed({1.0f, 1.0f}));
            scene().add<scene::UiImage>(made, scene::UiImage{.raycastTarget = false});
            handles.push_back(made);
        }
        UiRect& rect = scene().get<UiRect>(handles[index]);
        rect.visible = true;
        const math::Vec2 at = pixel(marks.handles[index]);
        rect.offsetMin = at - math::Vec2{handle};
        rect.offsetMax = at + math::Vec2{handle};
        scene::UiImage& image = scene().get<scene::UiImage>(handles[index]);
        image.color = linearColor(accent);
        image.cornerRadius = 2.0f * ppp;
    }
    for (std::size_t index = marks.handles.size(); index < handles.size(); ++index)
    {
        scene().get<UiRect>(handles[index]).visible = false;
    }
    for (std::size_t index = 0; index < marks.anchors.size(); ++index)
    {
        if (index == anchors.size())
        {
            anchors.push_back(line(1.5f));
        }
        scene().get<UiRect>(anchors[index]).visible = true;
        scene::UiLine& ring = scene().get<scene::UiLine>(anchors[index]);
        const math::Vec2 at = pixel(marks.anchors[index]);
        ring.points.resize(16);
        for (std::size_t point = 0; point < ring.points.size(); ++point)
        {
            const float turn = static_cast<float>(point) / static_cast<float>(ring.points.size()) * 2.0f * std::numbers::pi_v<float>;
            ring.points[point] = at + math::Vec2{std::cos(turn), std::sin(turn)} * (4.0f * ppp);
        }
        ring.color = linearColor(colors.textDim);
        ring.width = 1.5f * ppp;
    }
    for (std::size_t index = marks.anchors.size(); index < anchors.size(); ++index)
    {
        scene().get<UiRect>(anchors[index]).visible = false;
    }

    // A word on what the screen shows, at the bottom of it, on the colour of the panels.
    UiRect& hint = scene().get<UiRect>(hintBox);
    hint.visible = !marks.hint.empty();
    if (hint.visible)
    {
        const float size = state.theme.fontSize * UiPanel::zoomFor(state.theme.fontSize);
        scene::UiText& said = scene().get<scene::UiText>(hintText);
        if (said.text != marks.hint)
        {
            said.text = marks.hint;
        }
        said.size = size;
        const float width = kit.textWidth(EditorUiKit::regularFont(), marks.hint, size) + size * 1.4f;
        const float height = size * 2.0f;
        hint.offsetMin = math::Vec2{(view.x - width) * 0.5f, view.y - height * 2.0f};
        hint.offsetMax = hint.offsetMin + math::Vec2{width, height};
        scene::UiImage& image = scene().get<scene::UiImage>(hintBox);
        image.color = linearColor(math::Vec4(colors.panel.x, colors.panel.y, colors.panel.z, 0.85f));
        image.cornerRadius = 4.0f * ppp;
    }

    // Over the image of the viewport, one unit to a pixel.
    state.hosts.setCursor(math::Vec2(state.viewportOrigin.x, state.viewportOrigin.y));
    panel.update(kit, delta, 1.0f, view.y / ppp);
}

void drawViewportOverlay(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Viewport overlay");
    const ViewportMarks& marks = state.viewportMarks;
    const bool anything = marks.playing || marks.selecting || marks.gameFrame || marks.element || !marks.handles.empty() ||
                          !marks.anchors.empty() || !marks.hint.empty();
    if (!anything || state.viewportPixels.width == 0)
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.viewportOverlayUi)
    {
        state.viewportOverlayUi = std::make_shared<ViewportOverlayUi>();
    }
    state.viewportOverlayUi->update(state, kit, core::Duration(state.input.delta()));
}

void renderViewportOverlay(ToolsState& state, render::RenderWorld& world)
{
    if (state.viewportOverlayUi && state.uiKit)
    {
        // Clear around the marks, which the view shows through.
        state.viewportOverlayUi->panel.render(*state.uiKit, world, math::Vec4{0.0f});
    }
}

} // namespace devex::tools::detail
