#include <devex/ui/GlyphAtlas.hpp>

#include <devex/core/Log.hpp>
#include <devex/render/Renderer.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace devex::ui {
namespace {

// Pixels left between two letters, so that sampling one never reads its neighbour.
constexpr std::uint32_t padding = 1;

// A letter at a size and a step of the pen, in a face.
struct GlyphKey
{
    std::size_t face = 0;
    // The size in quarters of a pixel.
    std::uint32_t quarters = 0;
    std::uint32_t codepoint = 0;
    int subpixel = 0;

    bool operator==(const GlyphKey&) const = default;
};

struct GlyphKeyHash
{
    std::size_t operator()(const GlyphKey& key) const noexcept
    {
        std::size_t hash = key.face;
        hash = hash * 1000003u ^ key.quarters;
        hash = hash * 1000003u ^ key.codepoint;
        hash = hash * 1000003u ^ static_cast<std::size_t>(key.subpixel);
        return hash;
    }
};

// A row of letters of about the same height across a page.
struct Shelf
{
    std::uint32_t top = 0;
    std::uint32_t height = 0;
    std::uint32_t used = 0;
};

struct Page
{
    render::TextureHandle texture;
    std::vector<std::byte> pixels;
    std::vector<Shelf> shelves;
    std::uint32_t nextTop = 0;
    // The rows changed since the last flush; top above bottom when none.
    std::uint32_t dirtyTop = GlyphAtlas::pageSize;
    std::uint32_t dirtyBottom = 0;
};

} // namespace

struct GlyphAtlas::Implementation
{
    GlyphPages pages;
    FT_Library library = nullptr;
    // A face per file, which keeps its own copy of the bytes it reads.
    struct Face
    {
        const void* source = nullptr;
        std::size_t bytes = 0;
        std::vector<std::uint8_t> file;
        FT_Face face = nullptr;
    };
    std::vector<Face> faces;
    std::vector<Page> atlas;
    // Letters that draw nothing keep no entry in a page but are remembered as empty.
    std::unordered_map<GlyphKey, std::optional<RasterGlyph>, GlyphKeyHash> glyphs;
    // Past the most pages, set at a flush: the frame it ends still shows the letters it drew, and
    // the next one draws them again into fresh pages.
    bool full = false;
    bool startOver = false;

    ~Implementation()
    {
        for (Face& face : faces)
        {
            FT_Done_Face(face.face);
        }
        if (library != nullptr)
        {
            FT_Done_FreeType(library);
        }
    }

    [[nodiscard]] std::optional<std::size_t> faceOf(const asset::FontData& font)
    {
        if (font.source.empty())
        {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < faces.size(); ++index)
        {
            if (faces[index].source == font.source.data() && faces[index].bytes == font.source.size())
            {
                return faces[index].face != nullptr ? std::optional(index) : std::nullopt;
            }
        }
        if (library == nullptr && FT_Init_FreeType(&library) != 0)
        {
            library = nullptr;
            return std::nullopt;
        }
        Face& added = faces.emplace_back(Face{.source = font.source.data(), .bytes = font.source.size(), .file = font.source});
        if (FT_New_Memory_Face(library, added.file.data(), static_cast<FT_Long>(added.file.size()), 0, &added.face) != 0)
        {
            DEVEX_LOG_WARNING("FreeType cannot read the font {}: its letters keep their distances", font.family);
            added.face = nullptr;
            return std::nullopt;
        }
        return faces.size() - 1;
    }

