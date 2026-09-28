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
    // The second button, which opens context menus.
    bool secondaryPressed = false;
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

// How the tooltips look: the interface world draws them over every canvas, in pixels.
struct TooltipStyle
{
    math::Vec4 background{0.08f, 0.09f, 0.11f, 0.96f};
    math::Vec4 text{0.92f, 0.93f, 0.95f, 1.0f};
    // The font; without one, the default font of the drawing, else the font of the texts around
    // the element, since a game has no font of its own.
    asset::AssetId font;
    float size = 15.0f;
    float padding = 6.0f;
    float cornerRadius = 4.0f;
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

    // Appends the canvases of the last update to the frame, the lowest sort order first, then what
    // stands over all of them: the list of an open dropdown, and the tooltip.
    void build(const scene::Scene& scene, const DrawContext& context,
               render::RenderWorld& world) const;

    // Opens a popup where its anchors put it, or with its top left corner at a point of the image,
    // in pixels. Menus close by themselves; a modal stays until it is closed.
    void openPopup(scene::Scene& scene, scene::Entity popup, std::optional<math::Vec2> at = std::nullopt);
    void closePopup(scene::Scene& scene, scene::Entity popup);
    [[nodiscard]] bool isPopupOpen(const scene::Scene& scene, scene::Entity popup) const;
    // The element whose UiContextMenu opened the last context menu, such as the row of a list.
    [[nodiscard]] scene::Entity contextTarget() const noexcept;
    // Whether a button was clicked twice in a row, quickly, during this frame; the second click
    // also counts as a click.
    [[nodiscard]] bool wasDoubleClicked(std::string_view action) const;
    [[nodiscard]] bool wasDoubleClicked(scene::Entity entity) const;
    void setTooltipStyle(TooltipStyle style);
    // Gives a field the keyboard, its text selected, as a form does for its first field.
    void startEditing(const scene::Scene& scene, scene::Entity field);

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
    // The modal every input goes to while it is open: its canvas and its elements there.
    struct ModalRange
    {
        std::size_t canvas = 0;
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    // An element the pointer drags: the thumb of a scrollbar, the bar of a splitter, the edge of
    // a column. The grab is where the pointer took it, in units of its canvas.
    enum class DragKind : std::uint8_t
    {
        None,
        ScrollVertical,
        ScrollHorizontal,
        Splitter,
        Column,
    };
    struct Drag
    {
        DragKind kind = DragKind::None;
        scene::Entity entity;
        float grab = 0.0f;
        std::size_t column = 0;
    };

    // The dropdown whose list is open, the option under the pointer, and the first one shown.
    struct OpenDropdown
    {
        scene::Entity entity;
        std::int32_t highlighted = -1;
        std::size_t first = 0;
    };

    struct Tooltip
    {
        scene::Entity entity;
        float rested = 0.0f;
        bool shown = false;
        math::Vec2 at{0.0f};
    };
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

    // Popups, context menus, modals, dropdowns, tooltips, scrollbars, splitters, foldouts, tables
    // and virtual lists, in Controls.cpp.
    void layoutCanvases(scene::Scene& scene);
    void findModal(const scene::Scene& scene);
    [[nodiscard]] bool reachable(std::size_t canvas, std::size_t rect) const noexcept;
    // Closes the menus the pointer pressed outside of; answers whether the press was taken.
    [[nodiscard]] bool closeMenusOutside(scene::Scene& scene, const UiInput& input);
    void closeMenusHolding(scene::Scene& scene, scene::Entity entity);
    [[nodiscard]] bool closeTopMenu(scene::Scene& scene);
    void openContextMenu(scene::Scene& scene, const UiInput& input);
    [[nodiscard]] bool updateDropdown(scene::Scene& scene, const UiInput& input);
    void openDropdown(const scene::Scene& scene, scene::Entity entity);
    // The rectangles of the open dropdown list: the whole list and one option, in pixels.
    [[nodiscard]] bool dropdownList(const scene::Scene& scene, math::Vec2& min, math::Vec2& max, float& item,
                                    std::size_t& shown) const;
    [[nodiscard]] bool startDrag(scene::Scene& scene, const UiInput& input);
    void updateDrag(scene::Scene& scene, const UiInput& input);
    void updateControls(scene::Scene& scene, const UiInput& input);
    void updateTooltip(const scene::Scene& scene, const UiInput& input, float seconds);
    void noteClick(const scene::Scene& scene, scene::Entity entity);
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> hit(const scene::Scene& scene, math::Vec2 pointer,
                                                                         bool anyElement) const;
    [[nodiscard]] math::Vec2 toCanvas(std::size_t canvas, math::Vec2 pointer) const noexcept;
    [[nodiscard]] scene::Entity canvasEntityOf(const scene::Scene& scene, scene::Entity entity) const;

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

    math::Vec2 m_windowSize{0.0f};
    // Seconds since the world started, which tells a double click from two clicks.
    double m_clock = 0.0;
    std::vector<PopupPlacement> m_popupPlacements;
    std::optional<ModalRange> m_modal;
    scene::Entity m_contextTarget;
    // The last click, for the next one to be a double click.
    scene::Entity m_lastClick;
    double m_lastClickTime = -1.0;
    std::vector<scene::Entity> m_doubleClicked;
    std::vector<std::string> m_doubleClickedActions;
    Drag m_drag;
    // The splitter whose bar the pointer rests on, which lights up.
    scene::Entity m_barHover;
    // The header cell pressed, which sorts its table if the pointer is let go on it.
    scene::Entity m_headerPressed;
    std::size_t m_headerColumn = 0;
    std::optional<OpenDropdown> m_dropdown;
    Tooltip m_tooltip;
    TooltipStyle m_tooltipStyle;
};

} // namespace devex::ui
