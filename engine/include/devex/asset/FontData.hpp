#pragma once

#include <devex/core/Error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace devex::asset {

// One character of a font, as it sits in the atlas of distances. Positions and sizes are in the
// pixels the font was baked at, and scale with the size the text is drawn at.
struct FontGlyph
{
    std::uint32_t codepoint = 0;
    // Where the image of the glyph is in the atlas.
    std::uint16_t atlasX = 0;
    std::uint16_t atlasY = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    // Where to draw that image, from the pen, with Y going down as the screen does.
    float left = 0.0f;
    float top = 0.0f;
    // How far the pen moves after drawing it.
    float advance = 0.0f;
};

// How much closer two letters sit when one follows the other: an A after a V leans under it. The
// amount is in the pixels the font was baked at, and is usually negative.
struct FontKerning
{
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    float amount = 0.0f;
};

// A font baked into one atlas of signed distances: every glyph stays sharp at any size, since the
// shader reads how far each pixel is from the outline instead of a picture of the letter.
struct FontData
{
    std::string family;
    // The em of the font, in pixels, the distances were computed at.
    float bakedSize = 48.0f;
    // How far, in pixels of the baked size, the distances spread around the outline.
    float spread = 6.0f;
    // Above and below the baseline, and the gap between two lines.
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    std::uint32_t atlasWidth = 0;
    std::uint32_t atlasHeight = 0;
    // One byte per pixel: the distance to the outline, with 128 on the outline itself.
    std::vector<std::uint8_t> atlas;
    // Sorted by codepoint, so that a glyph is found by halving.
    std::vector<FontGlyph> glyphs;
    // Sorted by the pair they join, and holding only the pairs that move.
    std::vector<FontKerning> kerning;

    // The height of one line of text at the baked size.
    [[nodiscard]] float lineHeight() const noexcept
    {
        return ascent - descent + lineGap;
    }
};

// The glyph of a character, or nothing when the font does not have it.
[[nodiscard]] const FontGlyph* findGlyph(const FontData& font, std::uint32_t codepoint) noexcept;

// How much the second letter moves towards the first, in the pixels the font was baked at; 0 when
// the pair was not baked.
[[nodiscard]] float kerningBetween(const FontData& font, std::uint32_t first,
                                   std::uint32_t second) noexcept;

// Checks that the atlas matches its size and that the glyphs fit inside it.
[[nodiscard]] core::Result<void> validate(const FontData& font);

} // namespace devex::asset