    // Room for an image of a size in a page, a new page when none has it.
    [[nodiscard]] std::optional<std::pair<std::size_t, math::Vec2>> place(std::uint32_t width, std::uint32_t height)
    {
        const std::uint32_t paddedWidth = width + padding;
        const std::uint32_t paddedHeight = height + padding;
        if (paddedWidth > pageSize || paddedHeight > pageSize)
        {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < atlas.size(); ++index)
        {
            Page& page = atlas[index];
            // A shelf no taller than a third more than the letter, which wastes little.
            for (Shelf& shelf : page.shelves)
            {
                if (shelf.height >= paddedHeight && shelf.height <= paddedHeight + paddedHeight / 3 + 2 &&
                    shelf.used + paddedWidth <= pageSize)
                {
                    const math::Vec2 at{static_cast<float>(shelf.used), static_cast<float>(shelf.top)};
                    shelf.used += paddedWidth;
                    return std::pair{index, at};
                }
            }
            if (page.nextTop + paddedHeight <= pageSize)
            {
                page.shelves.push_back({.top = page.nextTop, .height = paddedHeight, .used = paddedWidth});
                const math::Vec2 at{0.0f, static_cast<float>(page.nextTop)};
                page.nextTop += paddedHeight;
                return std::pair{index, at};
            }
        }
        // A new page; past the most there may be, the atlas starts over after the next flush.
        if (atlas.size() >= maxPages)
        {
            full = true;
        }
        const render::TextureHandle texture = pages.create ? pages.create(pageSize) : render::TextureHandle{};
        Page& page = atlas.emplace_back(Page{.texture = texture, .pixels = std::vector<std::byte>(std::size_t{pageSize} * pageSize)});
        page.shelves.push_back({.top = 0, .height = paddedHeight, .used = paddedWidth});
        page.nextTop = paddedHeight;
        return std::pair{atlas.size() - 1, math::Vec2{0.0f}};
    }

    [[nodiscard]] std::optional<RasterGlyph> draw(std::size_t faceIndex, float pixels, std::uint32_t codepoint, int subpixel)
    {
        const FT_Face face = faces[faceIndex].face;
        const FT_UInt index = FT_Get_Char_Index(face, codepoint);
        if (index == 0 || FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(std::lround(pixels * 64.0f)), 72, 72) != 0 ||
            FT_Load_Glyph(face, index, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) != 0)
        {
            return std::nullopt;
        }
        FT_GlyphSlot slot = face->glyph;
        if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
        {
            // The pen a fraction of a pixel to the right moves the outline as much.
            FT_Outline_Translate(&slot->outline, static_cast<FT_Pos>(subpixel * 64 / subpixelSteps), 0);
        }
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_LIGHT) != 0 || slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
        {
            return std::nullopt;
        }
        const FT_Bitmap& bitmap = slot->bitmap;
        if (bitmap.width == 0 || bitmap.rows == 0)
        {
            return std::nullopt;
        }
        const std::optional<std::pair<std::size_t, math::Vec2>> placed = place(bitmap.width, bitmap.rows);
        if (!placed)
        {
            return std::nullopt;
        }
        Page& page = atlas[placed->first];
        const auto x = static_cast<std::uint32_t>(placed->second.x);
        const auto y = static_cast<std::uint32_t>(placed->second.y);
        for (std::uint32_t row = 0; row < bitmap.rows; ++row)
        {
            const unsigned char* const from = bitmap.buffer + static_cast<std::ptrdiff_t>(row) * bitmap.pitch;
            std::memcpy(page.pixels.data() + (std::size_t{y + row} * pageSize + x), from, bitmap.width);
        }
        page.dirtyTop = std::min(page.dirtyTop, y);
        page.dirtyBottom = std::max(page.dirtyBottom, y + bitmap.rows);
        const float size = static_cast<float>(pageSize);
        return RasterGlyph{
            .page = page.texture,
            .uvMin = math::Vec2{static_cast<float>(x) / size, static_cast<float>(y) / size},
            .uvMax = math::Vec2{static_cast<float>(x + bitmap.width) / size, static_cast<float>(y + bitmap.rows) / size},
            .offset = math::Vec2{static_cast<float>(slot->bitmap_left), -static_cast<float>(slot->bitmap_top)},
            .size = math::Vec2{static_cast<float>(bitmap.width), static_cast<float>(bitmap.rows)},
        };
    }
};

GlyphAtlas::GlyphAtlas(GlyphPages pages)
    : m_implementation(std::make_unique<Implementation>())
{
    m_implementation->pages = std::move(pages);
}

GlyphAtlas::~GlyphAtlas()
{
    clear();
}

