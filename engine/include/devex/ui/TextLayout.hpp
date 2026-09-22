#pragma once

#include <devex/asset/FontData.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/UiComponents.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Where the letters of a text land inside its rectangle. The result feeds the drawing and the
// editor alike, and stays pure so that tests read it directly.
namespace devex::ui {

// One letter: the rectangle it covers and the part of the atlas it reads.
struct GlyphQuad
{
    math::Vec2 min{0.0f};
    math::Vec2 max{0.0f};
    math::Vec2 uvMin{0.0f};
    math::Vec2 uvMax{0.0f};
    // Multiplies the colour of the text, for the spans a rich text colours itself.
    math::Vec4 color{1.0f};
    // Drawn a second time, a little aside, to stand in for a bold face the font does not carry.
    bool bold = false;
    // Leaned to the right, to stand in for an italic face.
    bool italic = false;
};

// An image asked for between two words, by its place in the list the text carries.
struct InlineImage
{
    std::uint32_t index = 0;
    math::Vec2 min{0.0f};
    math::Vec2 max{0.0f};
};

// Where the cursor can stand: before a character, or at the end of a line.
struct CaretStop
{
    // Byte offset in the text, as it was given, tags included.
    std::size_t offset = 0;
    // The top of the cursor, and how tall it is.
    math::Vec2 position{0.0f};
    float height = 0.0f;
    // The line it belongs to, counted from the first.
    std::uint32_t line = 0;
};

struct TextStyle
{
    // The height of an em, in the units of the canvas.
    float size = 24.0f;
    scene::TextAlign align = scene::TextAlign::Left;
    scene::TextVerticalAlign verticalAlign = scene::TextVerticalAlign::Top;
    bool wrap = true;
    float lineSpacing = 1.0f;
    // Reads [b], [i], [color=#rrggbb], [size=32] and [icon=0] as marks rather than as letters.
    // Two opening brackets in a row write one.
    bool rich = false;
};

struct TextLayoutResult
{
    std::vector<GlyphQuad> glyphs;
    std::vector<InlineImage> images;
    // One per place the cursor can stand, in the order of the text.
    std::vector<CaretStop> stops;
    // The room the text takes, whatever the rectangle it was placed in.
    math::Vec2 size{0.0f};
    std::size_t lineCount = 0;

    void clear() noexcept
    {
        glyphs.clear();
        images.clear();
        stops.clear();
        size = math::Vec2{0.0f};
        lineCount = 0;
    }
};

// Places the letters of the text inside the rectangle, wrapping and aligning them. Letters the
// font does not have are skipped.
void layoutText(const asset::FontData& font, std::string_view text, const TextStyle& style,
                math::Vec2 boxMin, math::Vec2 boxMax, TextLayoutResult& result);

// How a field lays out what it holds: never reading tags, since a field shows what was typed,
// and on one line unless it takes several.
[[nodiscard]] TextStyle fieldStyle(const scene::UiText& text, const scene::UiInput& field) noexcept;
// Where the letters of a field go inside its rectangle, once its padding is taken off.
void fieldBox(const scene::UiInput& field, math::Vec2 rectMin, math::Vec2 rectMax, math::Vec2& boxMin,
              math::Vec2& boxMax) noexcept;

// The room the text takes on its own, with a width to wrap at, or 0 for one line per paragraph.
[[nodiscard]] math::Vec2 measureText(const asset::FontData& font, std::string_view text,
                                     const TextStyle& style, float wrapWidth = 0.0f);

// Where the cursor stands for a byte offset, and the offset nearest to a point. Both read a text
// that was already laid out, so that a field measures itself once per frame.
[[nodiscard]] const CaretStop* caretAt(const TextLayoutResult& layout, std::size_t offset) noexcept;
[[nodiscard]] std::size_t offsetAt(const TextLayoutResult& layout, math::Vec2 point) noexcept;

// How wide, in atlas texels, one unit of the drawn text is: the shader keeps the edge of a letter
// one pixel wide from it.
[[nodiscard]] float textSharpness(const asset::FontData& font, float size) noexcept;

// The next character of a UTF-8 text, and where the one after it starts. Invalid bytes each come
// back as one unknown character, so that a broken text still draws.
[[nodiscard]] std::uint32_t nextCodepoint(std::string_view text, std::size_t& offset) noexcept;
// The start of the character before an offset, for a cursor that steps back.
[[nodiscard]] std::size_t previousOffset(std::string_view text, std::size_t offset) noexcept;

// How many characters come before a byte offset, and where the n-th character starts. A password
// draws dots rather than letters, so the cursor is carried from one text to the other by counting
// characters rather than bytes.
[[nodiscard]] std::size_t characterIndexOf(std::string_view text, std::size_t offset) noexcept;
[[nodiscard]] std::size_t offsetOfCharacter(std::string_view text, std::size_t index) noexcept;
// What a field shows: its text, or one dot per character when it hides what is typed.
[[nodiscard]] std::string shownText(std::string_view text, bool password);

} // namespace devex::ui
