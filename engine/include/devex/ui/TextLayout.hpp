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
};

struct TextStyle
{
    // The height of an em, in the units of the canvas.
    float size = 24.0f;
    scene::TextAlign align = scene::TextAlign::Left;
    scene::TextVerticalAlign verticalAlign = scene::TextVerticalAlign::Top;
    bool wrap = true;
    float lineSpacing = 1.0f;
};

struct TextLayoutResult
{
    std::vector<GlyphQuad> glyphs;
    // The room the text takes, whatever the rectangle it was placed in.
    math::Vec2 size{0.0f};
    std::size_t lineCount = 0;

    void clear() noexcept
    {
        glyphs.clear();
        size = math::Vec2{0.0f};
        lineCount = 0;
    }
};

// Places the letters of the text inside the rectangle, wrapping and aligning them. Letters the
// font does not have are skipped.
void layoutText(const asset::FontData& font, std::string_view text, const TextStyle& style,
                math::Vec2 boxMin, math::Vec2 boxMax, TextLayoutResult& result);

// The room the text takes on its own, with a width to wrap at, or 0 for one line per paragraph.
[[nodiscard]] math::Vec2 measureText(const asset::FontData& font, std::string_view text,
                                     const TextStyle& style, float wrapWidth = 0.0f);

// How wide, in atlas texels, one unit of the drawn text is: the shader keeps the edge of a letter
// one pixel wide from it.
[[nodiscard]] float textSharpness(const asset::FontData& font, float size) noexcept;

// The next character of a UTF-8 text, and where the one after it starts. Invalid bytes each come
// back as one unknown character, so that a broken text still draws.
[[nodiscard]] std::uint32_t nextCodepoint(std::string_view text, std::size_t& offset) noexcept;

} // namespace devex::ui
