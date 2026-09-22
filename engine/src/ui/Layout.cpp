#include <devex/ui/Layout.hpp>

#include <devex/scene/Scene.hpp>

#include <algorithm>
#include <cmath>

namespace devex::ui {
namespace {

// The two axes of a container, so that rows and columns share one placement.
struct Axis
{
    int main = 0;
    int cross = 1;
};

[[nodiscard]] float component(math::Vec2 value, int axis) noexcept
{
    return axis == 0 ? value.x : value.y;
}

void setComponent(math::Vec2& value, int axis, float amount) noexcept
{
    (axis == 0 ? value.x : value.y) = amount;
}

// Whether the element stretches on an axis, which its anchors say: anchors that meet give it its
// own size, anchors apart follow the parent.
[[nodiscard]] bool stretches(const scene::UiRect& rect, int axis) noexcept
{
    return component(rect.anchorMin, axis) != component(rect.anchorMax, axis);
}

// The size the element asks for on an axis when it does not stretch.
[[nodiscard]] float requestedSize(const scene::UiRect& rect, int axis) noexcept
{
    return std::max(component(rect.offsetMax, axis) - component(rect.offsetMin, axis), 0.0f);
}

// Places one corner of an element from its anchors inside the rectangle of its parent.
[[nodiscard]] math::Vec2 anchored(math::Vec2 parentMin, math::Vec2 parentSize, math::Vec2 anchor,
                                  math::Vec2 offset) noexcept
{
    return math::Vec2{parentMin.x + parentSize.x * anchor.x + offset.x,
                      parentMin.y + parentSize.y * anchor.y + offset.y};
}

// The rectangle an element takes from its anchors alone, without a container above it.
void placeByAnchors(const scene::UiRect& rect, math::Vec2 parentMin, math::Vec2 parentSize,
                    LaidOutRect& out)
{
    out.min = anchored(parentMin, parentSize, rect.anchorMin, rect.offsetMin);
    out.max = anchored(parentMin, parentSize, rect.anchorMax, rect.offsetMax);
    out.max.x = std::max(out.max.x, out.min.x);
    out.max.y = std::max(out.max.y, out.min.y);
}

// The children of an entity that take part in a layout, in their order.
[[nodiscard]] std::vector<scene::Entity> laidOutChildren(const scene::Scene& scene,
                                                         scene::Entity parent)
{
    std::vector<scene::Entity> children;
    for (scene::Entity child = scene.firstChild(parent); child.isValid();
         child = scene.nextSibling(child))
    {
        if (scene.tryGet<scene::UiRect>(child) != nullptr)
        {
            children.push_back(child);
        }
    }
    return children;
}

// Places the children of a container along its axis. Children that do not stretch keep their own
// size; the ones that do share what is left over, as does every child of an equalSize container.
void placeInLayout(const scene::Scene& scene, const scene::UiLayout& layout,
                   std::span<const scene::Entity> children, math::Vec2 innerMin,
                   math::Vec2 innerSize, std::vector<math::Vec2>& mins,
                   std::vector<math::Vec2>& maxes)
{
    const Axis axis = layout.kind == scene::UiLayoutKind::Row ? Axis{0, 1} : Axis{1, 0};
    const auto count = static_cast<float>(children.size());
    const float innerMain = component(innerSize, axis.main);
    const float innerCross = component(innerSize, axis.cross);

    if (layout.kind == scene::UiLayoutKind::Grid)
    {
        // Every cell of a grid is the same size, so that icons and slots line up.
        const auto columns = static_cast<float>(std::max<std::uint32_t>(layout.columns, 1));
        const float rows = std::max(std::ceil(count / columns), 1.0f);
        const float cellWidth =
            std::max((innerSize.x - layout.spacing * (columns - 1.0f)) / columns, 0.0f);
        float cellHeight = std::max((innerSize.y - layout.spacing * (rows - 1.0f)) / rows, 0.0f);
        if (!layout.equalSize)
        {
            // Rows as tall as the tallest child asks for, rather than sharing the height.
            cellHeight = 0.0f;
            for (const scene::Entity child : children)
            {
                cellHeight = std::max(cellHeight, requestedSize(scene.get<scene::UiRect>(child), 1));
            }
        }
        for (std::size_t index = 0; index < children.size(); ++index)
        {
            const auto column = static_cast<float>(index % static_cast<std::size_t>(columns));
            const auto row = static_cast<float>(index / static_cast<std::size_t>(columns));
            mins[index] = math::Vec2{innerMin.x + column * (cellWidth + layout.spacing),
                                     innerMin.y + row * (cellHeight + layout.spacing)};
            maxes[index] = math::Vec2{mins[index].x + cellWidth, mins[index].y + cellHeight};
        }
        return;
    }

    float fixed = 0.0f;
    float flexible = 0.0f;
    for (const scene::Entity child : children)
    {
        const scene::UiRect& rect = scene.get<scene::UiRect>(child);
        if (layout.equalSize || stretches(rect, axis.main))
        {
            flexible += 1.0f;
        }
        else
        {
            fixed += requestedSize(rect, axis.main);
        }
    }
    const float spacing = layout.spacing * std::max(count - 1.0f, 0.0f);
    const float share =
        flexible > 0.0f ? std::max(innerMain - spacing - fixed, 0.0f) / flexible : 0.0f;
    const float total = flexible > 0.0f ? innerMain : fixed + spacing;

    // Where the whole run starts inside the container.
    float pen = component(innerMin, axis.main);
    switch (layout.align)
    {
    case scene::TextAlign::Center:
        pen += (innerMain - total) * 0.5f;
        break;
    case scene::TextAlign::Right:
        pen += innerMain - total;
        break;
    case scene::TextAlign::Left:
        break;
    }

    for (std::size_t index = 0; index < children.size(); ++index)
    {
        const scene::UiRect& rect = scene.get<scene::UiRect>(children[index]);
        const float size =
            layout.equalSize || stretches(rect, axis.main) ? share : requestedSize(rect, axis.main);
        setComponent(mins[index], axis.main, pen);
        setComponent(maxes[index], axis.main, pen + size);
        pen += size + layout.spacing;

        // Across the container, the child keeps its anchors: it stretches, or sits where they put
        // it inside the room left by the padding.
        const float crossMin = component(innerMin, axis.cross);
        const float low = crossMin + innerCross * component(rect.anchorMin, axis.cross) +
                          component(rect.offsetMin, axis.cross);
        const float high = stretches(rect, axis.cross)
                               ? crossMin + innerCross * component(rect.anchorMax, axis.cross) +
                                     component(rect.offsetMax, axis.cross)
                               : low + requestedSize(rect, axis.cross);
        setComponent(mins[index], axis.cross, low);
        setComponent(maxes[index], axis.cross, std::max(high, low));
    }
}

// Lays out the children of one element, then their own children, parents first.
void layoutChildren(const scene::Scene& scene, scene::Entity parent, math::Vec2 parentMin,
                    math::Vec2 parentSize, float opacity, bool visible, std::uint16_t depth,
                    LayoutResult& result)
{
    // An interface nested deeper than this is a mistake; the limit also guards the recursion.
    if (depth > 64)
    {
        return;
    }
    const std::vector<scene::Entity> children = laidOutChildren(scene, parent);
    if (children.empty())
    {
        return;
    }

    std::vector<math::Vec2> mins(children.size());
    std::vector<math::Vec2> maxes(children.size());
    std::vector<scene::Entity> placed;
    if (const scene::UiLayout* const layout = scene.tryGet<scene::UiLayout>(parent))
    {
        // A container skips its hidden children, so that a menu closes the gap they leave.
        std::vector<std::size_t> indices;
        for (std::size_t index = 0; index < children.size(); ++index)
        {
            if (scene.get<scene::UiRect>(children[index]).visible)
            {
                placed.push_back(children[index]);
                indices.push_back(index);
            }
        }
        std::vector<math::Vec2> placedMins(placed.size());
        std::vector<math::Vec2> placedMaxes(placed.size());
        const math::Vec2 innerMin{parentMin.x + layout->padding.x, parentMin.y + layout->padding.y};
        const math::Vec2 innerSize{
            std::max(parentSize.x - layout->padding.x - layout->padding.z, 0.0f),
            std::max(parentSize.y - layout->padding.y - layout->padding.w, 0.0f)};
        placeInLayout(scene, *layout, placed, innerMin, innerSize, placedMins, placedMaxes);
        for (std::size_t index = 0; index < placed.size(); ++index)
        {
            mins[indices[index]] = placedMins[index];
            maxes[indices[index]] = placedMaxes[index];
        }
    }

    for (std::size_t index = 0; index < children.size(); ++index)
    {
        const scene::Entity child = children[index];
        const scene::UiRect& rect = scene.get<scene::UiRect>(child);
        LaidOutRect placement{.entity = child,
                              .scale = rect.scale,
                              .rotation = rect.rotation,
                              .opacity = opacity * std::clamp(rect.opacity, 0.0f, 1.0f),
                              .visible = visible && rect.visible,
                              .depth = depth};
        if (std::ranges::find(placed, child) != placed.end())
        {
            placement.min = mins[index];
            placement.max = maxes[index];
        }
        else
        {
            placeByAnchors(rect, parentMin, parentSize, placement);
        }
        placement.pivot =
            math::Vec2{placement.min.x + (placement.max.x - placement.min.x) * rect.pivot.x,
                       placement.min.y + (placement.max.y - placement.min.y) * rect.pivot.y};
        result.rects.push_back(placement);

        layoutChildren(scene, child, placement.min, placement.size(), placement.opacity,
                       placement.visible, static_cast<std::uint16_t>(depth + 1), result);
    }
}

} // namespace

const LaidOutRect* LayoutResult::find(scene::Entity entity) const noexcept
{
    const auto found = std::ranges::find(rects, entity, &LaidOutRect::entity);
    return found != rects.end() ? &*found : nullptr;
}

float canvasScale(const scene::Canvas& canvas, math::Vec2 windowSize) noexcept
{
    if (canvas.scaleMode == scene::CanvasScaleMode::ConstantPixels)
    {
        return 1.0f;
    }
    const float width = std::max(canvas.referenceResolution.x, 1.0f);
    const float height = std::max(canvas.referenceResolution.y, 1.0f);
    // Mixed in logarithms, so that a match of one half halves the difference evenly whichever way
    // the window is stretched.
    const float byWidth = std::log2(std::max(windowSize.x, 1.0f) / width);
    const float byHeight = std::log2(std::max(windowSize.y, 1.0f) / height);
    const float match = std::clamp(canvas.matchWidthOrHeight, 0.0f, 1.0f);
    return std::exp2(byWidth + (byHeight - byWidth) * match);
}

std::array<math::Vec2, 4> corners(const LaidOutRect& rect) noexcept
{
    const std::array<math::Vec2, 4> plain{rect.min, math::Vec2{rect.max.x, rect.min.y}, rect.max,
                                          math::Vec2{rect.min.x, rect.max.y}};
    const float cosine = std::cos(rect.rotation);
    const float sine = std::sin(rect.rotation);
    std::array<math::Vec2, 4> turned{};
    for (std::size_t index = 0; index < plain.size(); ++index)
    {
        const math::Vec2 local{(plain[index].x - rect.pivot.x) * rect.scale.x,
                               (plain[index].y - rect.pivot.y) * rect.scale.y};
        turned[index] = math::Vec2{rect.pivot.x + local.x * cosine - local.y * sine,
                                   rect.pivot.y + local.x * sine + local.y * cosine};
    }
    return turned;
}

bool contains(const LaidOutRect& rect, math::Vec2 point) noexcept
{
    // The point is brought back into the rectangle rather than the rectangle turned.
    const float cosine = std::cos(-rect.rotation);
    const float sine = std::sin(-rect.rotation);
    const math::Vec2 offset{point.x - rect.pivot.x, point.y - rect.pivot.y};
    const math::Vec2 local{offset.x * cosine - offset.y * sine, offset.x * sine + offset.y * cosine};
    const float scaleX = rect.scale.x != 0.0f ? rect.scale.x : 1.0f;
    const float scaleY = rect.scale.y != 0.0f ? rect.scale.y : 1.0f;
    const math::Vec2 place{rect.pivot.x + local.x / scaleX, rect.pivot.y + local.y / scaleY};
    return place.x >= rect.min.x && place.x <= rect.max.x && place.y >= rect.min.y &&
           place.y <= rect.max.y;
}

void layoutCanvas(const scene::Scene& scene, scene::Entity canvas, math::Vec2 windowSize,
                  LayoutResult& result)
{
    result.rects.clear();
    const scene::Canvas* const component = scene.tryGet<scene::Canvas>(canvas);
    if (component == nullptr)
    {
        result.canvasSize = windowSize;
        result.scale = 1.0f;
        return;
    }
    result.scale = std::max(canvasScale(*component, windowSize), 0.0001f);
    result.canvasSize = math::Vec2{windowSize.x / result.scale, windowSize.y / result.scale};
    layoutChildren(scene, canvas, math::Vec2{0.0f, 0.0f}, result.canvasSize, 1.0f,
                   component->visible, 0, result);
}

} // namespace devex::ui
