#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/DrawList.hpp>
#include <devex/ui/GlyphAtlas.hpp>
#include <devex/ui/Layout.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

using devex::asset::FontData;
using devex::math::Vec2;
using devex::render::TextureHandle;
using devex::ui::GlyphAtlas;
using devex::ui::GlyphPages;
using devex::ui::RasterGlyph;

namespace {

[[nodiscard]] const FontData& font()
{
    static const FontData baked = [] {
        devex::asset::ImportContext context{
            .source = std::filesystem::path{DEVEX_TEST_DATA_DIRECTORY} / "fonts" / "NotoSans-Regular.ttf",
            .mainId = devex::asset::AssetId::generate(),
            .name = "font",
            .options = {{"size", devex::serialization::TextValue(32.0)}},
        };
        const auto result = devex::asset::importFontFile(context);
        REQUIRE(result.has_value());
        const auto data = devex::asset::decodeFont(result->artifacts.front().bytes);
        REQUIRE(data.has_value());
        return *data;
    }();
    return baked;
}

// Pages kept in memory, as the renderer would receive them.
struct FakePages
{
    struct Update
    {
        TextureHandle page;
        std::uint32_t top = 0;
        std::uint32_t height = 0;
        std::vector<std::byte> rows;
    };

    std::uint32_t created = 0;
    std::vector<TextureHandle> destroyed;
    std::vector<Update> updates;

    [[nodiscard]] GlyphPages pages()
    {
        return GlyphPages{
            .create =
                [this](std::uint32_t size) {
                    CHECK(size == GlyphAtlas::pageSize);
                    ++created;
                    return TextureHandle{.index = created, .generation = 1};
                },
            .update =
                [this](TextureHandle page, std::uint32_t top, std::uint32_t height, std::span<const std::byte> rows) {
                    updates.push_back({.page = page, .top = top, .height = height, .rows = {rows.begin(), rows.end()}});
                },
            .destroy = [this](TextureHandle page) { destroyed.push_back(page); },
        };
    }
};

// The coverage of the pixels of a letter, read back from the rows sent for its page.
[[nodiscard]] std::uint32_t inkOf(const FakePages::Update& update, const RasterGlyph& glyph)
{
    std::uint32_t ink = 0;
    const auto left = static_cast<std::uint32_t>(std::lround(glyph.uvMin.x * GlyphAtlas::pageSize));
    const auto top = static_cast<std::uint32_t>(std::lround(glyph.uvMin.y * GlyphAtlas::pageSize));
    for (std::uint32_t y = top; y < top + static_cast<std::uint32_t>(glyph.size.y); ++y)
    {
        REQUIRE(y >= update.top);
        REQUIRE(y < update.top + update.height);
        for (std::uint32_t x = left; x < left + static_cast<std::uint32_t>(glyph.size.x); ++x)
        {
            ink += static_cast<std::uint32_t>(update.rows[std::size_t{y - update.top} * GlyphAtlas::pageSize + x]);
        }
    }
    return ink;
}

} // namespace

TEST_CASE("A font keeps its file, which its letters are drawn from at any size", "[ui][glyphs]")
{
    REQUIRE_FALSE(font().source.empty());
    // A TrueType file starts with its version.
    CHECK(font().source[0] == 0x00);
    CHECK(font().source[1] == 0x01);
}

TEST_CASE("A letter is drawn at its size into a page, which the next flush sends", "[ui][glyphs]")
{
    FakePages fake;
    GlyphAtlas atlas(fake.pages());

    const RasterGlyph* const letter = atlas.glyph(font(), 14.0f, 'H', 0);
    REQUIRE(letter != nullptr);
    CHECK(letter->page == TextureHandle{.index = 1, .generation = 1});
    // A capital of 14 pixels stands about 10 pixels above the baseline, its stems on whole pixels.
    CHECK(letter->size.y >= 9.0f);
    CHECK(letter->size.y <= 11.0f);
    CHECK(letter->offset.y == Catch::Approx(-letter->size.y));
    CHECK(letter->size.x == Catch::Approx((letter->uvMax.x - letter->uvMin.x) * GlyphAtlas::pageSize));
    CHECK(atlas.pageCount() == 1);
    CHECK(fake.updates.empty());

    // Asked again, it is the same image.
    CHECK(atlas.glyph(font(), 14.0f, 'H', 0) == letter);
    CHECK(atlas.glyphCount() == 1);

    atlas.flush();
    REQUIRE(fake.updates.size() == 1);
    CHECK(fake.updates[0].page == letter->page);
    CHECK(fake.updates[0].rows.size() == std::size_t{fake.updates[0].height} * GlyphAtlas::pageSize);
    CHECK(inkOf(fake.updates[0], *letter) > 0);
    // Nothing changed since: nothing is sent.
    atlas.flush();
    CHECK(fake.updates.size() == 1);

    // A letter at another size or a quarter of a pixel further is another image.
    const RasterGlyph* const larger = atlas.glyph(font(), 28.0f, 'H', 0);
    REQUIRE(larger != nullptr);
    CHECK(larger->size.y > letter->size.y * 1.8f);
    const RasterGlyph* const shifted = atlas.glyph(font(), 14.0f, 'H', 2);
    REQUIRE(shifted != nullptr);
    CHECK(shifted != letter);
    CHECK(atlas.glyphCount() == 3);
    atlas.flush();
    REQUIRE(fake.updates.size() == 2);
    // Half a pixel to the right, the stems cover their pixels differently.
    CHECK(inkOf(fake.updates[1], *shifted) > 0);
}

