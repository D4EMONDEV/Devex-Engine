#include <devex/ui/TextCache.hpp>

namespace devex::ui {
namespace {

[[nodiscard]] bool sameStyle(const TextStyle& first, const TextStyle& second) noexcept
{
    return first.size == second.size && first.align == second.align && first.verticalAlign == second.verticalAlign &&
           first.wrap == second.wrap && first.lineSpacing == second.lineSpacing && first.rich == second.rich;
}

} // namespace

const TextLayoutResult& TextCache::layout(scene::Entity element, std::uint32_t part, const asset::FontData& font,
                                          std::string_view text, const TextStyle& style, math::Vec2 box)
{
    // An element has a handful of texts at most.
    Entry& entry = m_entries[(static_cast<std::uint64_t>(element.index) << 3) | (part & 7u)];
    entry.used = true;
    // A font that was imported again is another font, even at the address of the one it replaces.
    const bool kept = entry.font == &font && entry.generation == element.generation && entry.bakedSize == font.bakedSize &&
                      entry.glyphCount == font.glyphs.size() && entry.box == box && sameStyle(entry.style, style) && entry.text == text;
    if (!kept)
    {
        entry.generation = element.generation;
        entry.font = &font;
        entry.bakedSize = font.bakedSize;
        entry.glyphCount = font.glyphs.size();
        entry.text.assign(text);
        entry.style = style;
        entry.box = box;
        layoutText(font, text, style, math::Vec2{0.0f}, box, entry.letters);
        ++m_placed;
    }
    return entry.letters;
}

void TextCache::sweep()
{
    std::erase_if(m_entries, [](auto& entry) {
        const bool used = entry.second.used;
        entry.second.used = false;
        return !used;
    });
}

void TextCache::clear() noexcept
{
    m_entries.clear();
}

std::size_t TextCache::size() const noexcept
{
    return m_entries.size();
}

std::uint64_t TextCache::placed() const noexcept
{
    return m_placed;
}

} // namespace devex::ui
