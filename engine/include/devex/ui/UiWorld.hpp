#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/ui/DrawList.hpp>
#include <devex/ui/Layout.hpp>
#include <devex/ui/TextArea.hpp>
#include <devex/ui/TextCache.hpp>
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
struct UiNumberField;
}

namespace devex::render {
struct RenderWorld;
}


namespace devex::ui {

// What drives the interface in one frame. The pointer is in pixels of the image the interface is
// drawn over, from its top left corner; the steps come from the keyboard or from the pad, and are
// sent once per press.
struct DEVEX_API UiInput
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
    // What an area of text answers beside these: the pages, the Tab key, undo and redo, and the
    // modifier that makes the arrows, Backspace and Delete go by words and Home and End to the ends
    // of the text.
    bool pageUpPressed = false;
    bool pageDownPressed = false;
    bool tabPressed = false;
    bool undoPressed = false;
    bool redoPressed = false;
    bool wordModifier = false;
};

// How the tooltips look: the interface world draws them over every canvas, in pixels.
struct DEVEX_API TooltipStyle
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

// What the pointer carries to the drop targets: a UiDragSource taken away, or what a drag that
// started outside the interface announced.
struct DEVEX_API Carried
{
    // Invalid when the drag started outside the interface.
    scene::Entity source;
    std::string type;
    std::string data;
    // Shown next to the pointer; nothing for a drag from outside, which draws its own.
    std::string label;
};

// What a drop target took.
struct DEVEX_API Drop
{
    // Invalid when the drag started outside the interface.
    scene::Entity source;
    scene::Entity target;
    std::string type;
    std::string data;
    // Where it was let go in the target, from its top left corner (0, 0) to its bottom right (1, 1):
    // a list tells a drop before a row from one after it.
    math::Vec2 at{0.5f};
};

// The interfaces of a game that plays: the canvases of its scene, laid out every frame, answering
// the pointer and the pad, and turned into the triangles the renderer draws.
class DEVEX_API UiWorld
{
public:
    // One canvas of the scene, as the last update placed it.
    struct DEVEX_API CanvasLayout
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
    // stands over all of them: the list of an open dropdown, and the tooltip. With `over`, the open
    // menus and the list of a dropdown go there instead, to be shown over what surrounds the image.
    void build(const scene::Scene& scene, const DrawContext& context,
               render::RenderWorld& world, render::RenderWorld* over = nullptr) const;

    // Where the menus opened at a point and the lists of dropdowns may stand, in pixels of the image:
    // by default the image itself. The tools give the whole window around a panel, so that a menu or
    // a list goes past the edge of its panel.
    void setPopupArea(math::Vec2 min, math::Vec2 max) noexcept;
    // What the open menus and the list of a dropdown cover, in pixels of the image, as `build` draws
    // them into `over`; nothing while none is open.
    [[nodiscard]] std::optional<std::pair<math::Vec2, math::Vec2>> overBounds(const scene::Scene& scene) const;

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
    // Whether the drawing shows the tooltips, inside the image. A tool whose image is too small for
    // them turns it off and shows `shownTooltip` itself, elsewhere.
    void setTooltipsDrawn(bool drawn) noexcept;
    // The tooltip shown at the last update, if any: its text, and where the pointer rested, in the
    // pixels of the image.
    struct DEVEX_API ShownTooltip
    {
        std::string_view text;
        math::Vec2 at{0.0f};
    };
    [[nodiscard]] std::optional<ShownTooltip> shownTooltip(const scene::Scene& scene) const;
    // Gives a field the keyboard, its text selected as a form does for its first field, or the
    // cursor at its end to go on typing. An area of text takes the keyboard the same way, its
    // cursor left where it was.
    void startEditing(const scene::Scene& scene, scene::Entity field, bool selectAll = true);

