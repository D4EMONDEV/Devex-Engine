#include <devex/asset/Artifact.hpp>
#include <devex/asset/TranslationData.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace devex::asset {
namespace {

// The characters every font is baked with: the Latin letters, digits and punctuation, the
// accented letters of western and central Europe with the œ of French, the punctuation of running
// text (dashes, curly quotes, the ellipsis, and the dot a password shows) and the euro sign. The
// characters of the translations of the project are baked with them: other writings come so.
constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 5> bakedRanges{{
    {0x0020, 0x00FF},
    {0x0100, 0x017F},
    {0x2010, 0x2027},
    {0x2030, 0x203A},
    {0x20AC, 0x20AC},
}};

// Where glyphs are placed in the atlas: rows filled from left to right, each as tall as its
// tallest glyph. Simple, and tight enough for letters of about the same size.
class ShelfPacker
{
public:
    explicit ShelfPacker(std::uint32_t width) noexcept
        : m_width(width)
    {
    }

    // The corner where a glyph of this size fits, growing the atlas downwards.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> place(std::uint32_t width, std::uint32_t height)
    {
        if (m_x + width > m_width)
        {
            m_x = 0;
            m_y += m_rowHeight;
            m_rowHeight = 0;
        }
        const std::pair<std::uint32_t, std::uint32_t> corner{m_x, m_y};
        m_x += width;
        m_rowHeight = std::max(m_rowHeight, height);
        return corner;
    }

    [[nodiscard]] std::uint32_t height() const noexcept
    {
        return m_y + m_rowHeight;
    }

private:
    std::uint32_t m_width = 0;
    std::uint32_t m_x = 0;
    std::uint32_t m_y = 0;
    std::uint32_t m_rowHeight = 0;
};

// One glyph, baked before it is placed in the atlas.
struct BakedGlyph
{
    std::uint32_t codepoint = 0;
    // In the font, which finds the kerning of two glyphs without looking their characters up.
    int index = 0;
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    int offsetX = 0;
    int offsetY = 0;
    float advance = 0.0f;
};

} // namespace

core::Result<ImportResult> importFontFile(ImportContext& context)
{
    const core::Result<std::vector<std::byte>> file = core::readBinaryFile(context.source);
    if (!file)
    {
        return std::unexpected(file.error());
    }
    // The characters the translations write, whatever their writing: a table that cannot be read
    // brings none, and says why when it imports itself.
    ImportResult result;
    std::vector<std::uint32_t> characters;
    for (const std::filesystem::path& table : context.translations)
    {
        result.dependencies.push_back(table);
        const core::Result<std::string> text = core::readTextFile(table);
        const core::Result<TranslationData> translation =
            text ? parseTranslationCsv(*text) : core::Result<TranslationData>(std::unexpected(text.error()));
        if (translation)
        {
            const std::vector<std::uint32_t> written = translationCharacters(*translation);
            characters.insert(characters.end(), written.begin(), written.end());
        }
    }
    core::Result<FontData> font = bakeFont(*file, core::toUtf8(context.source.stem()),
                                           static_cast<float>(context.numberOption("size", 48.0)),
                                           static_cast<float>(context.numberOption("spread", 6.0)), characters);
    if (!font)
    {
        return std::unexpected(font.error());
    }
    result.artifacts.push_back({context.mainId, AssetType::Font, context.name, encodeFont(*font)});
    return result;
}

