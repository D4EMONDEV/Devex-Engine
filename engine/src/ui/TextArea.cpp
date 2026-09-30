#include <devex/ui/TextArea.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace devex::ui {
namespace {

// What a byte is to a cursor that moves by words.
enum class ByteKind : std::uint8_t
{
    Space,
    Word,
    Other,
    Line,
};

[[nodiscard]] ByteKind kindOf(char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    if (byte == '\n')
    {
        return ByteKind::Line;
    }
    if (byte == ' ' || byte == '\t')
    {
        return ByteKind::Space;
    }
    // The bytes of a character beyond ASCII count as letters, which keeps them together.
    const bool letter = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_' || byte >= 0x80;
    return letter ? ByteKind::Word : ByteKind::Other;
}

} // namespace

void indexLines(std::string_view text, std::vector<std::uint32_t>& starts)
{
    starts.clear();
    starts.push_back(0);
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    for (const char* at = begin; at < end;)
    {
        const void* const found = std::memchr(at, '\n', static_cast<std::size_t>(end - at));
        if (found == nullptr)
        {
            break;
        }
        at = static_cast<const char*>(found) + 1;
        starts.push_back(static_cast<std::uint32_t>(at - begin));
    }
}

std::size_t lineOfOffset(std::span<const std::uint32_t> starts, std::size_t offset) noexcept
{
    if (starts.empty())
    {
        return 0;
    }
    const auto next = std::upper_bound(starts.begin(), starts.end(), offset,
                                       [](std::size_t value, std::uint32_t start) { return value < start; });
    return next == starts.begin() ? 0 : static_cast<std::size_t>(next - starts.begin()) - 1;
}

std::string_view lineText(std::string_view text, std::span<const std::uint32_t> starts, std::size_t line) noexcept
{
    if (line >= starts.size())
    {
        return {};
    }
    const std::size_t begin = std::min<std::size_t>(starts[line], text.size());
    // Without the new line that ends it.
    const std::size_t end = line + 1 < starts.size() ? std::min<std::size_t>(starts[line + 1] - 1, text.size()) : text.size();
    return text.substr(begin, end > begin ? end - begin : 0);
}

TextAreaMetrics textAreaMetrics(const asset::FontData& font, const scene::UiText& text, const scene::UiTextArea& area, const LaidOutRect& rect,
                                std::size_t lineCount)
{
    TextAreaMetrics metrics;
    const float scale = font.bakedSize > 0.0f ? text.size / font.bakedSize : 1.0f;
    metrics.lineHeight = std::max(font.lineHeight() * scale * std::max(text.lineSpacing, 0.1f), 1.0f);
    if (area.lineNumbers)
    {
        // As wide as the number of the last line, with a little room each side.
        std::size_t digits = 1;
        for (std::size_t count = std::max<std::size_t>(lineCount, 1); count >= 10; count /= 10)
        {
            ++digits;
        }
        const asset::FontGlyph* const zero = asset::findGlyph(font, U'0');
        const float digit = zero != nullptr ? zero->advance * scale : text.size * 0.6f;
        metrics.gutter = std::ceil(digit * (static_cast<float>(digits) + 2.0f));
    }
    const math::Vec2 padding{std::max(area.padding.x, 0.0f), std::max(area.padding.y, 0.0f)};
    const float bar = std::max(area.scrollbarSize, 0.0f);
    metrics.viewMin = math::Vec2{std::min(rect.min.x + metrics.gutter + padding.x, rect.max.x), std::min(rect.min.y + padding.y, rect.max.y)};
    metrics.viewMax = math::Vec2{std::max(rect.max.x - bar, metrics.viewMin.x), std::max(rect.max.y - padding.y, metrics.viewMin.y)};
    metrics.origin = metrics.viewMin;
    metrics.contentHeight = metrics.lineHeight * static_cast<float>(std::max<std::size_t>(lineCount, 1));
    return metrics;
}

TextAreaBars textAreaBars(const TextAreaMetrics& metrics, const scene::UiText& text, const scene::UiTextArea& area, const LaidOutRect& rect,
                          float contentWidth) noexcept
{
    TextAreaBars bars;
    const math::Vec2 view = metrics.viewSize();
    // A little room after the longest line, so that the cursor shows at its end.
    const float width = contentWidth + text.size;
    bars.maxScroll = math::Vec2{std::max(width - view.x, 0.0f), std::max(metrics.contentHeight - view.y, 0.0f)};
    const float bar = std::max(area.scrollbarSize, 0.0f);
    if (bar <= 0.0f)
    {
        return bars;
    }
    const math::Vec2 scroll{std::clamp(area.scroll.x, 0.0f, bars.maxScroll.x), std::clamp(area.scroll.y, 0.0f, bars.maxScroll.y)};
    if (bars.maxScroll.y > 0.5f)
    {
        const float track = rect.size().y;
        const float thumb = std::clamp(track * view.y / std::max(metrics.contentHeight, 1.0f), std::min(bar * 2.0f, track), track);
        bars.travel.y = track - thumb;
        const float top = rect.min.y + bars.travel.y * scroll.y / bars.maxScroll.y;
        bars.verticalMin = math::Vec2{rect.max.x - bar, top};
        bars.verticalMax = math::Vec2{rect.max.x - 1.0f, top + thumb};
    }
    if (bars.maxScroll.x > 0.5f)
    {
        const float left = rect.min.x + metrics.gutter;
        const float track = std::max(rect.max.x - bar - left, 0.0f);
        const float thumb = std::clamp(track * view.x / std::max(width, 1.0f), std::min(bar * 2.0f, track), track);
        bars.travel.x = track - thumb;
        const float start = left + bars.travel.x * scroll.x / bars.maxScroll.x;
        bars.horizontalMin = math::Vec2{start, rect.max.y - bar};
        bars.horizontalMax = math::Vec2{start + thumb, rect.max.y - 1.0f};
    }
    return bars;
}

