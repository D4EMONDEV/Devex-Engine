#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/ui/TextLayout.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using devex::asset::FontData;
using devex::math::Vec2;
using devex::scene::TextAlign;
using devex::scene::TextVerticalAlign;
using devex::ui::GlyphQuad;
using devex::ui::TextLayoutResult;
using devex::ui::TextStyle;

namespace {

// The font is baked once: the tests all read the same letters.
[[nodiscard]] const FontData& font()
{
    static const FontData baked = [] {
        devex::asset::ImportContext context{
            .source = std::filesystem::path{DEVEX_TEST_DATA_DIRECTORY} / "fonts" /
                      "NotoSans-Regular.ttf",
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

} // namespace

TEST_CASE("A text is read as UTF-8, broken bytes included", "[ui][text]")
{
    std::size_t offset = 0;
    const std::string_view text = "aé€";
    CHECK(devex::ui::nextCodepoint(text, offset) == U'a');
    CHECK(devex::ui::nextCodepoint(text, offset) == U'é');
    CHECK(devex::ui::nextCodepoint(text, offset) == U'€');
    CHECK(offset == text.size());

    // A byte that starts nothing comes back as the unknown character rather than stopping the text.
    std::size_t broken = 0;
    CHECK(devex::ui::nextCodepoint("\xFF", broken) == 0xFFFD);
    CHECK(broken == 1);
}

TEST_CASE("A line of text is placed letter after letter from its corner", "[ui][text]")
{
    TextLayoutResult result;
    devex::ui::layoutText(font(), "Hi", TextStyle{.size = 32.0f}, Vec2{100.0f, 50.0f},
                          Vec2{400.0f, 100.0f}, result);

    REQUIRE(result.glyphs.size() == 2);
    CHECK(result.lineCount == 1);
    // The letters follow each other, and the first one starts at the left edge.
    CHECK(result.glyphs[0].min.x < result.glyphs[1].min.x);
    CHECK(result.glyphs[0].min.x == Catch::Approx(100.0f).margin(4.0));
    // They sit under the top edge, within the ascent of the font.
    CHECK(result.glyphs[0].min.y > 50.0f);
    CHECK(result.glyphs[0].min.y < 50.0f + 32.0f);
    // Each letter reads its own place in the atlas.
    CHECK(result.glyphs[0].uvMin.x != result.glyphs[1].uvMin.x);
    CHECK(result.glyphs[0].uvMax.x <= 1.0f);
}

TEST_CASE("A text twice as large takes twice the room", "[ui][text]")
{
    const Vec2 small = devex::ui::measureText(font(), "Jouer", TextStyle{.size = 24.0f});
    const Vec2 large = devex::ui::measureText(font(), "Jouer", TextStyle{.size = 48.0f});

    CHECK(small.x > 0.0f);
    CHECK(large.x == Catch::Approx(small.x * 2.0f).epsilon(0.01));
    CHECK(large.y == Catch::Approx(small.y * 2.0f).epsilon(0.01));
}

TEST_CASE("A text wraps at the width of its rectangle, between words", "[ui][text]")
{
    const std::string_view sentence = "Un menu simple pour commencer la partie";
    const TextStyle style{.size = 24.0f, .wrap = true};
    const float full = devex::ui::measureText(font(), sentence, style).x;

    TextLayoutResult result;
    devex::ui::layoutText(font(), sentence, style, Vec2{0.0f, 0.0f}, Vec2{full * 0.5f, 400.0f},
                          result);
    CHECK(result.lineCount > 1);
    CHECK(result.size.x <= full * 0.5f);
    // Wrapping loses no letter: the spaces alone are dropped at the cuts.
    CHECK(result.glyphs.size() >= sentence.size() - 7);

    // Without wrapping, the whole sentence stays on one line and runs past the rectangle.
    TextLayoutResult single;
    devex::ui::layoutText(font(), sentence, TextStyle{.size = 24.0f, .wrap = false},
                          Vec2{0.0f, 0.0f}, Vec2{full * 0.5f, 400.0f}, single);
    CHECK(single.lineCount == 1);
    CHECK(single.size.x == Catch::Approx(full));
}

TEST_CASE("New lines start a line of their own", "[ui][text]")
{
    TextLayoutResult result;
    devex::ui::layoutText(font(), "un\ndeux\ntrois", TextStyle{.size = 20.0f}, Vec2{0.0f, 0.0f},
                          Vec2{1000.0f, 400.0f}, result);
    CHECK(result.lineCount == 3);
    // Each line sits under the previous one.
    CHECK(result.glyphs.front().min.y < result.glyphs.back().min.y);
}

TEST_CASE("Alignment moves the text inside its rectangle", "[ui][text]")
{
    const TextStyle left{.size = 24.0f};
    const TextStyle centred{.size = 24.0f, .align = TextAlign::Center};
    const TextStyle right{.size = 24.0f, .align = TextAlign::Right};
    const TextStyle middle{.size = 24.0f, .verticalAlign = TextVerticalAlign::Middle};

    const auto firstGlyph = [](const TextStyle& style) {
        TextLayoutResult result;
        devex::ui::layoutText(font(), "Jouer", style, Vec2{0.0f, 0.0f}, Vec2{400.0f, 200.0f},
                              result);
        REQUIRE_FALSE(result.glyphs.empty());
        return result.glyphs.front().min;
    };

    CHECK(firstGlyph(centred).x > firstGlyph(left).x);
    CHECK(firstGlyph(right).x > firstGlyph(centred).x);
    CHECK(firstGlyph(middle).y > firstGlyph(left).y);
    // A centred text keeps the same margin on both sides.
    TextLayoutResult result;
    devex::ui::layoutText(font(), "Jouer", centred, Vec2{0.0f, 0.0f}, Vec2{400.0f, 200.0f}, result);
    const float before = result.glyphs.front().min.x;
    const float after = 400.0f - result.glyphs.back().max.x;
    // The text is centred on the advances of its letters, so the ink of the first and the last one
    // leaves margins that differ by their own side bearings.
    CHECK(before == Catch::Approx(after).margin(4.0));
}

TEST_CASE("The sharpness of the text follows the size it is drawn at", "[ui][text]")
{
    // At the size it was baked at, one unit covers the whole spread of the distances.
    CHECK(devex::ui::textSharpness(font(), font().bakedSize) == Catch::Approx(font().spread));
    CHECK(devex::ui::textSharpness(font(), font().bakedSize * 2.0f) ==
          Catch::Approx(font().spread * 2.0f));
}