TEST_CASE("A letter that draws nothing, or a font without its file, gives no image", "[ui][glyphs]")
{
    FakePages fake;
    GlyphAtlas atlas(fake.pages());
    CHECK(atlas.glyph(font(), 14.0f, ' ', 0) == nullptr);
    // Remembered as empty, so that it is not drawn again.
    CHECK(atlas.glyphCount() == 1);
    CHECK(atlas.pageCount() == 0);

    FontData baked = font();
    baked.source.clear();
    CHECK(atlas.glyph(baked, 14.0f, 'H', 0) == nullptr);
    // Too large: the distances draw it as well.
    CHECK(atlas.glyph(font(), 300.0f, 'H', 0) == nullptr);
}

TEST_CASE("An atlas that runs out of pages starts over after the frame that filled it", "[ui][glyphs]")
{
    FakePages fake;
    GlyphAtlas atlas(fake.pages());
    // Large letters, a few dozen to a page, until there are more pages than the atlas keeps.
    float size = 200.0f;
    std::uint32_t codepoint = 'A';
    while (atlas.pageCount() <= GlyphAtlas::maxPages)
    {
        static_cast<void>(atlas.glyph(font(), size, codepoint, 0));
        if (++codepoint > 'Z')
        {
            codepoint = 'A';
            size += 0.25f;
        }
    }
    const std::size_t pages = atlas.pageCount();
    // The frame still shows its letters: their rows are sent, no page is destroyed yet.
    atlas.flush();
    CHECK(fake.updates.size() == pages);
    CHECK(fake.destroyed.empty());

    // The next frame draws into a fresh page.
    const RasterGlyph* const letter = atlas.glyph(font(), 14.0f, 'H', 0);
    REQUIRE(letter != nullptr);
    CHECK(fake.destroyed.size() == pages);
    CHECK(atlas.pageCount() == 1);
    CHECK(atlas.glyphCount() == 1);
    CHECK(letter->page.index == fake.created);
}

TEST_CASE("A pen stands on a whole pixel and a quarter of a pixel past it", "[ui][glyphs]")
{
    const auto check = [](float x, float whole, int subpixel) {
        const devex::ui::SnappedPen pen = devex::ui::snapPen(x);
        CHECK(pen.whole == Catch::Approx(whole));
        CHECK(pen.subpixel == subpixel);
    };
    check(10.0f, 10.0f, 0);
    check(10.3f, 10.0f, 1);
    check(10.6f, 10.0f, 2);
    check(10.74f, 10.0f, 3);
    // Closer to the next pixel than to its last quarter: the next pixel.
    check(10.9f, 11.0f, 0);
    check(-0.2f, -1.0f, 3);
}

TEST_CASE("A text with an atlas draws its letters at their size on whole pixels", "[ui][glyphs][draw]")
{
    using devex::scene::Canvas;
    using devex::scene::Entity;
    using devex::scene::UiRect;
    using devex::scene::UiText;

    devex::scene::Scene scene;
    const Entity canvas = scene.createEntity("Canvas");
    scene.add<Canvas>(canvas, Canvas{.scaleMode = devex::scene::CanvasScaleMode::ConstantPixels});
    const Entity label = scene.createEntity("Label");
    REQUIRE(scene.setParent(label, canvas).has_value());
    scene.add<UiRect>(label, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {10.3f, 5.0f}, .offsetMax = {300.0f, 40.0f}});
    scene.add<UiText>(label, UiText{.text = "Hello", .size = 14.0f});

    const TextureHandle sdf{.index = 40, .generation = 1};
    FakePages fake;
    GlyphAtlas atlas(fake.pages());
    devex::ui::LayoutResult layout;
    devex::ui::layoutCanvas(scene, canvas, Vec2{640.0f, 480.0f}, layout);
    const auto draw = [&](devex::ui::GlyphAtlas* glyphs) {
        devex::render::RenderWorld world;
        devex::ui::buildDrawList(scene, layout,
                                 devex::ui::DrawContext{
                                     .fonts = [&](devex::asset::AssetId) { return devex::ui::FontRef{.data = &font(), .atlas = sdf}; },
                                     .glyphs = glyphs,
                                 },
                                 world);
        return world;
    };

    const devex::render::RenderWorld raster = draw(&atlas);
    REQUIRE(raster.uiDraws.size() == 1);
    CHECK(raster.uiDraws[0].kind == devex::render::UiDrawKind::Glyph);
    CHECK(raster.uiDraws[0].texture == TextureHandle{.index = 1, .generation = 1});
    // A quad a letter: H, e, l, l, o, the two l one image unless their pens stand at other quarters.
    REQUIRE(raster.uiVertices.size() == 20);
    CHECK(atlas.glyphCount() >= 4);
    CHECK(atlas.glyphCount() <= 5);
    for (const devex::render::UiVertex& vertex : raster.uiVertices)
    {
        CHECK(vertex.position.x == Catch::Approx(std::round(vertex.position.x)));
        CHECK(vertex.position.y == Catch::Approx(std::round(vertex.position.y)));
    }
    // The letters stand in order, past the corner of the label.
    CHECK(raster.uiVertices[0].position.x >= 10.0f);
    CHECK(raster.uiVertices[4].position.x > raster.uiVertices[0].position.x);

    // Without an atlas, or outlined, the letters keep their distances.
    const devex::render::RenderWorld distances = draw(nullptr);
    REQUIRE_FALSE(distances.uiDraws.empty());
    CHECK(distances.uiDraws[0].kind == devex::render::UiDrawKind::Text);
    CHECK(distances.uiDraws[0].texture == sdf);
    scene.get<UiText>(label).outlineWidth = 1.0f;
    const devex::render::RenderWorld outlined = draw(&atlas);
    CHECK(std::ranges::all_of(outlined.uiDraws, [](const devex::render::UiDraw& each) {
        return each.kind == devex::render::UiDrawKind::Text;
    }));
}

