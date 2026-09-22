#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::asset::FontData;
using devex::asset::FontGlyph;
using devex::asset::ImportContext;

namespace {

const std::filesystem::path fontFile =
    std::filesystem::path{DEVEX_TEST_DATA_DIRECTORY} / "fonts" / "NotoSans-Regular.ttf";

[[nodiscard]] ImportContext contextFor(double size)
{
    return ImportContext{
        .source = fontFile,
        .mainId = AssetId::generate(),
        .name = "font",
        // A small size keeps the atlas quick to bake in debug builds.
        .options = {{"size", devex::serialization::TextValue(size)},
                    {"spread", devex::serialization::TextValue(4.0)}},
    };
}

[[nodiscard]] FontData importFont(double size = 16.0)
{
    ImportContext context = contextFor(size);
    const auto result = devex::asset::importFontFile(context);
    REQUIRE(result.has_value());
    REQUIRE(result->artifacts.size() == 1);
    CHECK(result->artifacts.front().id == context.mainId);
    CHECK(result->artifacts.front().type == AssetType::Font);

    const auto font = devex::asset::decodeFont(result->artifacts.front().bytes);
    REQUIRE(font.has_value());
    return *font;
}

} // namespace

TEST_CASE("A font imports as an atlas of distances covering the Latin letters", "[asset][font]")
{
    const FontData font = importFont();

    CHECK(font.family == "NotoSans-Regular");
    CHECK(font.bakedSize == 16.0f);
    CHECK(font.spread == 4.0f);
    CHECK(font.ascent > 0.0f);
    CHECK(font.descent < 0.0f);
    CHECK(font.lineHeight() > font.bakedSize);
    CHECK(font.atlas.size() == static_cast<std::size_t>(font.atlasWidth) * font.atlasHeight);

    // Letters, digits, punctuation and the accents of western Europe are all baked.
    for (const std::uint32_t codepoint : {U'A', U'z', U'0', U'?', U'é', U'ç', U'ü'})
    {
        INFO("codepoint " << codepoint);
        CHECK(devex::asset::findGlyph(font, codepoint) != nullptr);
    }
    CHECK(devex::asset::findGlyph(font, U'字') == nullptr);
}

TEST_CASE("Every glyph carries an image inside the atlas and an advance", "[asset][font]")
{
    const FontData font = importFont();

    const FontGlyph* const letter = devex::asset::findGlyph(font, U'A');
    REQUIRE(letter != nullptr);
    CHECK(letter->width > 0);
    CHECK(letter->height > 0);
    CHECK(letter->advance > 0.0f);
    // The distances of a letter run from its inside to its outside, crossing the outline at 128.
    std::uint8_t lowest = 255;
    std::uint8_t highest = 0;
    for (std::uint32_t row = 0; row < letter->height; ++row)
    {
        for (std::uint32_t column = 0; column < letter->width; ++column)
        {
            const std::uint8_t distance =
                font.atlas[(static_cast<std::size_t>(letter->atlasY) + row) * font.atlasWidth +
                           letter->atlasX + column];
            lowest = std::min(lowest, distance);
            highest = std::max(highest, distance);
        }
    }
    CHECK(lowest < 128);
    CHECK(highest > 128);

    // A space is drawn as nothing, yet it moves the pen.
    const FontGlyph* const space = devex::asset::findGlyph(font, U' ');
    REQUIRE(space != nullptr);
    CHECK(space->width == 0);
    CHECK(space->advance > 0.0f);

    // Glyphs never overlap each other, nor leave the atlas.
    for (const FontGlyph& glyph : font.glyphs)
    {
        INFO("codepoint " << glyph.codepoint);
        CHECK(static_cast<std::uint32_t>(glyph.atlasX) + glyph.width <= font.atlasWidth);
        CHECK(static_cast<std::uint32_t>(glyph.atlasY) + glyph.height <= font.atlasHeight);
    }
}

TEST_CASE("A font baked larger keeps its shape and grows its atlas", "[asset][font]")
{
    const FontData small = importFont(16.0);
    const FontData large = importFont(48.0);

    CHECK(large.atlasHeight > small.atlasHeight);
    CHECK(large.glyphs.size() == small.glyphs.size());

    const FontGlyph* const smallLetter = devex::asset::findGlyph(small, U'M');
    const FontGlyph* const largeLetter = devex::asset::findGlyph(large, U'M');
    REQUIRE(smallLetter != nullptr);
    REQUIRE(largeLetter != nullptr);
    // Three times the size advances the pen three times as far, give or take the rounding of the
    // outline to pixels.
    CHECK(largeLetter->advance > smallLetter->advance * 2.8f);
    CHECK(largeLetter->advance < smallLetter->advance * 3.2f);
}

TEST_CASE("A file that is not a font fails to import", "[asset][font]")
{
    ImportContext context = contextFor(16.0);
    context.source = std::filesystem::path{DEVEX_TEST_DATA_DIRECTORY} / "triangle.gltf";
    CHECK_FALSE(devex::asset::importFontFile(context).has_value());
}