    // The areas of text. The colours of runs of their text, in its order: the words of a language.
    // A tool gives them for the lines in view, which `visibleTextLines` names, at every frame they
    // change; they are not kept with the scene. An area the world has not updated yet takes them
    // all the same, as from a script's Start.
    void setTextSpans(const scene::Scene& scene, scene::Entity area, std::vector<TextSpan> spans);
    // Runs drawn behind the letters, such as what a search found, and lines marked at their left.
    void setTextHighlights(const scene::Scene& scene, scene::Entity area, std::vector<TextSpan> highlights);
    void setTextMarks(const scene::Scene& scene, scene::Entity area, std::vector<TextLineMark> marks);
    // The first line an area shows, from 0, and how many it shows.
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleTextLines(const scene::Scene& scene, scene::Entity area) const;
    // Where the cursor of an area stands and where its selection started, in bytes of its text; the
    // two are equal when nothing is selected.
    [[nodiscard]] std::pair<std::size_t, std::size_t> textSelection(scene::Entity area) const noexcept;
    // Selects from `anchor` to `caret`, where the cursor ends, and brings it into view.
    void selectText(scene::Scene& scene, scene::Entity area, std::size_t anchor, std::size_t caret);
    // Goes back on the last change of an area, or makes it again. A change made to its text from
    // outside, by a tool or a script, is a change it goes back on too.
    void undoText(scene::Scene& scene, scene::Entity area);
    void redoText(scene::Scene& scene, scene::Entity area);
    [[nodiscard]] bool canUndoText(scene::Entity area) const noexcept;
    [[nodiscard]] bool canRedoText(scene::Entity area) const noexcept;
    // Forgets the changes an area could go back on, as when its text becomes another one.
    void forgetTextHistory(scene::Scene& scene, scene::Entity area);
    // The top of the cursor of an area and its height, in units of its canvas: where a list of
    // completions opens. Nothing for an area that was not laid out.
    struct DEVEX_API CaretPlace
    {
        math::Vec2 position{0.0f};
        float height = 0.0f;
    };
    [[nodiscard]] std::optional<CaretPlace> textCaretPlace(const scene::Scene& scene, scene::Entity area) const;
    // The line of an area under a point of the image, from 0; -1 outside its text.
    [[nodiscard]] std::int32_t textLineAt(const scene::Scene& scene, scene::Entity area, math::Vec2 point) const;
    // The area that takes what is typed, if any.
    [[nodiscard]] scene::Entity editedTextArea() const noexcept;

    // What the pointer carries, if anything.
    [[nodiscard]] const Carried* carried() const noexcept;
    // Announces, for the next update, a drag that started outside the interface, such as one from
    // another window of a tool: the targets that accept its type light up under the pointer, and
    // take it when the pointer is let go over them. Announced every frame the drag lasts.
    void carryFromOutside(std::string type, std::string data);
    // The drop target under the pointer that accepts what it carries.
    [[nodiscard]] scene::Entity dropTarget() const noexcept;
    // Whether something was dropped on a target of that action, or on that target, during the last
    // update.
    [[nodiscard]] bool wasDropped(std::string_view action) const;
    [[nodiscard]] bool wasDropped(scene::Entity target) const;
    // The last drop of the last update, if any.
    [[nodiscard]] const Drop* dropped() const noexcept;

    // Whether a button of that action was clicked during the last update. An action names as many
    // buttons as a game needs: any of them answers.
    [[nodiscard]] bool wasClicked(std::string_view action) const;
    [[nodiscard]] bool wasClicked(scene::Entity entity) const;
    // Whether a slider was moved or a toggle turned over during the last update.
    [[nodiscard]] bool wasChanged(std::string_view action) const;
    [[nodiscard]] bool wasChanged(scene::Entity entity) const;
    [[nodiscard]] bool wasCancelled() const noexcept;
    // The control the pointer holds while it drags it: a slider, a number field, a colour picker.
    // A tool that makes one step of a drag waits for it to be let go.
    [[nodiscard]] scene::Entity held() const noexcept;
    // The dropdown whose list is open, if any.
    [[nodiscard]] scene::Entity listedDropdown() const noexcept;
    // The value of a plot the pointer is over at the last update, from 0: the bar under it, or the
    // point of the line nearest across; -1 when the pointer is elsewhere.
    [[nodiscard]] std::int32_t plotValueAt(const scene::Scene& scene, scene::Entity plot) const;
    // The hue, saturation and value a colour picker shows, which keep their hue while the colour is
    // grey; false for an entity that is not one.
    [[nodiscard]] bool pickerHsv(const scene::Scene& scene, scene::Entity entity, math::Vec3& hsv) const;

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
    struct DEVEX_API ModalRange
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
    struct DEVEX_API Drag
    {
        DragKind kind = DragKind::None;
        scene::Entity entity;
        float grab = 0.0f;
        std::size_t column = 0;
    };