core::Result<FontData> bakeFont(std::span<const std::byte> file, std::string family, float wantedSize, float wantedSpread,
                               std::span<const std::uint32_t> characters)
{
    const auto* const bytes = reinterpret_cast<const unsigned char*>(file.data());

    // stb_truetype trusts the offsets it reads, so the file is recognised as a font before it is
    // opened: anything else would be walked as if it were one.
    stbtt_fontinfo info{};
    const int offset = file.size() >= 12 ? stbtt_GetFontOffsetForIndex(bytes, 0) : -1;
    if (offset < 0 || stbtt_InitFont(&info, bytes, offset) == 0)
    {
        return core::makeError(core::ErrorCode::Parse, "the file is not a TrueType font");
    }

    const float size = std::clamp(wantedSize, 8.0f, 128.0f);
    const float spread = std::clamp(wantedSpread, 1.0f, 32.0f);
    // The size is the em of the font, as it is in a style sheet or in a drawing tool, so that the
    // same number gives the same letters as elsewhere.
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, size);
    const auto padding = static_cast<int>(std::ceil(spread));

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    // Every glyph is baked as its distance to the outline, which stays sharp at any size.
    std::vector<std::uint32_t> codepoints;
    for (const auto& [first, last] : bakedRanges)
    {
        for (std::uint32_t codepoint = first; codepoint <= last; ++codepoint)
        {
            codepoints.push_back(codepoint);
        }
    }
    codepoints.insert(codepoints.end(), characters.begin(), characters.end());
    std::ranges::sort(codepoints);
    const auto [repeated, end] = std::ranges::unique(codepoints);
    codepoints.erase(repeated, end);
    std::vector<BakedGlyph> baked;
    for (const std::uint32_t codepoint : codepoints)
    {
        const int index = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
        if (index == 0)
        {
            continue;
        }
        int advance = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&info, index, &advance, &bearing);

        BakedGlyph glyph{.codepoint = codepoint, .index = index, .advance = static_cast<float>(advance) * scale};
        unsigned char* const pixels =
            stbtt_GetGlyphSDF(&info, scale, index, padding, 128, 128.0f / spread, &glyph.width,
                              &glyph.height, &glyph.offsetX, &glyph.offsetY);
        if (pixels != nullptr)
        {
            glyph.pixels.assign(pixels, pixels + static_cast<std::size_t>(glyph.width) * glyph.height);
            stbtt_FreeSDF(pixels, nullptr);
        }
        else
        {
            // A space has no image, only an advance.
            glyph.width = 0;
            glyph.height = 0;
        }
        baked.push_back(std::move(glyph));
    }
    if (baked.empty())
    {
        return core::makeError(core::ErrorCode::Parse, "the font holds no usable glyph");
    }

    // Wider atlases waste less room; 1024 pixels hold the whole Latin range at a usual size, and the
    // thousands of characters of Chinese or Japanese ask for a wider one, kept about square.
    std::size_t area = 0;
    for (const BakedGlyph& glyph : baked)
    {
        area += static_cast<std::size_t>(glyph.width + 1) * static_cast<std::size_t>(glyph.height + 1);
    }
    std::uint32_t atlasWidth = 1024;
    while (atlasWidth < 8192 && area * 5 / 4 > static_cast<std::size_t>(atlasWidth) * atlasWidth)
    {
        atlasWidth *= 2;
    }
    ShelfPacker packer(atlasWidth);
    FontData font;
    font.family = std::move(family);
    font.bakedSize = size;
    font.spread = spread;
    font.ascent = static_cast<float>(ascent) * scale;
    font.descent = static_cast<float>(descent) * scale;
    font.lineGap = static_cast<float>(lineGap) * scale;
    font.atlasWidth = atlasWidth;

    std::vector<std::pair<std::uint32_t, std::uint32_t>> corners;
    corners.reserve(baked.size());
    for (const BakedGlyph& glyph : baked)
    {
        corners.push_back(glyph.width > 0 ? packer.place(static_cast<std::uint32_t>(glyph.width) + 1,
                                                         static_cast<std::uint32_t>(glyph.height) + 1)
                                          : std::pair<std::uint32_t, std::uint32_t>{0, 0});
    }
    font.atlasHeight = std::max(packer.height(), 1u);
    font.atlas.assign(static_cast<std::size_t>(font.atlasWidth) * font.atlasHeight, 0);

    for (std::size_t index = 0; index < baked.size(); ++index)
    {
        const BakedGlyph& glyph = baked[index];
        const auto [x, y] = corners[index];
        for (int row = 0; row < glyph.height; ++row)
        {
            const std::size_t destination = (static_cast<std::size_t>(y) + row) * font.atlasWidth + x;
            std::memcpy(font.atlas.data() + destination,
                        glyph.pixels.data() + static_cast<std::size_t>(row) * glyph.width,
                        static_cast<std::size_t>(glyph.width));
        }
        font.glyphs.push_back({
            .codepoint = glyph.codepoint,
            .atlasX = static_cast<std::uint16_t>(x),
            .atlasY = static_cast<std::uint16_t>(y),
            .width = static_cast<std::uint16_t>(glyph.width),
            .height = static_cast<std::uint16_t>(glyph.height),
            .left = static_cast<float>(glyph.offsetX),
            .top = static_cast<float>(glyph.offsetY),
            .advance = glyph.advance,
        });
    }
    std::ranges::sort(font.glyphs, {}, &FontGlyph::codepoint);

    if (core::Result<void> valid = validate(font); !valid)
    {
        return std::unexpected(valid.error());
    }
    // The pairs that move when one letter follows another, which is what keeps an A from drifting
    // away from a V. Only the pairs the font actually moves are kept, among the alphabets: the
    // characters of Chinese, Japanese and Korean, from U+2E80 on, are not kerned.
    constexpr std::uint32_t firstIdeograph = 0x2E80;
    std::vector<std::pair<std::uint32_t, int>> kerned;
    for (const BakedGlyph& glyph : baked)
    {
        if (glyph.codepoint < firstIdeograph)
        {
            kerned.emplace_back(glyph.codepoint, glyph.index);
        }
    }
    std::ranges::sort(kerned);
    for (const auto& [first, firstIndex] : kerned)
    {
        for (const auto& [second, secondIndex] : kerned)
        {
            const int amount = stbtt_GetGlyphKernAdvance(&info, firstIndex, secondIndex);
            if (amount != 0)
            {
                font.kerning.push_back({first, second, static_cast<float>(amount) * scale});
            }
        }
    }
    DEVEX_LOG_DEBUG("Baked {} glyphs and {} kerning pairs of '{}' into a {}x{} atlas at {} pixels",
                    font.glyphs.size(), font.kerning.size(), font.family, font.atlasWidth,
                    font.atlasHeight, font.bakedSize);
    return font;
}

} // namespace devex::asset
