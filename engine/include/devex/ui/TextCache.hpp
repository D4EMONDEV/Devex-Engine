#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/FontData.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/ui/TextLayout.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace devex::ui {

// The letters of the texts of an interface as they were last placed, kept from a frame to the next:
// placing the letters of a text is the slow part of drawing it, and most texts say the same thing in
// the same room for a long time. A text is kept by the element that shows it.
class DEVEX_API TextCache
{
public:
    // The letters of a text in a box of this size whose corner is the origin, which the drawing moves
    // to where the element stands. They are placed again only when the text, its style, its font or
    // the size of the box changed. An element keeps several texts apart by their `part`: what a field
    // holds, and what it shows while it is empty.
    [[nodiscard]] const TextLayoutResult& layout(scene::Entity element, std::uint32_t part, const asset::FontData& font,
                                                 std::string_view text, const TextStyle& style, math::Vec2 box);

    // Forgets the texts that were not asked for since the last sweep: their elements are gone, or
    // no longer drawn.
    void sweep();
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    // How many times letters were placed since the cache was made, which tells a text kept from one
    // placed again.
    [[nodiscard]] std::uint64_t placed() const noexcept;

private:
    struct Entry
    {
        std::uint32_t generation = 0;
        // What the letters were placed for.
        const asset::FontData* font = nullptr;
        float bakedSize = 0.0f;
        std::size_t glyphCount = 0;
        std::string text;
        TextStyle style;
        math::Vec2 box{0.0f};
        TextLayoutResult letters;
        bool used = false;
    };

    std::unordered_map<std::uint64_t, Entry> m_entries;
    std::uint64_t m_placed = 0;
};

} // namespace devex::ui