    // The dropdown whose list is open, the option under the pointer, and the first one shown.
    struct DEVEX_API OpenDropdown
    {
        scene::Entity entity;
        std::int32_t highlighted = -1;
        std::size_t first = 0;
    };

    struct DEVEX_API Tooltip
    {
        scene::Entity entity;
        float rested = 0.0f;
        bool shown = false;
        math::Vec2 at{0.0f};
    };
    struct DEVEX_API ButtonState
    {
        scene::Entity entity;
        math::Vec4 tint{1.0f};
    };

    // The field being edited: where its cursor stands, in bytes of its own text.
    struct DEVEX_API EditingField
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
    // Number fields, in Controls.cpp: dragged sideways once pressed, typed into once clicked, and
    // their value written into their text.
    void updateNumberFields(scene::Scene& scene, const UiInput& input);
    void beginNumberEdit(scene::Scene& scene, scene::Entity entity);
    void finishNumberEdit(scene::Scene& scene);
    void setNumber(scene::Entity entity, scene::UiNumberField& field, double value);
    void writeNumberTexts(scene::Scene& scene) const;
    void updateColorPickers(scene::Scene& scene, const UiInput& input);
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
    // Takes a drag source away once the pointer moved far enough from where it was pressed, finds
    // the target under what it carries, and drops it there. Answers whether it holds the pointer.
    [[nodiscard]] bool updateCarry(scene::Scene& scene, const UiInput& input, bool taken);
    [[nodiscard]] scene::Entity targetUnder(const scene::Scene& scene, math::Vec2 pointer, const Carried& carried) const;
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
    // The letters of the texts as the last drawing placed them, which the next one takes again for
    // the texts that did not change.
    mutable TextCache m_textCache;
    // Kept from one frame to the next, so that a field that is typed into allocates nothing.
    TextLayoutResult m_fieldLayout;

    math::Vec2 m_windowSize{0.0f};
    // Seconds since the world started, which tells a double click from two clicks.
    double m_clock = 0.0;
    std::vector<PopupPlacement> m_popupPlacements;
    std::optional<std::pair<math::Vec2, math::Vec2>> m_popupArea;
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
    bool m_tooltipsDrawn = true;

    // A drag source pressed, and where, until the pointer moves far enough to take it away.
    struct DEVEX_API DragCandidate
    {
        scene::Entity source;
        math::Vec2 from{0.0f};
    };
    std::optional<DragCandidate> m_dragCandidate;
    std::optional<Carried> m_carried;
    // A drag from outside, announced for the next update.
    std::optional<Carried> m_outside;
    scene::Entity m_dropTarget;
    std::vector<Drop> m_drops;
    std::vector<std::string> m_dropActions;
    // Where the pointer was at the last update, which the carried label follows.
    math::Vec2 m_pointer{0.0f};

    // A number field pressed: dragged once the pointer moves far enough, typed into if it is let go
    // without moving. The value follows `raw`, which keeps what rounding to a step would lose.
    struct DEVEX_API NumberDrag
    {
        scene::Entity entity;
        math::Vec2 from{0.0f};
        float lastX = 0.0f;
        double raw = 0.0;
        bool moved = false;
    };
    std::optional<NumberDrag> m_numberDrag;
    // The number field being typed into, read once its edit ends unless Escape ended it or its text
    // is still the one it started with, which may hold fewer digits than the value.
    scene::Entity m_numberEdit;
    std::string m_numberEditText;
    bool m_editCancelled = false;

    // What a colour picker shows beside its colour: the hue kept while the colour is grey, and the
    // intensity of a colour brighter than white, which the square leaves as it is.
    struct DEVEX_API PickerState
    {
        scene::Entity entity;
        math::Vec4 color{-1.0f};
        math::Vec3 hsv{0.0f};
        float intensity = 1.0f;
    };
    enum class PickerPart : std::uint8_t
    {
        None,
        Square,
        Hue,
        Alpha,
    };
    [[nodiscard]] PickerState& pickerState(scene::Entity entity, math::Vec4 color);
    std::vector<PickerState> m_pickers;
    scene::Entity m_pickerHeld;
    PickerPart m_pickerPart = PickerPart::None;

