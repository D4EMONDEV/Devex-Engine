#pragma once

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
struct Canvas
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
DEVEX_DECLARE_REFLECTION(Canvas);

// The rectangle of an element inside its parent. The anchors are the fractions of the parent the
// corners hang from, and the offsets move each corner away from its anchor in units: anchors equal
// on an axis give a fixed size, anchors apart stretch with the parent.
//
//     a whole panel      anchorMin {0, 0}     anchorMax {1, 1}   offsets {0, 0}
//     a title at the top anchorMin {0, 0}     anchorMax {1, 0}   offsetMax {0, 80}
//     a centred button   anchorMin {0.5, 0.5} anchorMax {0.5, 0.5}
//
// X goes right and Y goes down, as the screen does.
struct UiRect
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
DEVEX_DECLARE_REFLECTION(UiRect);

// A coloured rectangle, with a texture when it has one. Borders keep the corners of a texture
// unstretched, which lets one image draw a panel of any size.
struct UiImage
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
DEVEX_DECLARE_REFLECTION(UiImage);

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
struct UiText
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
DEVEX_DECLARE_REFLECTION(UiText);

// Makes the text of its entity editable. The letters, the font, the size and the colour come from
// the UiText beside it, which also holds what was typed; this component says how it is edited.
struct UiInput
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
DEVEX_DECLARE_REFLECTION(UiInput);

// Answers the mouse, the keyboard and the pad on the rectangle of its entity. It tints the UiImage
// of the entity as the pointer comes and goes, and reports its clicks to the scripts under the
// name of its action.
struct UiButton
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
DEVEX_DECLARE_REFLECTION(UiButton);

// Writes a value read from a component into the text of the entity, once a frame. The text of
// the UiText beside it is replaced: this is where the sentence lives, and every {} in it takes
// the value, so that a score or a health bar follows the game without a line of script.
struct UiBinding
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
DEVEX_DECLARE_REFLECTION(UiBinding);

// A value the pointer drags between two ends. The rectangle of the entity is the track: the part
// before the value is filled, and the handle sits on it. A UiImage on the same entity draws what
// lies under them, and the arrows move the value while the slider has the focus.
struct UiSlider
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
DEVEX_DECLARE_REFLECTION(UiSlider);

// A box that is either on or off. Clicking it, or pressing the submit button while it has the
// focus, turns it over; the mark is drawn inside the rectangle of the entity.
struct UiToggle
{
    bool value = false;
    math::Vec4 checkColor{1.0f, 1.0f, 1.0f, 1.0f};
    // The mark fills this much of the box.
    float checkSize = 0.55f;
    bool interactable = true;
    // What a script asks for: Ui.WasChanged("fullscreen").
    std::string action;
};
DEVEX_DECLARE_REFLECTION(UiToggle);

// Moves what it holds, so that a list longer than its rectangle can be walked through. The
// element cuts its children by itself: it does not need clipChildren as well.
struct UiScroll
{
    // How far the content is moved, in units; 0 shows its start.
    math::Vec2 offset{0.0f, 0.0f};
    bool horizontal = false;
    bool vertical = true;
    // Units the wheel moves the content by, per notch.
    float speed = 60.0f;
};
DEVEX_DECLARE_REFLECTION(UiScroll);

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
struct UiLayout
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
DEVEX_DECLARE_REFLECTION(UiLayout);

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
