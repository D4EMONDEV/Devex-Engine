#pragma once

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
struct LaidOutRect
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

    [[nodiscard]] math::Vec2 size() const noexcept
    {
        return max - min;
    }
};

// The elements of one canvas, parents before their children, in the order they are drawn.
struct LayoutResult
{
    std::vector<LaidOutRect> rects;
    // The size of the canvas in its own units, and how many pixels one unit takes.
    math::Vec2 canvasSize{0.0f};
    float scale = 1.0f;

    [[nodiscard]] const LaidOutRect* find(scene::Entity entity) const noexcept;
};

// How many pixels one unit of the canvas takes on a window of that size, in pixels.
[[nodiscard]] float canvasScale(const scene::Canvas& canvas, math::Vec2 windowSize) noexcept;

// Places every UiRect under the canvas entity. The canvas itself fills the window.
void layoutCanvas(const scene::Scene& scene, scene::Entity canvas, math::Vec2 windowSize,
                  LayoutResult& result);

// Whether the rectangle cuts anything, and what is left of a box once it is cut.
[[nodiscard]] bool isClipped(const math::Vec4& clip) noexcept;
[[nodiscard]] math::Vec4 intersectClip(const math::Vec4& clip, const math::Vec4& other) noexcept;

// The four corners of a placed element, turned and scaled around its pivot, starting at the top
// left corner and going clockwise.
[[nodiscard]] std::array<math::Vec2, 4> corners(const LaidOutRect& rect) noexcept;
// Whether a point of the canvas lies inside the element, rotation included.
[[nodiscard]] bool contains(const LaidOutRect& rect, math::Vec2 point) noexcept;

} // namespace devex::ui
