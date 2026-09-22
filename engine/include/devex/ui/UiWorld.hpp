#pragma once

#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/ui/DrawList.hpp>
#include <devex/ui/Layout.hpp>
#include <devex/ui/TextLayout.hpp>
#include <devex/ui/Theme.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
    // Notches of the wheel this frame; positive scrolls the content up.
    float wheel = 0.0f;
    // The characters typed this frame, and the keys that edit a field.
    std::string typed;
    bool backspacePressed = false;
    bool deletePressed = false;
    bool leftPressed = false;
    bool rightPressed = false;
    bool upPressed = false;
    bool downPressed = false;
    bool homePressed = false;
    bool endPressed = false;
    bool selecting = false;
    bool copyPressed = false;
    bool cutPressed = false;
    bool pastePressed = false;
    bool selectAllPressed = false;
    // What the clipboard holds, and what the interface asks to put in it.
    std::string clipboard;
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

    // Where the fields find their font, so that a click lands between the right letters. Without
    // it a field still takes what is typed, but only at the end of its text.
    void setFonts(std::function<FontRef(asset::AssetId)> fonts, asset::AssetId defaultFont = {});
    // Where the canvases find the theme they name. Without it the elements keep their own look.
    void setThemes(ThemeSource themes);

    // Once per frame, before the drawing. The window size is in pixels. The scene is written to:
    // a list that scrolls and a field that is typed into keep their state in their components.
    void update(scene::Scene& scene, math::Vec2 windowSize, const UiInput& input,
                core::Duration delta);
    // What the interface asks to put in the clipboard this frame, empty when it asks nothing.
    [[nodiscard]] const std::string& clipboardRequest() const noexcept;

    // The field being edited, if any: the runtime turns the typing of the system on while there
    // is one, so that the keyboard of a phone opens and a dead key composes.
    [[nodiscard]] scene::Entity editedField() const noexcept;
    [[nodiscard]] bool isEditing() const noexcept;
    // Whether a field ended its edit with Enter during the last update.
    [[nodiscard]] bool wasSubmitted(std::string_view action) const;
    [[nodiscard]] bool wasSubmitted(scene::Entity entity) const;
    // Where the cursor and the selection of a field stand, in bytes of the text it draws; nothing
    // for a field that is not being edited.
    [[nodiscard]] const EditState* editStateOf(scene::Entity entity) const noexcept;

    // Appends the canvases of the last update to the frame, the lowest sort order first.
    void build(const scene::Scene& scene, const DrawContext& context,
               render::RenderWorld& world) const;

    // Whether a button of that action was clicked during the last update. An action names as many
    // buttons as a game needs: any of them answers.
    [[nodiscard]] bool wasClicked(std::string_view action) const;
    [[nodiscard]] bool wasClicked(scene::Entity entity) const;
    // Whether a slider was moved or a toggle turned over during the last update.
    [[nodiscard]] bool wasChanged(std::string_view action) const;
    [[nodiscard]] bool wasChanged(scene::Entity entity) const;
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

    // The field being edited: where its cursor stands, in bytes of its own text.
    struct EditingField
    {
        scene::Entity entity;
        std::size_t caret = 0;
        // Where the selection started; equal to the cursor when nothing is selected.
        std::size_t anchor = 0;
        // Seconds since the cursor last moved, which makes it blink.
        float blink = 0.0f;
        bool dragging = false;
    };

    [[nodiscard]] ButtonState& buttonState(scene::Entity entity);
    // Writes what the bindings read into the texts they drive, before anything is placed.
    void updateBindings(scene::Scene& scene);
    void updateHover(const scene::Scene& scene, const UiInput& input);
    void updateScroll(scene::Scene& scene, const UiInput& input);
    void updateNavigation(const scene::Scene& scene, const UiInput& input);
    void updateFields(scene::Scene& scene, const UiInput& input, core::Duration delta);
    // Drags the slider under the pointer and moves the one that has the focus. Answers whether
    // the arrows went into a slider rather than into moving the focus.
    [[nodiscard]] bool updateSliders(scene::Scene& scene, const UiInput& input);
    void changeSlider(scene::Entity entity, scene::UiSlider& slider, float value);
    void editField(scene::Scene& scene, const UiInput& input);
    void updateTints(const scene::Scene& scene, core::Duration delta);
    void click(scene::Scene& scene, scene::Entity entity);
    void submit(const scene::Scene& scene, scene::Entity entity);
    // Lays the text of a field out where it was placed, into m_fieldLayout.
    [[nodiscard]] bool layoutField(const scene::UiText& text, const scene::UiInput& field,
                                   const LaidOutRect& rect);
    // The canvas an entity was laid out in, which also says how its units turn into pixels.
    [[nodiscard]] const CanvasLayout* canvasOf(scene::Entity entity) const noexcept;
    // The place in the text of a field a point falls on, in bytes of the text of the field.
    [[nodiscard]] std::size_t caretFromPoint(const scene::Scene& scene, scene::Entity entity,
                                             const LaidOutRect& rect, math::Vec2 point);

    std::vector<CanvasLayout> m_canvases;
    std::vector<ButtonState> m_buttons;
    std::vector<scene::Entity> m_clicked;
    std::vector<std::string> m_clickedActions;
    std::vector<scene::Entity> m_changed;
    std::vector<std::string> m_changedActions;
    std::vector<scene::Entity> m_submitted;
    std::vector<std::string> m_submittedActions;
    scene::Entity m_hovered;
    scene::Entity m_pressed;
    scene::Entity m_dragged;
    scene::Entity m_focused;
    bool m_pointerOverInterface = false;
    bool m_cancelled = false;
    std::string m_clipboardRequest;
    EditingField m_editing;
    // The same cursor, in the bytes of the text the field draws, which the drawing reads.
    EditState m_editVisual;
    std::function<FontRef(asset::AssetId)> m_fonts;
    ThemeApplier m_theme;
    asset::AssetId m_defaultFont;
    // Kept from one frame to the next, so that a field that is typed into allocates nothing.
    TextLayoutResult m_fieldLayout;
};

} // namespace devex::ui
