#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace devex::ui {
namespace {

constexpr std::uint32_t unknownCodepoint = 0xFFFD;

// One thing to place: a letter, or an image asked for between two words. Everything the layout
// needs is already resolved here, which leaves the line breaking to work on advances alone.
struct Item
{
    std::uint32_t codepoint = 0;
    std::size_t offset = 0;
    // Where the pen moves after it, in units, kerning included.
    float advance = 0.0f;
    float size = 0.0f;
    math::Vec4 color{1.0f};
    bool bold = false;
    bool italic = false;
    // The place of the image in the list the text carries, or -1 for a letter.
    int image = -1;
};

// What a span of rich text looks like, while the marks are read.
struct Span
{
    math::Vec4 color{1.0f};
    float size = 0.0f;
    bool bold = false;
    bool italic = false;
};

[[nodiscard]] math::Vec4 parseColor(std::string_view text, math::Vec4 fallback) noexcept
{
    if (!text.empty() && text.front() == '#')
    {
        text.remove_prefix(1);
    }
    if (text.size() != 6 && text.size() != 8)
    {
        return fallback;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return fallback;
    }
    const auto channel = [](std::uint32_t byte) {
        // Colours are written the way they are picked, and kept the way the engine keeps them.
        const float amount = static_cast<float>(byte) / 255.0f;
        return amount <= 0.04045f ? amount / 12.92f : std::pow((amount + 0.055f) / 1.055f, 2.4f);
    };
    const bool hasAlpha = text.size() == 8;
    const std::uint32_t red = (value >> (hasAlpha ? 24 : 16)) & 0xFF;
    const std::uint32_t green = (value >> (hasAlpha ? 16 : 8)) & 0xFF;
    const std::uint32_t blue = (value >> (hasAlpha ? 8 : 0)) & 0xFF;
    const std::uint32_t alpha = hasAlpha ? value & 0xFF : 255;
    return math::Vec4{channel(red), channel(green), channel(blue),
                      static_cast<float>(alpha) / 255.0f};
}

// Reads one mark at `offset`, which starts on a bracket, and returns where the text goes on. An
// unknown mark is left as it is, so that a text that only looks like a mark still reads.
[[nodiscard]] bool readTag(std::string_view text, std::size_t& offset, std::vector<Span>& spans,
                           int& image)
{
    const std::size_t close = text.find(']', offset);
    if (close == std::string_view::npos)
    {
        return false;
    }
    const std::string_view tag = text.substr(offset + 1, close - offset - 1);
    Span span = spans.back();
    if (tag == "b")
    {
        span.bold = true;
    }
    else if (tag == "i")
    {
        span.italic = true;
    }
    else if (tag.starts_with("color="))
    {
        span.color = parseColor(tag.substr(6), span.color);
    }
    else if (tag.starts_with("size="))
    {
        float size = span.size;
        const std::string_view number = tag.substr(5);
        if (std::from_chars(number.data(), number.data() + number.size(), size).ec == std::errc{})
        {
            span.size = std::max(size, 1.0f);
        }
    }
    else if (tag.starts_with("icon="))
    {
        int index = 0;
        const std::string_view number = tag.substr(5);
        if (std::from_chars(number.data(), number.data() + number.size(), index).ec == std::errc{})
        {
            image = std::max(index, 0);
        }
        offset = close + 1;
        return true;
    }
    else if (tag == "/b" || tag == "/i" || tag == "/color" || tag == "/size")
    {
        if (spans.size() > 1)
        {
            spans.pop_back();
        }
        offset = close + 1;
        return true;
    }
    else
    {
        return false;
    }
    spans.push_back(span);
    offset = close + 1;
    return true;
}

// Cuts the text into what the lines will hold, reading the marks of a rich text on the way.
void readItems(const asset::FontData& font, std::string_view text, const TextStyle& style,
               std::vector<Item>& items)
{
    items.clear();
    std::vector<Span> spans{Span{.size = style.size}};
    std::uint32_t previous = 0;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const std::size_t start = offset;
        if (style.rich && text[offset] == '[')
        {
            // Two brackets in a row write one.
            if (offset + 1 < text.size() && text[offset + 1] == '[')
            {
                offset += 1;
            }
            else
            {
                int image = -1;
                if (readTag(text, offset, spans, image))
                {
                    if (image >= 0)
                    {
                        const float size = spans.back().size;
                        items.push_back({.offset = start, .advance = size, .size = size,
                                         .color = spans.back().color, .image = image});
                        previous = 0;
                    }
                    continue;
                }
            }
        }

        const std::uint32_t codepoint = nextCodepoint(text, offset);
        const Span& span = spans.back();
        const float scale = font.bakedSize > 0.0f ? span.size / font.bakedSize : 1.0f;
        const asset::FontGlyph* const glyph = asset::findGlyph(font, codepoint);
        const float kerning =
            previous != 0 ? asset::kerningBetween(font, previous, codepoint) * scale : 0.0f;
        items.push_back({
            .codepoint = codepoint,
            .offset = start,
            .advance = (glyph != nullptr ? glyph->advance * scale : 0.0f) + kerning,
            .size = span.size,
            .color = span.color,
            .bold = span.bold,
            .italic = span.italic,
        });
        previous = codepoint;
    }
}

