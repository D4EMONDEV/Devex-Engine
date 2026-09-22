#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

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
};
DEVEX_DECLARE_REFLECTION(UiText);

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
