#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/FontData.hpp>
#include <devex/math/Math.hpp>
#include <devex/render/RenderWorld.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace devex::render {
class Renderer;
}

// The letters of fonts drawn from their files at the very size they are shown, into pages of the
// renderer, as Godot and Dear ImGui draw theirs with FreeType: sharper than the distances at the small
// sizes of tools, with stems that sit on whole pixels.
namespace devex::ui {

// What the pages of the atlas need from the renderer.
struct DEVEX_API GlyphPages
{
    // An empty texture of one channel, size by size pixels.
    std::function<render::TextureHandle(std::uint32_t size)> create;
    // Replaces whole rows of a page, one byte a pixel, from `top` down.
    std::function<void(render::TextureHandle page, std::uint32_t top, std::uint32_t height, std::span<const std::byte> rows)> update;
    std::function<void(render::TextureHandle page)> destroy;
};

// Pages that are textures of a renderer, which must outlive the atlas: read at their nearest pixel,
// since every letter sits on whole pixels.
[[nodiscard]] DEVEX_API GlyphPages rendererPages(render::Renderer& renderer);

// A letter drawn at a size: the part of its page it covers, and where it goes from the pen.
struct DEVEX_API RasterGlyph
{
    render::TextureHandle page;
    math::Vec2 uvMin{0.0f};
    math::Vec2 uvMax{0.0f};
    // From the pen, on the baseline, to the top left corner of the image, in pixels, Y going down.
    math::Vec2 offset{0.0f};
    math::Vec2 size{0.0f};
};

// Letters drawn by FreeType with light hinting, which only moves them up and down: the advances stay
// those of the layout, and the pen stands at a quarter of a pixel. Pages fill as letters are asked
// for; once there are too many, the atlas starts over at the next flush.
class DEVEX_API GlyphAtlas
{
public:
    static constexpr std::uint32_t pageSize = 1024;
    static constexpr std::size_t maxPages = 8;
    // The fractions of a pixel the pen may stand at, horizontally.
    static constexpr int subpixelSteps = 4;

    explicit GlyphAtlas(GlyphPages pages);
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    // The image of a character of a font at a size in pixels, its pen `subpixel` steps past a whole
    // pixel; null when the font carries no file, lacks the character, or draws nothing for it.
    [[nodiscard]] const RasterGlyph* glyph(const asset::FontData& font, float pixels, std::uint32_t codepoint, int subpixel);
    // Sends the rows the letters drawn since the last call changed, before the frame that shows them.
    void flush();
    // Forgets every letter and every page.
    void clear();

    [[nodiscard]] std::size_t pageCount() const noexcept;
    [[nodiscard]] std::size_t glyphCount() const noexcept;

private:
    struct Implementation;

    std::unique_ptr<Implementation> m_implementation;
};

// Where a pen at a position in pixels draws from: the whole pixel to the left, and the step of a
// quarter of a pixel past it; a pen that rounds up to the next pixel moves to it.
struct DEVEX_API SnappedPen
{
    float whole = 0.0f;
    int subpixel = 0;
};
[[nodiscard]] DEVEX_API SnappedPen snapPen(float x) noexcept;

} // namespace devex::ui