// One line of the text, as the wrapping cut it.
struct Line
{
    std::size_t begin = 0;
    std::size_t end = 0;
    float width = 0.0f;
    float size = 0.0f;
};

// Cuts the items into lines: at every new line, and at the last space that fits when the text
// wraps. A word longer than the width is cut where it runs out.
void breakLines(std::span<const Item> items, bool wrap, float width, std::vector<Line>& lines)
{
    lines.clear();
    Line line{};
    float lastSpaceWidth = 0.0f;
    std::size_t lastSpace = items.size();
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const Item& item = items[index];
        line.size = std::max(line.size, item.size);
        if (item.codepoint == U'\n')
        {
            line.end = index;
            lines.push_back(line);
            line = Line{.begin = index + 1};
            lastSpace = items.size();
            continue;
        }
        if (item.codepoint == U' ')
        {
            lastSpace = index;
            lastSpaceWidth = line.width;
        }
        else if (wrap && width > 0.0f && line.width + item.advance > width && index > line.begin)
        {
            if (lastSpace != items.size() && lastSpace > line.begin)
            {
                line.end = lastSpace;
                line.width = lastSpaceWidth;
                lines.push_back(line);
                line = Line{.begin = lastSpace + 1};
                index = lastSpace;
                lastSpace = items.size();
                continue;
            }
            line.end = index;
            lines.push_back(line);
            line = Line{.begin = index};
            lastSpace = items.size();
            --index;
            continue;
        }
        line.width += item.advance;
    }
    line.end = items.size();
    if (line.size == 0.0f && !lines.empty())
    {
        line.size = lines.back().size;
    }
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

std::size_t previousOffset(std::string_view text, std::size_t offset) noexcept
{
    if (offset == 0)
    {
        return 0;
    }
    std::size_t back = std::min(offset, text.size()) - 1;
    // The bytes that follow the first one of a character all start with the same two bits.
    while (back > 0 && (static_cast<unsigned char>(text[back]) & 0xC0) == 0x80)
    {
        --back;
    }
    return back;
}

TextStyle fieldStyle(const scene::UiText& text, const scene::UiInput& field) noexcept
{
    return TextStyle{.size = text.size,
                     .align = text.align,
                     .verticalAlign = text.verticalAlign,
                     .wrap = field.multiline && text.wrap,
                     .lineSpacing = text.lineSpacing,
                     .rich = false};
}

void fieldBox(const scene::UiInput& field, math::Vec2 rectMin, math::Vec2 rectMax, math::Vec2& boxMin,
              math::Vec2& boxMax) noexcept
{
    const math::Vec2 padding{std::max(field.padding.x, 0.0f), std::max(field.padding.y, 0.0f)};
    boxMin = math::Vec2{rectMin.x + padding.x, rectMin.y + padding.y};
    // A padding wider than the field leaves it an empty box rather than an inside-out one.
    boxMax = math::Vec2{std::max(rectMax.x - padding.x, boxMin.x),
                        std::max(rectMax.y - padding.y, boxMin.y)};
}

std::size_t characterIndexOf(std::string_view text, std::size_t offset) noexcept
{
    std::size_t index = 0;
    std::size_t walked = 0;
    while (walked < offset && walked < text.size())
    {
        static_cast<void>(nextCodepoint(text, walked));
        ++index;
    }
    return index;
}

std::size_t offsetOfCharacter(std::string_view text, std::size_t index) noexcept
{
    std::size_t offset = 0;
    for (std::size_t walked = 0; walked < index && offset < text.size(); ++walked)
    {
        static_cast<void>(nextCodepoint(text, offset));
    }
    return offset;
}

std::string shownText(std::string_view text, bool password)
{
    if (!password)
    {
        return std::string(text);
    }
    std::string dots;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        static_cast<void>(nextCodepoint(text, offset));
        // U+2022, the dot a password shows.
        dots += "\xe2\x80\xa2";
    }
    return dots;
}

float textSharpness(const asset::FontData& font, float size) noexcept
{
    // A unit of drawn text covers size / bakedSize texels of the atlas, over which the distances
    // run across the spread.
    const float scale = font.bakedSize > 0.0f ? size / font.bakedSize : 1.0f;
    return std::max(font.spread * scale, 0.0001f);
}

