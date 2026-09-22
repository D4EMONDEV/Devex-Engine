#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <cmath>

namespace devex::ui {
namespace {

constexpr std::uint32_t unknownCodepoint = 0xFFFD;

// One line of a paragraph, as the wrapping cut it.
struct Line
{
    std::size_t begin = 0;
    std::size_t end = 0;
    float width = 0.0f;
};

[[nodiscard]] float advanceOf(const asset::FontData& font, std::uint32_t codepoint,
                              float scale) noexcept
{
    const asset::FontGlyph* const glyph = asset::findGlyph(font, codepoint);
    return glyph != nullptr ? glyph->advance * scale : 0.0f;
}

// Cuts the text into the lines it is drawn as: at every new line, and at the last space that fits
// when the text wraps. A word longer than the width is cut where it runs out.
void breakLines(const asset::FontData& font, std::string_view text, float scale, bool wrap,
                float width, std::vector<Line>& lines)
{
    lines.clear();
    Line line{};
    float lastSpaceWidth = 0.0f;
    std::size_t lastSpace = std::string_view::npos;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const std::size_t start = offset;
        const std::uint32_t codepoint = nextCodepoint(text, offset);
        if (codepoint == U'\n')
        {
            line.end = start;
            lines.push_back(line);
            line = Line{.begin = offset};
            lastSpace = std::string_view::npos;
            continue;
        }
        const float advance = advanceOf(font, codepoint, scale);
        if (codepoint == U' ')
        {
            lastSpace = start;
            lastSpaceWidth = line.width;
        }
        else if (wrap && width > 0.0f && line.width + advance > width && start > line.begin)
        {
            if (lastSpace != std::string_view::npos && lastSpace > line.begin)
            {
                // Back to the last space, which the new line starts after.
                line.end = lastSpace;
                line.width = lastSpaceWidth;
                lines.push_back(line);
                std::size_t next = lastSpace;
                static_cast<void>(nextCodepoint(text, next));
                line = Line{.begin = next};
                offset = next;
                lastSpace = std::string_view::npos;
                continue;
            }
            // A word on its own, longer than the line: it is cut where it runs out.
            line.end = start;
            lines.push_back(line);
            line = Line{.begin = start};
            offset = start;
            lastSpace = std::string_view::npos;
            continue;
        }
        line.width += advance;
    }
    line.end = text.size();
    lines.push_back(line);
}

[[nodiscard]] float alignedStart(float boxMin, float boxWidth, float lineWidth,
                                 scene::TextAlign align) noexcept
{
    switch (align)
    {
    case scene::TextAlign::Center:
        return boxMin + (boxWidth - lineWidth) * 0.5f;
    case scene::TextAlign::Right:
        return boxMin + boxWidth - lineWidth;
    case scene::TextAlign::Left:
        break;
    }
    return boxMin;
}

} // namespace

std::uint32_t nextCodepoint(std::string_view text, std::size_t& offset) noexcept
{
    if (offset >= text.size())
    {
        offset = text.size();
        return 0;
    }
    const auto first = static_cast<unsigned char>(text[offset]);
    ++offset;
    if (first < 0x80)
    {
        return first;
    }

    // How many bytes follow the first one, and what the first one contributes.
    int following = 0;
    std::uint32_t codepoint = 0;
    if ((first & 0xE0) == 0xC0)
    {
        following = 1;
        codepoint = first & 0x1Fu;
    }
    else if ((first & 0xF0) == 0xE0)
    {
        following = 2;
        codepoint = first & 0x0Fu;
    }
    else if ((first & 0xF8) == 0xF0)
    {
        following = 3;
        codepoint = first & 0x07u;
    }
    else
    {
        return unknownCodepoint;
    }

    for (int index = 0; index < following; ++index)
    {
        if (offset >= text.size() || (static_cast<unsigned char>(text[offset]) & 0xC0) != 0x80)
        {
            return unknownCodepoint;
        }
        codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[offset]) & 0x3Fu);
        ++offset;
    }
    return codepoint;
}

float textSharpness(const asset::FontData& font, float size) noexcept
{
    // A unit of drawn text covers size / bakedSize texels of the atlas, over which the distances
    // run across the spread.
    const float scale = font.bakedSize > 0.0f ? size / font.bakedSize : 1.0f;
    return std::max(font.spread * scale, 0.0001f);
}

void layoutText(const asset::FontData& font, std::string_view text, const TextStyle& style,
                math::Vec2 boxMin, math::Vec2 boxMax, TextLayoutResult& result)
{
    result.clear();
    if (font.glyphs.empty() || font.bakedSize <= 0.0f)
    {
        return;
    }
    const float scale = style.size / font.bakedSize;
    const float ascent = font.ascent * scale;
    const float lineHeight = font.lineHeight() * scale * std::max(style.lineSpacing, 0.0f);
    const math::Vec2 box{std::max(boxMax.x - boxMin.x, 0.0f), std::max(boxMax.y - boxMin.y, 0.0f)};

    std::vector<Line> lines;
    breakLines(font, text, scale, style.wrap, box.x, lines);
    result.lineCount = lines.size();
    for (const Line& line : lines)
    {
        result.size.x = std::max(result.size.x, line.width);
    }
    result.size.y = lineHeight * static_cast<float>(lines.size());

    float top = boxMin.y;
    switch (style.verticalAlign)
    {
    case scene::TextVerticalAlign::Middle:
        top += (box.y - result.size.y) * 0.5f;
        break;
    case scene::TextVerticalAlign::Bottom:
        top += box.y - result.size.y;
        break;
    case scene::TextVerticalAlign::Top:
        break;
    }

    const auto atlasWidth = static_cast<float>(std::max(font.atlasWidth, 1u));
    const auto atlasHeight = static_cast<float>(std::max(font.atlasHeight, 1u));
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const Line& line = lines[index];
        float pen = alignedStart(boxMin.x, box.x, line.width, style.align);
        const float baseline = top + lineHeight * static_cast<float>(index) + ascent;
        std::size_t offset = line.begin;
        while (offset < line.end)
        {
            const std::uint32_t codepoint = nextCodepoint(text, offset);
            const asset::FontGlyph* const glyph = asset::findGlyph(font, codepoint);
            if (glyph == nullptr)
            {
                continue;
            }
            if (glyph->width > 0 && glyph->height > 0)
            {
                const math::Vec2 min{pen + glyph->left * scale, baseline + glyph->top * scale};
                result.glyphs.push_back({
                    .min = min,
                    .max = math::Vec2{min.x + static_cast<float>(glyph->width) * scale,
                                      min.y + static_cast<float>(glyph->height) * scale},
                    .uvMin = math::Vec2{static_cast<float>(glyph->atlasX) / atlasWidth,
                                        static_cast<float>(glyph->atlasY) / atlasHeight},
                    .uvMax = math::Vec2{
                        static_cast<float>(glyph->atlasX + glyph->width) / atlasWidth,
                        static_cast<float>(glyph->atlasY + glyph->height) / atlasHeight},
                });
            }
            pen += glyph->advance * scale;
        }
    }
}

math::Vec2 measureText(const asset::FontData& font, std::string_view text, const TextStyle& style,
                       float wrapWidth)
{
    TextLayoutResult result;
    layoutText(font, text, style, math::Vec2{0.0f, 0.0f}, math::Vec2{wrapWidth, 0.0f}, result);
    return result.size;
}

} // namespace devex::ui
