#include <devex/asset/FontData.hpp>

#include <algorithm>

namespace devex::asset {

const FontGlyph* findGlyph(const FontData& font, std::uint32_t codepoint) noexcept
{
    const auto found = std::ranges::lower_bound(font.glyphs, codepoint, {}, &FontGlyph::codepoint);
    return found != font.glyphs.end() && found->codepoint == codepoint ? &*found : nullptr;
}

core::Result<void> validate(const FontData& font)
{
    if (font.atlasWidth == 0 || font.atlasHeight == 0 || font.glyphs.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the font has no glyphs");
    }
    if (font.atlas.size() != static_cast<std::size_t>(font.atlasWidth) * font.atlasHeight)
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "{} pixels for an atlas of {}x{}", font.atlas.size(), font.atlasWidth,
                               font.atlasHeight);
    }
    if (!(font.bakedSize > 0.0f) || !(font.ascent > font.descent))
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "the font was baked at {} pixels, from {} to {}", font.bakedSize,
                               font.descent, font.ascent);
    }
    if (!std::ranges::is_sorted(font.glyphs, {}, &FontGlyph::codepoint))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the glyphs are not in order");
    }
    for (const FontGlyph& glyph : font.glyphs)
    {
        if (static_cast<std::uint32_t>(glyph.atlasX) + glyph.width > font.atlasWidth ||
            static_cast<std::uint32_t>(glyph.atlasY) + glyph.height > font.atlasHeight)
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "the glyph {} lies outside the atlas", glyph.codepoint);
        }
    }
    return {};
}

} // namespace devex::asset