const CaretStop* caretAt(const TextLayoutResult& layout, std::size_t offset) noexcept
{
    const auto found = std::ranges::find_if(
        layout.stops, [offset](const CaretStop& stop) { return stop.offset >= offset; });
    if (found != layout.stops.end() && found->offset == offset)
    {
        return &*found;
    }
    // Between two characters, or past the end: the nearest stop before it.
    return layout.stops.empty() ? nullptr
                                : &layout.stops[found == layout.stops.begin()
                                                    ? 0
                                                    : static_cast<std::size_t>(
                                                          found - layout.stops.begin() - 1)];
}

std::size_t offsetAt(const TextLayoutResult& layout, math::Vec2 point) noexcept
{
    const CaretStop* best = nullptr;
    float bestDistance = 0.0f;
    for (const CaretStop& stop : layout.stops)
    {
        // The line is chosen first, then the nearest place in it.
        const float above = stop.position.y - point.y;
        const float below = point.y - (stop.position.y + stop.height);
        const float vertical = std::max({above, below, 0.0f});
        const float distance = vertical * 1000.0f + std::abs(stop.position.x - point.x);
        if (best == nullptr || distance < bestDistance)
        {
            best = &stop;
            bestDistance = distance;
        }
    }
    return best != nullptr ? best->offset : 0;
}

void layoutText(const asset::FontData& font, std::string_view text, const TextStyle& style,
                math::Vec2 boxMin, math::Vec2 boxMax, TextLayoutResult& result)
{
    result.clear();
    if (font.glyphs.empty() || font.bakedSize <= 0.0f)
    {
        return;
    }
    std::vector<Item> items;
    readItems(font, text, style, items);

    const math::Vec2 box{std::max(boxMax.x - boxMin.x, 0.0f), std::max(boxMax.y - boxMin.y, 0.0f)};
    std::vector<Line> lines;
    breakLines(items, style.wrap, box.x, lines);

    // Lines are as tall as the largest letter they hold.
    const float spacing = std::max(style.lineSpacing, 0.0f);
    float height = 0.0f;
    for (Line& line : lines)
    {
        if (line.size <= 0.0f)
        {
            line.size = style.size;
        }
        result.size.x = std::max(result.size.x, line.width);
        height += font.lineHeight() * (line.size / font.bakedSize) * spacing;
    }
    result.size.y = height;
    result.lineCount = lines.size();

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
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const Line& line = lines[lineIndex];
        const float lineScale = line.size / font.bakedSize;
        const float lineHeight = font.lineHeight() * lineScale * spacing;
        float pen = alignedStart(boxMin.x, box.x, line.width, style.align);
        const float baseline = top + font.ascent * lineScale;

        for (std::size_t index = line.begin; index < line.end; ++index)
        {
            const Item& item = items[index];
            result.stops.push_back({.offset = item.offset,
                                    .position = math::Vec2{pen, top},
                                    .height = lineHeight,
                                    .line = static_cast<std::uint32_t>(lineIndex)});
            pen += item.advance;
            if (item.image >= 0)
            {
                // An image sits on the line, as tall as the letters around it.
                result.images.push_back({
                    .index = static_cast<std::uint32_t>(item.image),
                    .min = math::Vec2{pen - item.advance, baseline - item.size * 0.8f},
                    .max = math::Vec2{pen, baseline + item.size * 0.2f},
                });
                continue;
            }
            const asset::FontGlyph* const glyph = asset::findGlyph(font, item.codepoint);
            if (glyph == nullptr || glyph->width == 0 || glyph->height == 0)
            {
                continue;
            }
            const float scale = item.size / font.bakedSize;
            const math::Vec2 min{pen - item.advance + glyph->left * scale,
                                 baseline + glyph->top * scale};
            result.glyphs.push_back({
                .min = min,
                .max = math::Vec2{min.x + static_cast<float>(glyph->width) * scale,
                                  min.y + static_cast<float>(glyph->height) * scale},
                .uvMin = math::Vec2{static_cast<float>(glyph->atlasX) / atlasWidth,
                                    static_cast<float>(glyph->atlasY) / atlasHeight},
                .uvMax = math::Vec2{static_cast<float>(glyph->atlasX + glyph->width) / atlasWidth,
                                    static_cast<float>(glyph->atlasY + glyph->height) / atlasHeight},
                .color = item.color,
                .bold = item.bold,
                .italic = item.italic,
            });
        }
        // The end of the line, where the cursor stands after the last letter.
        result.stops.push_back({.offset = line.end < items.size() ? items[line.end].offset
                                                                  : text.size(),
                                .position = math::Vec2{pen, top},
                                .height = lineHeight,
                                .line = static_cast<std::uint32_t>(lineIndex)});
        top += lineHeight;
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
