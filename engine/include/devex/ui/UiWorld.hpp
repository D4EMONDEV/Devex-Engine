#pragma once

#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/ui/DrawList.hpp>
#include <devex/ui/Layout.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::scene {
class Scene;
}

namespace devex::render {
struct RenderWorld;
}

namespace devex::ui {

// What drives the interface in one frame. The pointer is in pixels of the image the interface is
// drawn over, from its top left corner; the steps come from the keyboard or from the pad, and are
// sent once per press.
struct UiInput
{
    math::Vec2 pointer{0.0f};
    bool pointerDown = false;
    bool pointerPressed = false;
    bool pointerReleased = false;
    bool pointerMoved = false;
    // -1, 0 or 1: which way the focus moves.
    int moveX = 0;
    int moveY = 0;
    bool submitPressed = false;
    bool cancelPressed = false;
};

// The interfaces of a game that plays: the canvases of its scene, laid out every frame, answering
// the pointer and the pad, and turned into the triangles the renderer draws.
class UiWorld
{
public:
    // One canvas of the scene, as the last update placed it.
    struct CanvasLayout
    {
        scene::Entity entity;
        LayoutResult layout;
        std::int32_t sortOrder = 0;
        bool interactive = true;
    };

    // Once per frame, before the drawing. The window size is in pixels.
    void update(const scene::Scene& scene, math::Vec2 windowSize, const UiInput& input,
                core::Duration delta);

    // Appends the canvases of the last update to the frame, the lowest sort order first.
    void build(const scene::Scene& scene, const DrawContext& context,
               render::RenderWorld& world) const;

    // Whether a button of that action was clicked during the last update. An action names as many
    // buttons as a game needs: any of them answers.
    [[nodiscard]] bool wasClicked(std::string_view action) const;
    [[nodiscard]] bool wasClicked(scene::Entity entity) const;
    [[nodiscard]] bool wasCancelled() const noexcept;

    [[nodiscard]] scene::Entity hovered() const noexcept;
    [[nodiscard]] scene::Entity focused() const noexcept;
    // Moves the focus, as a menu does when it opens. An entity without an interactable button
    // clears it.
    void setFocus(const scene::Scene& scene, scene::Entity entity);
    // Whether the pointer is over an element that answers it, which a game reads before shooting.
    [[nodiscard]] bool pointerOverInterface() const noexcept;

    // The tint the buttons give the image of their entity, white when there is none.
    [[nodiscard]] math::Vec4 tint(scene::Entity entity) const;
    // The canvases of the last update, in the order they are drawn.
    [[nodiscard]] std::span<const CanvasLayout> canvases() const noexcept;

    // Forgets every canvas and every button, as when another scene starts playing.
    void clear();

private:
    struct ButtonState
    {
        scene::Entity entity;
        math::Vec4 tint{1.0f};
    };

    [[nodiscard]] ButtonState& buttonState(scene::Entity entity);
    void updateHover(const scene::Scene& scene, const UiInput& input);
    void updateNavigation(const scene::Scene& scene, const UiInput& input);
    void updateTints(const scene::Scene& scene, core::Duration delta);
    void click(const scene::Scene& scene, scene::Entity entity);

    std::vector<CanvasLayout> m_canvases;
    std::vector<ButtonState> m_buttons;
    std::vector<scene::Entity> m_clicked;
    std::vector<std::string> m_clickedActions;
    scene::Entity m_hovered;
    scene::Entity m_pressed;
    scene::Entity m_focused;
    bool m_pointerOverInterface = false;
    bool m_cancelled = false;
};

} // namespace devex::ui
