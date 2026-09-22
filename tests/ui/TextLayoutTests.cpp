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

TEST_CASE("Kerning brings a letter under the one before it", "[ui][text]")
{
    // The font carries the pairs it moves: A and V lean into each other.
    const float amount = devex::asset::kerningBetween(font(), U'A', U'V');
    CHECK(amount < 0.0f);
    CHECK(devex::asset::kerningBetween(font(), U'A', U'A') == 0.0f);

    const TextStyle style{.size = 32.0f, .wrap = false};
    const float pair = devex::ui::measureText(font(), "AV", style).x;
    const float apart = devex::ui::measureText(font(), "AA", style).x;
    CHECK(pair < apart);
}

TEST_CASE("A rich text colours, bolds and sizes the spans it marks", "[ui][text]")
{
    TextLayoutResult result;
    const TextStyle style{.size = 32.0f, .wrap = false, .rich = true};
    devex::ui::layoutText(font(), "a[b]b[/b][color=#ff0000]c[/color][size=64]a", style,
                          Vec2{0.0f, 0.0f}, Vec2{600.0f, 100.0f}, result);

    REQUIRE(result.glyphs.size() == 4);
    CHECK_FALSE(result.glyphs[0].bold);
    CHECK(result.glyphs[1].bold);
    // The colour is kept the way the engine keeps colours: red alone, and lit.
    CHECK(result.glyphs[2].color.x == Catch::Approx(1.0f));
    CHECK(result.glyphs[2].color.y == Catch::Approx(0.0f));
    CHECK_FALSE(result.glyphs[2].bold);
    // The last letter is the same one, twice as tall.
    const float small = result.glyphs[0].max.y - result.glyphs[0].min.y;
    const float large = result.glyphs[3].max.y - result.glyphs[3].min.y;
    CHECK(large == Catch::Approx(small * 2.0f).margin(1.0));
}

TEST_CASE("Two brackets in a row write one, and an unknown mark is left alone", "[ui][text]")
{
    TextLayoutResult plain;
    devex::ui::layoutText(font(), "[[x]", TextStyle{.size = 32.0f, .wrap = false, .rich = true},
                          Vec2{0.0f, 0.0f}, Vec2{600.0f, 100.0f}, plain);
    // "[x]" is written: the doubled bracket, the letter and the closing one.
    CHECK(plain.glyphs.size() == 3);

    TextLayoutResult unknown;
    devex::ui::layoutText(font(), "[wave]a", TextStyle{.size = 32.0f, .wrap = false, .rich = true},
                          Vec2{0.0f, 0.0f}, Vec2{600.0f, 100.0f}, unknown);
    CHECK(unknown.glyphs.size() == 7);
}

TEST_CASE("The cursor stands before every letter and at the end of each line", "[ui][text]")
{
    TextLayoutResult result;
    const TextStyle style{.size = 32.0f, .wrap = false};
    devex::ui::layoutText(font(), "ab\ncd", style, Vec2{0.0f, 0.0f}, Vec2{600.0f, 200.0f}, result);

    // Two letters and the end of the line, twice over.
    REQUIRE(result.stops.size() == 6);
    CHECK(result.stops[0].offset == 0);
    CHECK(result.stops[2].offset == 2);
    CHECK(result.stops[2].line == 0);
    CHECK(result.stops[3].line == 1);
    CHECK(result.stops.back().offset == 5);

    // The place of an offset, and the offset of a place, answer each other.
    const devex::ui::CaretStop* const third = devex::ui::caretAt(result, 1);
    REQUIRE(third != nullptr);
    CHECK(third->offset == 1);
    CHECK(devex::ui::offsetAt(result, third->position) == 1);
    // A point past the end of the second line lands after its last letter.
    const devex::ui::CaretStop& last = result.stops.back();
    CHECK(devex::ui::offsetAt(
              result, Vec2{last.position.x + 100.0f, last.position.y + last.height * 0.5f}) == 5);
}

TEST_CASE("A password carries its cursor from the letters to the dots", "[ui][text]")
{
    const std::string_view secret = "éa";
    CHECK(devex::ui::characterIndexOf(secret, secret.size()) == 2);
    // The first letter takes two bytes, so the second character starts at 2.
    CHECK(devex::ui::offsetOfCharacter(secret, 1) == 2);
    CHECK(devex::ui::previousOffset(secret, 2) == 0);

    const std::string dots = devex::ui::shownText(secret, true);
    CHECK(devex::ui::characterIndexOf(dots, dots.size()) == 2);
    CHECK(dots.size() == 6);
    CHECK(devex::ui::shownText(secret, false) == secret);
}