void layoutAreaLine(const asset::FontData& font, std::string_view line, float size, std::int32_t tabSize, math::Vec2 origin, TextAreaLine& placed)
{
    placed.shown.clear();
    placed.sources.clear();
    placed.length = line.size();
    placed.left = origin.x;
    std::string_view laid = line;
    if (tabSize > 0 && line.find('\t') != std::string_view::npos)
    {
        for (std::size_t index = 0; index < line.size(); ++index)
        {
            const std::size_t count = line[index] == '\t' ? static_cast<std::size_t>(tabSize) : 1;
            placed.shown.append(count, line[index] == '\t' ? ' ' : line[index]);
            placed.sources.insert(placed.sources.end(), count, static_cast<std::uint32_t>(index));
        }
        laid = placed.shown;
    }
    layoutText(font, laid, TextStyle{.size = size, .wrap = false}, origin, origin, placed.layout);
}

float TextAreaLine::xOf(std::size_t offset) const noexcept
{
    if (layout.stops.empty())
    {
        return left;
    }
    std::size_t shownOffset = std::min(offset, length);
    if (!sources.empty())
    {
        // The first byte placed that comes from that byte of the line, or the end of what is placed.
        const auto found = std::lower_bound(sources.begin(), sources.end(), static_cast<std::uint32_t>(shownOffset));
        shownOffset = static_cast<std::size_t>(found - sources.begin());
    }
    const CaretStop* const stop = caretAt(layout, shownOffset);
    return stop != nullptr ? stop->position.x : left;
}

std::size_t TextAreaLine::sourceOf(std::size_t shownOffset) const noexcept
{
    if (sources.empty())
    {
        return std::min(shownOffset, length);
    }
    return shownOffset < sources.size() ? sources[shownOffset] : length;
}

std::size_t TextAreaLine::offsetAt(float x) const noexcept
{
    const CaretStop* best = nullptr;
    float bestDistance = 0.0f;
    for (const CaretStop& stop : layout.stops)
    {
        const float distance = std::abs(stop.position.x - x);
        if (best == nullptr || distance < bestDistance)
        {
            best = &stop;
            bestDistance = distance;
        }
    }
    if (best == nullptr)
    {
        return 0;
    }
    // A tab is one byte however wide it is drawn: the cursor lands before it or after it.
    if (!sources.empty() && best->offset < sources.size())
    {
        const std::uint32_t source = sources[best->offset];
        const auto first = std::lower_bound(sources.begin(), sources.end(), source);
        const auto last = std::upper_bound(sources.begin(), sources.end(), source);
        const auto at = sources.begin() + static_cast<std::ptrdiff_t>(best->offset);
        return (at - first) * 2 > (last - first) ? std::min<std::size_t>(source + 1, length) : source;
    }
    return sourceOf(best->offset);
}

std::pair<std::size_t, std::size_t> wordAround(std::string_view text, std::size_t offset) noexcept
{
    std::size_t at = std::min(offset, text.size());
    // Past the last letter of a word, the word is still the one meant.
    if ((at == text.size() || kindOf(text[at]) != ByteKind::Word) && at > 0 && kindOf(text[at - 1]) == ByteKind::Word)
    {
        --at;
    }
    if (at >= text.size() || kindOf(text[at]) == ByteKind::Line)
    {
        return {at, at};
    }
    const ByteKind kind = kindOf(text[at]);
    std::size_t begin = at;
    std::size_t end = at + 1;
    if (kind == ByteKind::Other)
    {
        return {begin, end};
    }
    while (begin > 0 && kindOf(text[begin - 1]) == kind)
    {
        --begin;
    }
    while (end < text.size() && kindOf(text[end]) == kind)
    {
        ++end;
    }
    return {begin, end};
}

std::size_t previousWord(std::string_view text, std::size_t offset) noexcept
{
    std::size_t at = std::min(offset, text.size());
    if (at == 0)
    {
        return 0;
    }
    if (kindOf(text[at - 1]) == ByteKind::Line)
    {
        return at - 1;
    }
    while (at > 0 && kindOf(text[at - 1]) == ByteKind::Space)
    {
        --at;
    }
    if (at == 0 || kindOf(text[at - 1]) == ByteKind::Line)
    {
        return at;
    }
    const ByteKind kind = kindOf(text[at - 1]);
    while (at > 0 && kindOf(text[at - 1]) == kind)
    {
        --at;
    }
    return at;
}

std::size_t nextWord(std::string_view text, std::size_t offset) noexcept
{
    std::size_t at = std::min(offset, text.size());
    if (at >= text.size())
    {
        return text.size();
    }
    if (kindOf(text[at]) == ByteKind::Line)
    {
        return at + 1;
    }
    const ByteKind kind = kindOf(text[at]);
    if (kind != ByteKind::Space)
    {
        while (at < text.size() && kindOf(text[at]) == kind)
        {
            ++at;
        }
    }
    while (at < text.size() && kindOf(text[at]) == ByteKind::Space)
    {
        ++at;
    }
    return at;
}

} // namespace devex::ui