const RasterGlyph* GlyphAtlas::glyph(const asset::FontData& font, float pixels, std::uint32_t codepoint, int subpixel)
{
    Implementation& atlas = *m_implementation;
    if (atlas.startOver)
    {
        clear();
    }
    // Sizes are told apart to a quarter of a pixel; very large letters keep their distances.
    const auto quarters = static_cast<std::uint32_t>(std::lround(std::clamp(pixels, 1.0f, 256.0f) * 4.0f));
    if (pixels > 256.0f)
    {
        return nullptr;
    }
    const std::optional<std::size_t> face = atlas.faceOf(font);
    if (!face)
    {
        return nullptr;
    }
    const GlyphKey key{.face = *face, .quarters = quarters, .codepoint = codepoint, .subpixel = std::clamp(subpixel, 0, subpixelSteps - 1)};
    auto found = atlas.glyphs.find(key);
    if (found == atlas.glyphs.end())
    {
        found = atlas.glyphs.emplace(key, atlas.draw(*face, static_cast<float>(quarters) / 4.0f, codepoint, key.subpixel)).first;
    }
    return found->second ? &*found->second : nullptr;
}

void GlyphAtlas::flush()
{
    Implementation& atlas = *m_implementation;
    // Too many pages: the letters are drawn again from the next frame on, into fresh ones.
    atlas.startOver = atlas.full;
    for (Page& page : atlas.atlas)
    {
        if (page.dirtyTop < page.dirtyBottom && atlas.pages.update && page.texture.isValid())
        {
            const std::size_t first = std::size_t{page.dirtyTop} * pageSize;
            const std::size_t count = std::size_t{page.dirtyBottom - page.dirtyTop} * pageSize;
            atlas.pages.update(page.texture, page.dirtyTop, page.dirtyBottom - page.dirtyTop,
                               std::span<const std::byte>(page.pixels).subspan(first, count));
        }
        page.dirtyTop = pageSize;
        page.dirtyBottom = 0;
    }
}

void GlyphAtlas::clear()
{
    Implementation& atlas = *m_implementation;
    for (const Page& page : atlas.atlas)
    {
        if (atlas.pages.destroy && page.texture.isValid())
        {
            atlas.pages.destroy(page.texture);
        }
    }
    atlas.atlas.clear();
    atlas.glyphs.clear();
    atlas.full = false;
    atlas.startOver = false;
}

std::size_t GlyphAtlas::pageCount() const noexcept
{
    return m_implementation->atlas.size();
}

std::size_t GlyphAtlas::glyphCount() const noexcept
{
    return m_implementation->glyphs.size();
}

GlyphPages rendererPages(render::Renderer& renderer)
{
    return GlyphPages{
        .create =
            [&renderer](std::uint32_t size) {
                asset::TextureData image{.format = asset::TextureFormat::R8Unorm, .filter = asset::TextureFilter::Nearest};
                image.mips.push_back({.width = size, .height = size, .bytes = std::vector<std::byte>(std::size_t{size} * size)});
                const core::Result<render::TextureHandle> page = renderer.createTexture(image);
                if (!page)
                {
                    DEVEX_LOG_WARNING("A page of letters cannot be made: {}", page.error());
                }
                return page ? *page : render::TextureHandle{};
            },
        .update =
            [&renderer](render::TextureHandle page, std::uint32_t top, std::uint32_t height, std::span<const std::byte> rows) {
                if (core::Result<void> updated = renderer.updateTexture(page, top, height, rows); !updated)
                {
                    DEVEX_LOG_WARNING("A page of letters cannot be updated: {}", updated.error());
                }
            },
        .destroy = [&renderer](render::TextureHandle page) { renderer.destroyTexture(page); },
    };
}

SnappedPen snapPen(float x) noexcept
{
    const float whole = std::floor(x);
    const auto steps = static_cast<int>(std::lround((x - whole) * static_cast<float>(GlyphAtlas::subpixelSteps)));
    return steps >= GlyphAtlas::subpixelSteps ? SnappedPen{.whole = whole + 1.0f, .subpixel = 0} : SnappedPen{.whole = whole, .subpixel = steps};
}

} // namespace devex::ui
