#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/UiComponents.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace devex::scene {
class Scene;
}

// Where the elements of an interface land on the screen. The solver is pure: it reads the scene
// and returns rectangles, which the drawing, the editor and the tests all share.
namespace devex::ui {

// One element, placed. Positions are in the units of the canvas, X to the right and Y downwards,
// with the origin at the top left corner of the window.
struct DEVEX_API LaidOutRect
{
    scene::Entity entity;
    // The rectangle before rotation and scaling.
    math::Vec2 min{0.0f};
    math::Vec2 max{0.0f};
    // The point it turns and grows around, in the same units.
    math::Vec2 pivot{0.0f};
    math::Vec2 scale{1.0f};
    float rotation = 0.0f;
    // The opacity of the element multiplied by the ones above it.
    float opacity = 1.0f;
    // False when the element, or one above it, is hidden.
    bool visible = true;
    // How deep it sits under the canvas, and where it comes in the order of its siblings: the
    // elements are laid out parents first, so a later one is drawn over an earlier one.
    std::uint16_t depth = 0;
    // What the element is cut to, from the ones above it: left, top, right and bottom, in units.
    // An empty rectangle means nothing cuts it.
    math::Vec4 clip{0.0f};
    // The room its children take, for an element that scrolls: what lies beyond its own size is
    // what the offset can reach.
    math::Vec2 content{0.0f};
    // How many elements after it lie under it: its subtree is itself and those.
    std::uint32_t descendants = 0;

    [[nodiscard]] math::Vec2 size() const noexcept
    {
        return max - min;
    }
};

// The elements of one canvas, parents before their children, in the order they are drawn.
struct DEVEX_API LayoutResult
{
    std::vector<LaidOutRect> rects;
    // The size of the canvas in its own units, and how many pixels one unit takes.
    math::Vec2 canvasSize{0.0f};
    float scale = 1.0f;

    [[nodiscard]] const LaidOutRect* find(scene::Entity entity) const noexcept;
};

// How many pixels one unit of the canvas takes on a window of that size, in pixels.
[[nodiscard]] DEVEX_API float canvasScale(const scene::Canvas& canvas, math::Vec2 windowSize) noexcept;

// Places every UiRect under the canvas entity. The canvas itself fills the window.
// A popup opened at a point, which it takes as its top left corner, in units of its canvas.
struct DEVEX_API PopupPlacement
{
    scene::Entity popup;
    math::Vec2 point{0.0f};
    // Where it may stand, in units of its canvas; an empty area keeps it inside the canvas.
    math::Vec2 areaMin{0.0f};
    math::Vec2 areaMax{0.0f};
};

// Open popups are laid out after the rest of their canvas, so that they are drawn over it and
// answer the pointer first, and nothing above them cuts or scrolls them. Those opened at a point
// stand there, inside the canvas or the area of their placement.
DEVEX_API void layoutCanvas(const scene::Scene& scene, scene::Entity canvas, math::Vec2 windowSize,
                            LayoutResult& result, std::span<const PopupPlacement> popups = {});

// The index after the last descendant of the element at `index`: its subtree is [index, end).
[[nodiscard]] DEVEX_API std::size_t subtreeEnd(std::span<const LaidOutRect> rects, std::size_t index) noexcept;

// The bar of a splitter laid out in `rect`, between its two children: its corners, in units.
[[nodiscard]] DEVEX_API std::pair<math::Vec2, math::Vec2> splitterBar(const LaidOutRect& rect,
                                                                      const scene::UiSplitter& splitter) noexcept;

// The parts of a colour picker laid out in `rect`, in units: the square, the bar of hue at its
// right, and the bar of opacity under them, empty when the picker has none.
struct DEVEX_API ColorPickerParts
{
    math::Vec2 squareMin{0.0f};
    math::Vec2 squareMax{0.0f};
    math::Vec2 hueMin{0.0f};
    math::Vec2 hueMax{0.0f};
    math::Vec2 alphaMin{0.0f};
    math::Vec2 alphaMax{0.0f};
};
[[nodiscard]] DEVEX_API ColorPickerParts colorPickerParts(const LaidOutRect& rect, const scene::UiColorPicker& picker) noexcept;

// The value of a plot laid out in `rect` a point is over: the bar under it, or the point of the line
// nearest across; -1 outside the plot or when it has no value.
[[nodiscard]] DEVEX_API std::int32_t plotValueAt(const LaidOutRect& rect, const scene::UiPlot& plot, math::Vec2 point) noexcept;

// The item the first child of a virtual list shows: the first one in view.
[[nodiscard]] DEVEX_API std::uint32_t virtualFirst(const LaidOutRect& rect, const scene::UiVirtualList& list) noexcept;

// Whether the rectangle cuts anything, and what is left of a box once it is cut.
[[nodiscard]] DEVEX_API bool isClipped(const math::Vec4& clip) noexcept;
[[nodiscard]] DEVEX_API math::Vec4 intersectClip(const math::Vec4& clip, const math::Vec4& other) noexcept;

// The four corners of a placed element, turned and scaled around its pivot, starting at the top
// left corner and going clockwise.
[[nodiscard]] DEVEX_API std::array<math::Vec2, 4> corners(const LaidOutRect& rect) noexcept;
// Whether a point of the canvas lies inside the element, rotation included.
[[nodiscard]] DEVEX_API bool contains(const LaidOutRect& rect, math::Vec2 point) noexcept;

} // namespace devex::ui
