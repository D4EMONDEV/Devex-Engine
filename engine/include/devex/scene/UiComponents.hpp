#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/EntityRef.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Interface components: what the interface world lays out, draws and answers to. They hold data
// only; the Ui module places them on the screen while a game runs, and the editor shows them on
// its 2D screen.
namespace devex::scene {

// How a canvas turns its own units into pixels of the screen.
enum class CanvasScaleMode : std::uint8_t
{
    // One unit is one pixel, whatever the size of the window: crisp, but small on large screens.
    ConstantPixels,
    // The canvas is laid out at its reference resolution, then scaled to the window, so that an
    // interface designed once fits every screen.
    ScaleWithScreen,
};

// The root of an interface. Every UiRect under it is placed inside its rectangle, which covers the
// whole window. Canvases are drawn over the game, in the order of their sortOrder.
struct DEVEX_API Canvas
{
    CanvasScaleMode scaleMode = CanvasScaleMode::ScaleWithScreen;
    // The size, in units, the interface was designed at.
    math::Vec2 referenceResolution{1920.0f, 1080.0f};
    // Follows the width of the window at 0 and its height at 1; between the two it mixes them,
    // which keeps an interface usable on screens both wider and taller than the reference.
    float matchWidthOrHeight = 0.5f;
    // The look its elements follow: a theme asset of named styles. Without one, every element
    // keeps the values it carries.
    asset::AssetId theme;
    // Canvases with a higher order are drawn over the others; a pause menu sits above a HUD.
    std::int32_t sortOrder = 0;
    bool visible = true;
    // Answers the mouse and the pad. A HUD that only shows numbers can turn it off, so that
    // clicks reach whatever lies underneath.
    bool interactive = true;
};
DEVEX_DECLARE_ENGINE_REFLECTION(Canvas);

// The rectangle of an element inside its parent. The anchors are the fractions of the parent the
// corners hang from, and the offsets move each corner away from its anchor in units: anchors equal
// on an axis give a fixed size, anchors apart stretch with the parent.
//
//     a whole panel      anchorMin {0, 0}     anchorMax {1, 1}   offsets {0, 0}
//     a title at the top anchorMin {0, 0}     anchorMax {1, 0}   offsetMax {0, 80}
//     a centred button   anchorMin {0.5, 0.5} anchorMax {0.5, 0.5}
//
// X goes right and Y goes down, as the screen does.
struct DEVEX_API UiRect
{
    math::Vec2 anchorMin{0.5f, 0.5f};
    math::Vec2 anchorMax{0.5f, 0.5f};
    // The left and top corner, from the minimum anchor.
    math::Vec2 offsetMin{-100.0f, -25.0f};
    // The right and bottom corner, from the maximum anchor.
    math::Vec2 offsetMax{100.0f, 25.0f};
    // The point the element turns and grows around, as a fraction of its own rectangle.
    math::Vec2 pivot{0.5f, 0.5f};
    math::Vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;
    // Multiplies the transparency of the element and of everything under it.
    float opacity = 1.0f;
    bool visible = true;
    // Cuts everything under it to its own rectangle, as a list or a panel does.
    bool clipChildren = false;
    // The style of the theme of the canvas this element follows: "panel", "title". The values the
    // style names are written into the components of the element, every frame.
    std::string style;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiRect);

// A coloured rectangle, with a texture when it has one. Borders keep the corners of a texture
// unstretched, which lets one image draw a panel of any size.
struct DEVEX_API UiImage
{
    asset::AssetId texture;
    // Multiplies the texture, or fills the rectangle on its own.
    math::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    // The borders kept unstretched, as fractions of the texture: left, top, right and bottom. All
    // zero stretches the whole image, and 0.25 keeps a quarter of it at each side.
    math::Vec4 border{0.0f, 0.0f, 0.0f, 0.0f};
    // Rounds the corners, in units. A rectangle with no texture then draws a rounded panel.
    float cornerRadius = 0.0f;
    // Whether the mouse and the pad stop on it, rather than passing through.
    bool raycastTarget = true;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiImage);

enum class TextAlign : std::uint8_t
{
    Left,
    Center,
    Right,
};

enum class TextVerticalAlign : std::uint8_t
{
    Top,
    Middle,
    Bottom,
};

// A line or a paragraph of text, drawn from the atlas of distances of its font, which keeps the
// letters sharp at any size.
struct DEVEX_API UiText
{
    std::string text = "Text";
    asset::AssetId font;
    // In units; the font is baked once and drawn at any size.
    float size = 24.0f;
    math::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align = TextAlign::Left;
    TextVerticalAlign verticalAlign = TextVerticalAlign::Top;
    // Cuts long lines at the width of the rectangle, between words when it can.
    bool wrap = true;
    // Multiplies the height of a line of the font.
    float lineSpacing = 1.0f;
    // Draws the text a second time around itself; a width of 0 leaves it plain.
    math::Vec4 outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    float outlineWidth = 0.0f;
    bool raycastTarget = false;
    // Reads [b], [i], [color=#rrggbb], [size=32] and [icon=0] as marks rather than as letters.
    // Two opening brackets in a row write one.
    bool rich = false;
    // The images [icon=n] draws, in the order a text asks for them.
    std::vector<asset::AssetId> icons;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiText);

// Makes the text of its entity editable. The letters, the font, the size and the colour come from
// the UiText beside it, which also holds what was typed; this component says how it is edited.
struct DEVEX_API UiInput
{
    // Shown, dimmed, while the text is empty.
    std::string placeholder;
    math::Vec4 placeholderColor{0.5f, 0.5f, 0.5f, 1.0f};
    math::Vec4 selectionColor{0.2f, 0.4f, 0.9f, 0.5f};
    math::Vec4 caretColor{1.0f, 1.0f, 1.0f, 1.0f};
    // The room between the edges of the field and its letters: across, then down.
    math::Vec2 padding{12.0f, 0.0f};
    // Takes new lines rather than ending the edit on Enter.
    bool multiline = false;
    // Draws every letter as a dot, for a password.
    bool password = false;
    // Longest text the field takes, in characters; 0 does not limit it.
    std::uint32_t maxLength = 0;
    bool interactable = true;
    // What a script asks for when Enter ends the edit: Ui.WasSubmitted("name").
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiInput);

// Answers the mouse, the keyboard and the pad on the rectangle of its entity. It tints the UiImage
// of the entity as the pointer comes and goes, and reports its clicks to the scripts under the
// name of its action.
struct DEVEX_API UiButton
{
    // What a script asks for: Ui.WasClicked("play"). An empty action is still clickable, and is
    // then read by entity.
    std::string action;
    bool interactable = true;
    // Multiply the color of the image of the entity.
    math::Vec4 hoverColor{1.15f, 1.15f, 1.15f, 1.0f};
    math::Vec4 pressedColor{0.8f, 0.8f, 0.8f, 1.0f};
    math::Vec4 disabledColor{0.5f, 0.5f, 0.5f, 0.6f};
    // Seconds the tint takes to follow the pointer.
    float fadeTime = 0.1f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiButton);

// Writes a value read from a component into the text of the entity, once a frame. The text of
// the UiText beside it is replaced: this is where the sentence lives, and every {} in it takes
// the value, so that a score or a health bar follows the game without a line of script.
struct DEVEX_API UiBinding
{
    // The component the value is read from, by its name: "Transform", or a component of the game.
    std::string component = "Transform";
    // The field inside it, and the part of it that is wanted when it holds several numbers:
    // "position.x", "color.a".
    std::string field;
    // What the text becomes, with the value in place of every {}.
    std::string format = "{}";
    // Digits after the point of a number; a negative number writes it as it reads.
    std::int32_t decimals = -1;
    // The entity the value is read from; the one carrying the binding when it is not set.
    EntityRef source;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiBinding);

// A value the pointer drags between two ends. The rectangle of the entity is the track: the part
// before the value is filled, and the handle sits on it. A UiImage on the same entity draws what
// lies under them, and the arrows move the value while the slider has the focus.
struct DEVEX_API UiSlider
{
    float value = 0.5f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    // Rounds the value to a multiple of this, counted from the smaller end; 0 leaves it free.
    float step = 0.0f;
    // The part of the track before the handle.
    math::Vec4 fillColor{0.35f, 0.6f, 1.0f, 1.0f};
    math::Vec4 handleColor{1.0f, 1.0f, 1.0f, 1.0f};
    // The handle is as wide as the track is tall, times this; 0 hides it.
    float handleSize = 1.0f;
    // What the arrows and the pad move the value by; 0 uses a twentieth of the range.
    float keyStep = 0.0f;
    bool interactable = true;
    // What a script asks for: Ui.WasChanged("volume").
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiSlider);

// A box that is either on or off. Clicking it, or pressing the submit button while it has the
// focus, turns it over; the mark is drawn inside the rectangle of the entity.
struct DEVEX_API UiToggle
{
    bool value = false;
    math::Vec4 checkColor{1.0f, 1.0f, 1.0f, 1.0f};
    // The mark fills this much of the box.
    float checkSize = 0.55f;
    bool interactable = true;
    // What a script asks for: Ui.WasChanged("fullscreen").
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiToggle);

// Moves what it holds, so that a list longer than its rectangle can be walked through. The
// element cuts its children by itself: it does not need clipChildren as well.
struct DEVEX_API UiScroll
{
    // How far the content is moved, in units; 0 shows its start.
    math::Vec2 offset{0.0f, 0.0f};
    bool horizontal = false;
    bool vertical = true;
    // Units the wheel moves the content by, per notch.
    float speed = 60.0f;
    // A bar along the edge while the content is longer than the element, whose thumb the pointer
    // drags and whose track moves the content a page.
    bool scrollbar = true;
    float scrollbarSize = 8.0f;
    math::Vec4 scrollbarColor{1.0f, 1.0f, 1.0f, 0.3f};
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiScroll);

// How a popup behaves once open.
enum class UiPopupKind : std::uint8_t
{
    // Closes when one of its buttons is clicked, when the pointer presses outside it, or on
    // Escape: context menus and the menus of a menu bar.
    Menu,
    // Stays until the game closes it, and keeps the rest of its canvas from the pointer and the
    // keys under a veil: confirmations and forms.
    Modal,
};

// An element shown over the rest of its canvas while it is open, and hidden otherwise: its UiRect
// is visible only while it is open. Ui.OpenPopup opens it where its anchors put it, or at a point
// of the screen; a UiContextMenu opens it under the pointer.
struct DEVEX_API UiPopup
{
    UiPopupKind kind = UiPopupKind::Menu;
    // A modal darkens what lies under it with this.
    math::Vec4 veilColor{0.0f, 0.0f, 0.0f, 0.45f};
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiPopup);

// Opens a popup menu under the pointer when the element, or an element inside it, is clicked with
// the second button: the menu of a row of a list. Ui.ContextTarget then names the element.
struct DEVEX_API UiContextMenu
{
    EntityRef popup;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiContextMenu);

// A line of help shown next to the pointer once it has rested on the element, even on an element
// that cannot be used.
struct DEVEX_API UiTooltip
{
    std::string text;
    // Seconds the pointer rests before it shows.
    float delay = 0.5f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiTooltip);

// A button that shows one of its options, and a list of all of them to choose from once clicked.
// The UiText of the entity shows the chosen option, and the list takes its font, size and color.
struct DEVEX_API UiDropdown
{
    std::vector<std::string> options;
    // The chosen option, from 0; -1 shows none.
    std::int32_t selected = 0;
    // Behind the list, and behind the option under the pointer.
    math::Vec4 listColor{0.14f, 0.15f, 0.18f, 1.0f};
    math::Vec4 highlightColor{0.35f, 0.6f, 1.0f, 1.0f};
    math::Vec4 arrowColor{1.0f, 1.0f, 1.0f, 0.7f};
    bool interactable = true;
    // What a script asks for: Ui.WasChanged("quality").
    std::string action;
    // Shown while no option is chosen: "Choose a class".
    std::string placeholder;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiDropdown);

// A number the pointer drags sideways, or types once the field is clicked, as the fields of an
// inspector. The UiText of the entity shows it, and the UiInput beside it takes what is typed,
// which may be a sum such as 2*3+1.
struct DEVEX_API UiNumberField
{
    float value = 0.0f;
    // The value stays between them when the smaller is below the larger; equal, it is free.
    float minValue = 0.0f;
    float maxValue = 0.0f;
    // Rounds the value to a multiple of this, counted from the smaller end; 0 leaves it free.
    float step = 0.0f;
    // What a unit the pointer moves across adds to the value; Shift makes it ten times smaller.
    float dragSpeed = 0.01f;
    // Digits shown after the point at most: 1.5 rather than 1.500.
    std::int32_t decimals = 3;
    // What the text shows, the value in place of {}: "{} m", "{}°". Without {} it shows as it is,
    // as a dash says that several values differ.
    std::string format = "{}";
    bool interactable = true;
    // What a script asks for: Ui.WasChanged("speed").
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiNumberField);

// Chooses a colour in its rectangle: a square of saturation across and brightness down for the hue
// of the bar at its right, and a bar of opacity under them. The pointer drags in each part. The
// colour is linear, as the colours of images are; the square spreads it as the eye sees it.
struct DEVEX_API UiColorPicker
{
    math::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    // Whether the bar of opacity shows; without it the opacity stays as it is.
    bool alpha = true;
    // The width of the bars, and the room between the parts, in units.
    float barSize = 16.0f;
    float spacing = 8.0f;
    bool interactable = true;
    // What a script asks for: Ui.WasChanged("tint").
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiColorPicker);

// Shares its rectangle between its first two children, with a bar between them the pointer drags:
// a panel beside another, or above it.
struct DEVEX_API UiSplitter
{
    // One above the other rather than side by side.
    bool vertical = false;
    // Where the bar starts, in units from the start of the element.
    float position = 200.0f;
    // Neither child gets smaller than this.
    float minSize = 40.0f;
    float barSize = 6.0f;
    math::Vec4 barColor{1.0f, 1.0f, 1.0f, 0.06f};
    math::Vec4 hoverColor{0.35f, 0.6f, 1.0f, 0.8f};
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiSplitter);

// A header that shows or hides the element it names, with an arrow at its left that says which:
// the sections of an inspector, or the branches of a tree when foldouts hold each other.
struct DEVEX_API UiFoldout
{
    bool expanded = true;
    // Shown while expanded; its UiRect is hidden otherwise.
    EntityRef content;
    math::Vec4 arrowColor{1.0f, 1.0f, 1.0f, 0.7f};
    bool interactable = true;
    // What a script asks for: Ui.WasChanged("details").
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiFoldout);

// A long list shown with a handful of elements: its children are the rows on screen, placed at the
// items they show as the list scrolls, which a script fills from the item in `first`. The element must
// sit in a UiScroll: it takes the height of every item, and draws only those in view.
struct DEVEX_API UiVirtualList
{
    std::uint32_t itemCount = 0;
    // The height of one item, in units.
    float itemSize = 32.0f;
    // The item its first child shows, which the interface writes as the list scrolls.
    std::uint32_t first = 0;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiVirtualList);

// Lines its rows up in columns: the children of each UiTableRow among its descendants are the
// cells, one per column, as wide as the column. The row marked header resizes the columns when
// the edge of one of its cells is dragged, and sorts by a column when it is clicked.
struct DEVEX_API UiTable
{
    // The width of each column, in units.
    std::vector<float> columns{160.0f, 160.0f};
    bool resizable = true;
    // The column the rows are sorted by, -1 for none, and which way: a click on the header picks
    // them, and a script sorts its rows once Ui.WasChanged of the action says so.
    std::int32_t sortColumn = -1;
    bool sortAscending = true;
    math::Vec4 arrowColor{1.0f, 1.0f, 1.0f, 0.7f};
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiTable);

// A row of the UiTable above it.
struct DEVEX_API UiTableRow
{
    // The row of the column titles.
    bool header = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiTableRow);

// Lets the pointer carry the element to a UiDropTarget: pressed on it and moved a few pixels, what it
// holds follows the pointer until it is let go. A button carried away is not clicked.
struct DEVEX_API UiDragSource
{
    // What is carried, which the targets accept by name: "item", "asset".
    std::string type;
    // What the target is given: a name, an identifier.
    std::string data;
    // Shown next to the pointer while it is carried; the first text at or under the element when
    // empty.
    std::string label;
    bool interactable = true;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiDragSource);

// Takes what is dropped on it, or on an element inside it, when it accepts its type; lit while
// the pointer carries something it accepts over it.
struct DEVEX_API UiDropTarget
{
    std::vector<std::string> accepts;
    math::Vec4 highlightColor{0.35f, 0.6f, 1.0f, 0.3f};
    // What a script asks for: Ui.WasDropped("slot").
    std::string action;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiDropTarget);

// How the children of a container follow each other.
enum class UiLayoutKind : std::uint8_t
{
    // Side by side, left to right.
    Row,
    // One under the other.
    Column,
    // In rows of `columns` cells of the same size.
    Grid,
};

// Places the children of its entity itself, instead of leaving them to their anchors: a menu of
// buttons then keeps its spacing whatever it holds. Children keep their own size on the axis the
// container does not drive, unless they stretch.
struct DEVEX_API UiLayout
{
    UiLayoutKind kind = UiLayoutKind::Column;
    // Units between two children.
    float spacing = 8.0f;
    // Inside the rectangle: left, top, right and bottom.
    math::Vec4 padding{0.0f, 0.0f, 0.0f, 0.0f};
    // Cells per row of a grid; ignored by rows and columns.
    std::uint32_t columns = 3;
    // Gives every child the same share of the axis, rather than the size of its rectangle.
    bool equalSize = false;
    // Where the children sit on the axis of the container when they do not fill it.
    TextAlign align = TextAlign::Center;
};
DEVEX_DECLARE_ENGINE_REFLECTION(UiLayout);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::CanvasScaleMode>
{
    static constexpr std::array<std::string_view, 2> names{"constant_pixels", "scale_with_screen"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::TextAlign>
{
    static constexpr std::array<std::string_view, 3> names{"left", "center", "right"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::TextVerticalAlign>
{
    static constexpr std::array<std::string_view, 3> names{"top", "middle", "bottom"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::UiLayoutKind>
{
    static constexpr std::array<std::string_view, 3> names{"row", "column", "grid"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::UiPopupKind>
{
    static constexpr std::array<std::string_view, 2> names{"menu", "modal"};
};
