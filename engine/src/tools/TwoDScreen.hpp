#pragma once

#include <devex/core/Export.hpp>

#include "EditorCamera.hpp"
#include "EditorView.hpp"

#include <devex/math/Math.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <optional>

namespace devex::scene {
class Scene;
}

// The 2D screen of the editor, as in Godot: 2D scenes seen from the front, and the interfaces of
// every scene.
namespace devex::tools::detail {

// What the viewport shows of the edited scene. As Godot keeps 2D nodes out of its 3D view and 3D
// nodes out of its 2D view, a scene shows in the screen of its kind; the other screen shows nothing
// of it, except the 2D screen of a 3D scene, where its interfaces are edited.
enum class ScreenContent : std::uint8_t
{
    Scene,
    Interfaces,
    Nothing,
};

[[nodiscard]] DEVEX_API ScreenContent screenContent(bool twoD, const scene::Scene& scene) noexcept;

// The rectangle of the XY plane the game shows, in meters.
struct DEVEX_API GameFrame
{
    math::Vec2 min{0.0f};
    math::Vec2 max{0.0f};
    // Whether it is the view of the primary camera, which draws its own box.
    bool camera = false;
};

// The view of the primary camera of a 2D scene when it is orthographic, or else the view a new 2D
// camera would have from the origin, where the interfaces of a 3D scene are edited. The aspect is
// the width of the image over its height.
[[nodiscard]] DEVEX_API GameFrame gameFrame(const scene::Scene& scene, float aspect);

// Where the 2D screen draws the interfaces: laid out on the image of the view, as the game lays them
// out in the viewport, then shrunk into the frame of the game. Nothing when the frame is off the view.
[[nodiscard]] DEVEX_API std::optional<InterfaceFrame> interfaceFrame(const GameFrame& frame, const ViewportView& view);

// The screen of the kind of a scene, and its 2D view on what the game shows.
DEVEX_API void fitToScene(EditorCamera& camera, const scene::Scene& scene);

} // namespace devex::tools::detail
