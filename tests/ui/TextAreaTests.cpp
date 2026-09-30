#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/DrawList.hpp>
#include <devex/ui/Layout.hpp>
#include <devex/ui/TextArea.hpp>
#include <devex/ui/UiWorld.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using devex::core::Duration;
using devex::math::Vec2;
using devex::math::Vec4;
using devex::scene::Canvas;
using devex::scene::CanvasScaleMode;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::UiRect;
using devex::scene::UiText;
using devex::scene::UiTextArea;
using devex::ui::UiInput;
using devex::ui::UiWorld;

namespace {

constexpr Vec2 window{1920.0f, 1080.0f};
constexpr Duration frame{std::chrono::milliseconds(16)};

[[nodiscard]] const devex::asset::FontData& bakedFont()
{
    static const devex::asset::FontData baked = [] {
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

[[nodiscard]] devex::ui::FontRef fontRef(devex::asset::AssetId)
{
    devex::render::TextureHandle atlas;
    atlas.index = 0;
    atlas.generation = 1;
    return devex::ui::FontRef{.data = &bakedFont(), .atlas = atlas};
}

// A canvas holding one area of text, 400 units wide and 200 tall, and the world that runs it.
struct Notes
{
    Scene scene;
    Entity canvas;
    Entity area;
    UiWorld world;

    explicit Notes(std::string text = {}, UiTextArea settings = {})
    {
        canvas = scene.createEntity("Canvas");
        scene.add<Canvas>(canvas, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
        area = scene.createEntity("Notes");
        REQUIRE(scene.setParent(area, canvas).has_value());
        scene.add<UiRect>(area, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {100.0f, 100.0f}, .offsetMax = {500.0f, 300.0f}});
        scene.add<UiText>(area, UiText{.text = std::move(text), .size = 20.0f});
        scene.add<UiTextArea>(area, std::move(settings));
        world.setFonts(&fontRef);
        world.update(scene, window, UiInput{}, frame);
    }

    [[nodiscard]] std::string& text()
    {
        return scene.get<UiText>(area).text;
    }
    [[nodiscard]] UiTextArea& settings()
    {
        return scene.get<UiTextArea>(area);
    }
    void send(UiInput input)
    {
        world.update(scene, window, input, frame);
    }
    // A press and a release at a point of the window.
    void click(Vec2 point)
    {
        send(UiInput{.pointer = point, .pointerDown = true, .pointerPressed = true});
        send(UiInput{.pointer = point, .pointerReleased = true});
    }
    [[nodiscard]] std::size_t caret() const
    {
        return world.textSelection(area).first;
    }
    [[nodiscard]] devex::render::RenderWorld draw() const
    {
        devex::render::RenderWorld drawn;
        world.build(scene, devex::ui::DrawContext{.fonts = &fontRef}, drawn);
        return drawn;
    }
};

} // namespace

TEST_CASE("The lines of a text are found by their new lines, and words around a byte", "[ui][textarea]")
{
    std::vector<std::uint32_t> starts;
    devex::ui::indexLines("one\ntwo\n\nfour", starts);
    REQUIRE(starts == std::vector<std::uint32_t>{0, 4, 8, 9});
    CHECK(devex::ui::lineOfOffset(starts, 0) == 0);
    CHECK(devex::ui::lineOfOffset(starts, 3) == 0);
    CHECK(devex::ui::lineOfOffset(starts, 4) == 1);
    CHECK(devex::ui::lineOfOffset(starts, 8) == 2);
    CHECK(devex::ui::lineOfOffset(starts, 13) == 3);
    CHECK(devex::ui::lineText("one\ntwo\n\nfour", starts, 1) == "two");
    CHECK(devex::ui::lineText("one\ntwo\n\nfour", starts, 2).empty());
    CHECK(devex::ui::lineText("one\ntwo\n\nfour", starts, 3) == "four");
    // A text always has a first line, even empty.
    devex::ui::indexLines("", starts);
    CHECK(starts == std::vector<std::uint32_t>{0});

    const std::string_view code = "speed_x += 12;";
    CHECK(devex::ui::wordAround(code, 2) == std::pair<std::size_t, std::size_t>{0, 7});
    // Just past a word, the word is still the one meant.
    CHECK(devex::ui::wordAround(code, 7) == std::pair<std::size_t, std::size_t>{0, 7});
    CHECK(devex::ui::wordAround(code, 8) == std::pair<std::size_t, std::size_t>{8, 9});
    CHECK(devex::ui::wordAround(code, 12) == std::pair<std::size_t, std::size_t>{11, 13});
    CHECK(devex::ui::nextWord(code, 0) == 8);
    CHECK(devex::ui::nextWord(code, 8) == 11);
    CHECK(devex::ui::previousWord(code, 13) == 11);
    CHECK(devex::ui::previousWord(code, 11) == 8);
    CHECK(devex::ui::previousWord(code, 8) == 0);
    // A new line is a stop of its own.
    CHECK(devex::ui::nextWord("a\nb", 1) == 2);
    CHECK(devex::ui::previousWord("a\nb", 2) == 1);
}

TEST_CASE("A line of an area says where the cursor stands, tabs included", "[ui][textarea]")
{
    devex::ui::TextAreaLine plain;
    devex::ui::layoutAreaLine(bakedFont(), "abc", 20.0f, 4, Vec2{50.0f, 10.0f}, plain);
    CHECK(plain.xOf(0) == Catch::Approx(50.0f));
    CHECK(plain.xOf(1) > plain.xOf(0));
    CHECK(plain.xOf(3) > plain.xOf(2));
    CHECK(plain.offsetAt(plain.xOf(2) + 0.5f) == 2);
    CHECK(plain.offsetAt(0.0f) == 0);
    CHECK(plain.offsetAt(10000.0f) == 3);

    // A tab is one byte, as wide as four spaces.
    devex::ui::TextAreaLine spaces;
    devex::ui::layoutAreaLine(bakedFont(), "    b", 20.0f, 4, Vec2{50.0f, 10.0f}, spaces);
    devex::ui::TextAreaLine tabbed;
    devex::ui::layoutAreaLine(bakedFont(), "\tb", 20.0f, 4, Vec2{50.0f, 10.0f}, tabbed);
    CHECK(tabbed.xOf(1) == Catch::Approx(spaces.xOf(4)));
    CHECK(tabbed.xOf(2) == Catch::Approx(spaces.xOf(5)));
    CHECK(tabbed.offsetAt(spaces.xOf(1)) == 0);
    CHECK(tabbed.offsetAt(spaces.xOf(4)) == 1);
    CHECK(tabbed.offsetAt(10000.0f) == 2);
}

TEST_CASE("An area of text takes what is typed, new lines that keep their indentation, and tabs", "[ui][textarea]")
{
    Notes notes({}, UiTextArea{.indentAfter = "{", .action = "notes"});
    CHECK_FALSE(notes.world.isEditing());
    // Typing goes nowhere until the area is clicked.
    notes.send(UiInput{.typed = "lost"});
    CHECK(notes.text().empty());

    notes.click(Vec2{300.0f, 200.0f});
    CHECK(notes.world.isEditing());
    CHECK(notes.world.editedTextArea() == notes.area);
    notes.send(UiInput{.typed = "if (x) {"});
    CHECK(notes.text() == "if (x) {");
    CHECK(notes.world.wasChanged(notes.area));
    CHECK(notes.world.wasChanged("notes"));

    // Enter writes a new line, indented once more after what opens a block.
    notes.send(UiInput{.submitPressed = true});
    CHECK(notes.text() == "if (x) {\n    ");
    notes.send(UiInput{.typed = "y();"});
    notes.send(UiInput{.submitPressed = true});
    CHECK(notes.text() == "if (x) {\n    y();\n    ");
    // Backspace in the indentation goes back a whole stop, and Tab forward to the next one.
    notes.send(UiInput{.backspacePressed = true});
    CHECK(notes.text() == "if (x) {\n    y();\n");
    notes.send(UiInput{.typed = "ab"});
    notes.send(UiInput{.tabPressed = true});
    CHECK(notes.text() == "if (x) {\n    y();\nab  ");
    // A frame that changes nothing reports nothing.
    notes.send(UiInput{});
    CHECK_FALSE(notes.world.wasChanged(notes.area));

    // With several lines selected, Tab moves them in and Shift+Tab out; an empty line stays empty.
    notes.text() = "a\n\nb";
    notes.send(UiInput{});
    notes.send(UiInput{.selectAllPressed = true});
    notes.send(UiInput{.tabPressed = true});
    CHECK(notes.text() == "    a\n\n    b");
    notes.send(UiInput{.selecting = true, .tabPressed = true});
    CHECK(notes.text() == "a\n\nb");

    // An area that only shows takes no change, but still selects and copies.
    notes.settings().readOnly = true;
    notes.send(UiInput{.typed = "no"});
    notes.send(UiInput{.backspacePressed = true});
    CHECK(notes.text() == "a\n\nb");
    notes.send(UiInput{.selectAllPressed = true});
    notes.send(UiInput{.copyPressed = true});
    CHECK(notes.world.clipboardRequest() == "a\n\nb");

    // Clicking away gives the keyboard back.
    notes.click(Vec2{900.0f, 900.0f});
    CHECK_FALSE(notes.world.isEditing());

    // A field that starts to be typed into takes the keyboard from the area, and the area from it.
    const Entity field = notes.scene.createEntity("Field");
    REQUIRE(notes.scene.setParent(field, notes.canvas).has_value());
    notes.scene.add<UiRect>(field, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {100.0f, 400.0f}, .offsetMax = {500.0f, 440.0f}});
    notes.scene.add<UiText>(field, UiText{.text = "", .size = 20.0f});
    notes.scene.add<devex::scene::UiInput>(field);
    notes.settings().readOnly = false;
    notes.world.startEditing(notes.scene, notes.area);
    notes.world.startEditing(notes.scene, field);
    CHECK_FALSE(notes.world.editedTextArea().isValid());
    notes.send(UiInput{.typed = "x"});
    CHECK(notes.scene.get<UiText>(field).text == "x");
    CHECK(notes.text() == "a\n\nb");
    notes.world.startEditing(notes.scene, notes.area);
    CHECK_FALSE(notes.world.editedField().isValid());
    notes.send(UiInput{.typed = "y"});
    CHECK(notes.scene.get<UiText>(field).text == "x");
    CHECK(notes.text() != "a\n\nb");
}

TEST_CASE("The cursor of an area moves by letters, words, lines and pages, and selects", "[ui][textarea]")
{
    Notes notes("alpha beta\n  gamma\nxy");
    notes.world.startEditing(notes.scene, notes.area);
    REQUIRE(notes.world.editedTextArea() == notes.area);
    notes.world.selectText(notes.scene, notes.area, 0, 0);

    notes.send(UiInput{.rightPressed = true, .wordModifier = true});
    CHECK(notes.caret() == 6);
    notes.send(UiInput{.endPressed = true});
    CHECK(notes.caret() == 10);
    // Down keeps the place across as far as the line goes, and comes back to it.
    notes.send(UiInput{.downPressed = true});
    CHECK(notes.caret() == 18);
    notes.send(UiInput{.downPressed = true});
    CHECK(notes.caret() == 21);
    notes.send(UiInput{.upPressed = true});
    notes.send(UiInput{.upPressed = true});
    CHECK(notes.caret() == 10);
    // Home goes to the first letter of the line, then to its start.
    notes.send(UiInput{.downPressed = true});
    notes.send(UiInput{.homePressed = true});
    CHECK(notes.caret() == 13);
    notes.send(UiInput{.homePressed = true});
    CHECK(notes.caret() == 11);
    // With the modifier, Home and End reach the ends of the text; a page is more than it holds.
    notes.send(UiInput{.endPressed = true, .wordModifier = true});
    CHECK(notes.caret() == 21);
    notes.send(UiInput{.pageUpPressed = true});
    CHECK(notes.caret() == 0);

    // Shift keeps where the selection started; typing replaces it.
    notes.send(UiInput{.rightPressed = true, .selecting = true, .wordModifier = true});
    CHECK(notes.world.textSelection(notes.area) == std::pair<std::size_t, std::size_t>{6, 0});
    notes.send(UiInput{.typed = "A "});
    CHECK(notes.text() == "A beta\n  gamma\nxy");
    // Ctrl+Backspace and Ctrl+Delete take a word.
    notes.send(UiInput{.deletePressed = true, .wordModifier = true});
    CHECK(notes.text() == "A \n  gamma\nxy");
    notes.send(UiInput{.backspacePressed = true, .wordModifier = true});
    CHECK(notes.text() == "\n  gamma\nxy");

    // A press lands between two letters, a second one takes the word, a third one the line.
    const std::optional<UiWorld::CaretPlace> place = notes.world.textCaretPlace(notes.scene, notes.area);
    REQUIRE(place.has_value());
    const Vec2 second{place->position.x + 40.0f, place->position.y + place->height * 1.5f};
    notes.click(second);
    const std::size_t pressed = notes.caret();
    CHECK(pressed >= 3);
    CHECK(pressed <= 8);
    notes.click(second);
    CHECK(notes.world.textSelection(notes.area) == std::pair<std::size_t, std::size_t>{8, 3});
    notes.click(second);
    CHECK(notes.world.textSelection(notes.area) == std::pair<std::size_t, std::size_t>{9, 1});
    CHECK(notes.world.textLineAt(notes.scene, notes.area, second) == 1);
    CHECK(notes.world.textLineAt(notes.scene, notes.area, Vec2{900.0f, 900.0f}) == -1);
}

TEST_CASE("An area goes back on what was typed, and on what was written into it from outside", "[ui][textarea]")
{
    Notes notes;
    notes.world.startEditing(notes.scene, notes.area);
    CHECK_FALSE(notes.world.canUndoText(notes.area));
    // Letters typed one after the other are one change, up to the end of a word.
    notes.send(UiInput{.typed = "on"});
    notes.send(UiInput{.typed = "e"});
    notes.send(UiInput{.typed = " "});
    notes.send(UiInput{.typed = "two"});
    CHECK(notes.text() == "one two");
    notes.send(UiInput{.undoPressed = true});
    CHECK(notes.text() == "one ");
    notes.send(UiInput{.undoPressed = true});
    CHECK(notes.text().empty());
    CHECK_FALSE(notes.world.canUndoText(notes.area));
    notes.send(UiInput{.redoPressed = true});
    notes.send(UiInput{.redoPressed = true});
    CHECK(notes.text() == "one two");
    CHECK(notes.caret() == 7);
    CHECK_FALSE(notes.world.canRedoText(notes.area));

    // A tool that rewrites the text makes a change like any other.
    notes.text() = "one TWO two";
    notes.send(UiInput{});
    CHECK_FALSE(notes.world.wasChanged(notes.area));
    notes.world.undoText(notes.scene, notes.area);
    CHECK(notes.text() == "one two");
    notes.world.redoText(notes.scene, notes.area);
    CHECK(notes.text() == "one TWO two");
    // A new change forgets what could be made again.
    notes.world.undoText(notes.scene, notes.area);
    notes.send(UiInput{.typed = "!"});
    CHECK_FALSE(notes.world.canRedoText(notes.area));

    // Cut and paste are changes too, and the returns of another system are left out.
    notes.send(UiInput{.selectAllPressed = true});
    notes.send(UiInput{.cutPressed = true});
    CHECK(notes.text().empty());
    CHECK(notes.world.clipboardRequest() == "one two!");
    notes.send(UiInput{.pastePressed = true, .clipboard = "a\r\nb"});
    CHECK(notes.text() == "a\nb");
    notes.send(UiInput{.undoPressed = true});
    notes.send(UiInput{.undoPressed = true});
    CHECK(notes.text() == "one two!");

    // Another text altogether starts a history of its own.
    notes.text() = "fresh";
    notes.world.forgetTextHistory(notes.scene, notes.area);
    notes.send(UiInput{});
    CHECK_FALSE(notes.world.canUndoText(notes.area));
    CHECK(notes.text() == "fresh");
}

TEST_CASE("An area draws the lines in view only, and scrolls to its cursor and with the wheel", "[ui][textarea]")
{
    std::string text;
    for (int line = 0; line < 500; ++line)
    {
        text += "line\n";
    }
    Notes notes(text, UiTextArea{.padding = {0.0f, 0.0f}, .lineNumbers = false});
    const std::pair<std::size_t, std::size_t> top = notes.world.visibleTextLines(notes.scene, notes.area);
    CHECK(top.first == 0);
    CHECK(top.second >= 7);
    CHECK(top.second <= 12);
    // Four letters a line, for the lines in view and no other, and the thumb of the bar.
    const devex::render::RenderWorld drawn = notes.draw();
    CHECK(drawn.uiVertices.size() == top.second * 4 * 4 + 4);

    // The wheel moves three lines a notch, and never past the ends.
    const Vec2 inside{300.0f, 200.0f};
    notes.send(UiInput{.pointer = inside, .wheel = -2.0f});
    CHECK(notes.world.visibleTextLines(notes.scene, notes.area).first == 6);
    notes.send(UiInput{.pointer = inside, .wheel = 100.0f});
    CHECK(notes.settings().scroll.y == Catch::Approx(0.0f));
    // The wheel elsewhere leaves it alone.
    notes.send(UiInput{.pointer = Vec2{900.0f, 900.0f}, .wheel = -2.0f});
    CHECK(notes.settings().scroll.y == Catch::Approx(0.0f));

    // The cursor is brought into view wherever it goes.
    notes.world.startEditing(notes.scene, notes.area);
    notes.send(UiInput{.endPressed = true, .wordModifier = true});
    const std::pair<std::size_t, std::size_t> bottom = notes.world.visibleTextLines(notes.scene, notes.area);
    CHECK(bottom.first + bottom.second == 501);
    notes.world.selectText(notes.scene, notes.area, 5 * 250, 5 * 250);
    const std::pair<std::size_t, std::size_t> middle = notes.world.visibleTextLines(notes.scene, notes.area);
    CHECK(middle.first <= 250);
    CHECK(middle.first + middle.second > 250);

    // The numbers of the lines take a gutter, and the letters start after it.
    const std::optional<UiWorld::CaretPlace> bare = notes.world.textCaretPlace(notes.scene, notes.area);
    notes.settings().lineNumbers = true;
    notes.send(UiInput{});
    const std::optional<UiWorld::CaretPlace> numbered = notes.world.textCaretPlace(notes.scene, notes.area);
    REQUIRE(bare.has_value());
    REQUIRE(numbered.has_value());
    CHECK(numbered->position.x > bare->position.x + 20.0f);
}

TEST_CASE("The runs of an area take their colours, and what is selected or found shows behind the letters", "[ui][textarea]")
{
    Notes notes("int x;", UiTextArea{.scrollbarSize = 0.0f});
    const Vec4 blue{0.0f, 0.0f, 1.0f, 1.0f};
    const Vec4 yellow{1.0f, 1.0f, 0.0f, 0.5f};
    notes.world.setTextSpans(notes.area, {devex::ui::TextSpan{.begin = 0, .end = 3, .color = blue}});
    // "int", "x" and ";": five letters, the first three in blue and the others in the colour of the text.
    const devex::render::RenderWorld plain = notes.draw();
    REQUIRE(plain.uiVertices.size() == 5 * 4);
    CHECK(plain.uiVertices[0].color == blue);
    CHECK(plain.uiVertices[2 * 4].color == blue);
    CHECK(plain.uiVertices[3 * 4].color == Vec4{1.0f, 1.0f, 1.0f, 1.0f});

    // What a search found is a box behind its letters, drawn before them.
    notes.world.setTextHighlights(notes.area, {devex::ui::TextSpan{.begin = 4, .end = 5, .color = yellow}});
    const devex::render::RenderWorld found = notes.draw();
    REQUIRE(found.uiVertices.size() == 6 * 4);
    CHECK(found.uiVertices[0].color == yellow);

    // While it is edited: the line of the cursor, the selection, the letters, then the cursor.
    notes.world.startEditing(notes.scene, notes.area);
    notes.world.selectText(notes.scene, notes.area, 0, 3);
    const devex::render::RenderWorld edited = notes.draw();
    REQUIRE(edited.uiVertices.size() == 9 * 4);
    CHECK(edited.uiVertices[0].color == notes.settings().currentLineColor);
    CHECK(edited.uiVertices[2 * 4].color == notes.settings().selectionColor);
    // The selection covers the first three letters.
    CHECK(edited.uiVertices[2 * 4].position.x == Catch::Approx(edited.uiVertices[3 * 4].position.x).margin(3.0f));
    CHECK(edited.uiVertices[8 * 4].color == notes.settings().caretColor);

    // A line that is marked is underlined, and with a gutter it has a bar at its left.
    notes.world.setTextMarks(notes.area, {devex::ui::TextLineMark{.line = 0, .color = Vec4{1.0f, 0.0f, 0.0f, 1.0f}}});
    CHECK(notes.draw().uiVertices.size() == 10 * 4);
}