    // The areas of text, in TextAreas.cpp. A change of the text of an area: what stood there, and
    // what stands there now, with the cursor before and after it.
    struct DEVEX_API TextChange
    {
        std::size_t offset = 0;
        std::string removed;
        std::string inserted;
        std::size_t caretBefore = 0;
        std::size_t anchorBefore = 0;
        std::size_t caretAfter = 0;
        // Letters typed one after the other make one change to go back on.
        bool typing = false;
    };
    // What the world keeps of an area from a frame to the next.
    struct DEVEX_API TextAreaState
    {
        scene::Entity entity;
        // The text as the area last saw it, which tells a change made from outside.
        std::string known;
        std::vector<std::uint32_t> lineStarts;
        // The width of the longest line, in units.
        float contentWidth = 0.0f;
        std::size_t caret = 0;
        std::size_t anchor = 0;
        // Where the cursor wants to stand across while it goes up and down; negative when unset.
        float wantedX = -1.0f;
        std::vector<TextChange> undo;
        std::vector<TextChange> redo;
        std::vector<TextSpan> spans;
        std::vector<TextSpan> highlights;
        std::vector<TextLineMark> marks;
        mutable TextAreaView view;
        bool seen = false;
    };
    enum class AreaBar : std::uint8_t
    {
        None,
        Vertical,
        Horizontal,
    };
    [[nodiscard]] TextAreaState* areaState(scene::Entity entity) noexcept;
    [[nodiscard]] const TextAreaState* areaState(scene::Entity entity) const noexcept;
    [[nodiscard]] TextAreaState& ensureAreaState(scene::Entity entity, const std::string& text);
    // What an area of the scene keeps, made for one not updated yet; null for what is no area.
    [[nodiscard]] TextAreaState* areaStateOf(const scene::Scene& scene, scene::Entity entity);
    [[nodiscard]] const TextAreaView* textAreaView(scene::Entity entity) const noexcept;
    // Reads the text again when it changed outside the area, as a change it can go back on.
    void syncArea(TextAreaState& state, std::string& text);
    void indexArea(const scene::Scene& scene, TextAreaState& state, const std::string& text);
    // Replaces a run of the text, remembers it, and leaves the cursor after what was written.
    void changeArea(scene::Scene& scene, TextAreaState& state, std::string& text, std::size_t offset, std::size_t removed,
                    std::string_view inserted, bool typing);
    void applyTextChange(scene::Scene& scene, TextAreaState& state, std::string& text, const TextChange& change, bool forward);
    void notifyArea(const scene::Scene& scene, scene::Entity entity);
    // The font of an area and where it puts things, when it was laid out.
    [[nodiscard]] bool areaPlace(const scene::Scene& scene, scene::Entity entity, const asset::FontData*& font, TextAreaMetrics& metrics,
                                 const LaidOutRect*& rect, float& scale) const;
    [[nodiscard]] std::size_t areaOffsetAt(const scene::Scene& scene, const TextAreaState& state, math::Vec2 point) const;
    void revealCaret(scene::Scene& scene, TextAreaState& state);
    // The wheel over an area moves its lines; answers whether it took it.
    [[nodiscard]] bool scrollTextArea(scene::Scene& scene, const UiInput& input);
    void updateTextAreas(scene::Scene& scene, const UiInput& input, core::Duration delta);
    void editTextArea(scene::Scene& scene, TextAreaState& state, const UiInput& input);

    std::vector<TextAreaState> m_areas;
    // The area that takes what is typed, and what the pointer holds of it.
    scene::Entity m_areaFocus;
    bool m_areaSelecting = false;
    AreaBar m_areaBar = AreaBar::None;
    float m_areaGrab = 0.0f;
    float m_areaBlink = 0.0f;
    // The last press on an area, for the next one to select a word, then a line.
    double m_areaPressTime = -1.0;
    std::size_t m_areaPressOffset = 0;
    int m_areaPresses = 0;
};

// The text a number field shows: at most `decimals` digits after the point, without the zeros that
// end them, and never -0.
[[nodiscard]] DEVEX_API std::string formatNumber(double value, std::int32_t decimals);
// What a number field reads from what was typed: a number, or a sum of numbers with + - * / and
// brackets, a comma read as a point. Nothing when it cannot be read.
[[nodiscard]] DEVEX_API std::optional<double> evaluateNumber(std::string_view text);

} // namespace devex::ui
