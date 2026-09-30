#include <devex/asset/Artifact.hpp>
#include <devex/asset/ThemeData.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/DrawList.hpp>
#include <devex/ui/Layout.hpp>
#include <devex/ui/UiWorld.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>

using devex::core::Duration;
using devex::math::Vec2;
using devex::math::Vec4;
using devex::scene::Canvas;
using devex::scene::CanvasScaleMode;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::UiButton;
using devex::scene::UiImage;
using devex::scene::UiLayout;
using devex::scene::UiLayoutKind;
using devex::scene::UiRect;
using devex::ui::UiInput;
using devex::ui::UiWorld;

namespace {

constexpr Vec2 window{1920.0f, 1080.0f};
constexpr Duration frame{std::chrono::milliseconds(16)};

// A canvas holding a column of buttons, as a menu is built.
struct Menu
{
    Scene scene;
    Entity canvas;
    Entity column;
    std::vector<Entity> buttons;

    explicit Menu(int count)
    {
        canvas = scene.createEntity("Canvas");
        scene.add<Canvas>(canvas, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
        column = scene.createEntity("Entries");
        REQUIRE(scene.setParent(column, canvas).has_value());
        scene.add<UiRect>(column, UiRect{.anchorMin = {0.5f, 0.5f},
                                         .anchorMax = {0.5f, 0.5f},
                                         .offsetMin = {-200.0f, -150.0f},
                                         .offsetMax = {200.0f, 150.0f}});
        scene.add<UiLayout>(column, UiLayout{.kind = UiLayoutKind::Column, .spacing = 10.0f});
        for (int index = 0; index < count; ++index)
        {
            const Entity button = scene.createEntity("Button");
            REQUIRE(scene.setParent(button, column).has_value());
            scene.add<UiRect>(button, UiRect{.anchorMin = {0.0f, 0.0f},
                                             .anchorMax = {1.0f, 0.0f},
                                             .offsetMin = {0.0f, 0.0f},
                                             .offsetMax = {0.0f, 60.0f}});
            scene.add<UiImage>(button, UiImage{});
            scene.add<UiButton>(button, UiButton{.action = "entry", .fadeTime = 0.0f});
            buttons.push_back(button);
        }
    }

    // The middle of a button, in pixels of the window.
    [[nodiscard]] Vec2 centreOf(const UiWorld& world, std::size_t index) const
    {
        const auto canvases = world.canvases();
        REQUIRE_FALSE(canvases.empty());
        const devex::ui::LaidOutRect* const rect = canvases.front().layout.find(buttons[index]);
        REQUIRE(rect != nullptr);
        return Vec2{(rect->min.x + rect->max.x) * 0.5f, (rect->min.y + rect->max.y) * 0.5f};
    }
};

} // namespace

TEST_CASE("A button answers a press and a release on itself", "[ui][world]")
{
    Menu menu(3);
    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    const Vec2 first = menu.centreOf(world, 0);

    world.update(menu.scene, window, UiInput{.pointer = first}, frame);
    CHECK(world.hovered() == menu.buttons[0]);
    CHECK(world.pointerOverInterface());

    world.update(menu.scene, window,
                 UiInput{.pointer = first, .pointerDown = true, .pointerPressed = true}, frame);
    CHECK_FALSE(world.wasClicked("entry"));
    CHECK(world.focused() == menu.buttons[0]);

    world.update(menu.scene, window, UiInput{.pointer = first, .pointerReleased = true}, frame);
    CHECK(world.wasClicked("entry"));
    CHECK(world.wasClicked(menu.buttons[0]));
    CHECK_FALSE(world.wasClicked(menu.buttons[1]));

    // The click is over: the next frame reports nothing.
    world.update(menu.scene, window, UiInput{.pointer = first}, frame);
    CHECK_FALSE(world.wasClicked("entry"));
}

TEST_CASE("A release away from the button it was pressed on is not a click", "[ui][world]")
{
    Menu menu(3);
    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    const Vec2 first = menu.centreOf(world, 0);
    const Vec2 second = menu.centreOf(world, 1);

    world.update(menu.scene, window,
                 UiInput{.pointer = first, .pointerDown = true, .pointerPressed = true}, frame);
    world.update(menu.scene, window, UiInput{.pointer = second, .pointerReleased = true}, frame);
    CHECK_FALSE(world.wasClicked("entry"));
}

TEST_CASE("A button that cannot be used answers nothing", "[ui][world]")
{
    Menu menu(2);
    menu.scene.get<UiButton>(menu.buttons[0]).interactable = false;
    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    const Vec2 first = menu.centreOf(world, 0);

    world.update(menu.scene, window,
                 UiInput{.pointer = first, .pointerDown = true, .pointerPressed = true}, frame);
    world.update(menu.scene, window, UiInput{.pointer = first, .pointerReleased = true}, frame);
    CHECK_FALSE(world.wasClicked("entry"));
    CHECK_FALSE(world.hovered().isValid());
    // The pointer still rests on the interface, which swallows the click all the same.
    CHECK(world.pointerOverInterface());
}

TEST_CASE("The keyboard and the pad move the focus down the menu", "[ui][world]")
{
    Menu menu(3);
    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    CHECK_FALSE(world.focused().isValid());

    // The first step takes the first entry, wherever the pointer is.
    world.update(menu.scene, window, UiInput{.moveY = 1}, frame);
    CHECK(world.focused() == menu.buttons[0]);
    world.update(menu.scene, window, UiInput{.moveY = 1}, frame);
    CHECK(world.focused() == menu.buttons[1]);
    world.update(menu.scene, window, UiInput{.moveY = 1}, frame);
    CHECK(world.focused() == menu.buttons[2]);
    // At the end of the menu the focus stays where it is.
    world.update(menu.scene, window, UiInput{.moveY = 1}, frame);
    CHECK(world.focused() == menu.buttons[2]);
    world.update(menu.scene, window, UiInput{.moveY = -1}, frame);
    CHECK(world.focused() == menu.buttons[1]);

    // Submitting clicks the focused entry, with no pointer at all.
    world.update(menu.scene, window, UiInput{.submitPressed = true}, frame);
    CHECK(world.wasClicked(menu.buttons[1]));
    CHECK(world.wasClicked("entry"));
}

TEST_CASE("The focus skips the entries that are hidden or disabled", "[ui][world]")
{
    Menu menu(3);
    menu.scene.get<UiRect>(menu.buttons[1]).visible = false;
    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    world.update(menu.scene, window, UiInput{.moveY = 1}, frame);
    CHECK(world.focused() == menu.buttons[0]);
    world.update(menu.scene, window, UiInput{.moveY = 1}, frame);
    CHECK(world.focused() == menu.buttons[2]);
}

TEST_CASE("The topmost canvas answers the pointer first", "[ui][world]")
{
    Menu menu(2);
    // A second canvas over the first, covering it whole.
    const Entity overlay = menu.scene.createEntity("Overlay");
    menu.scene.add<Canvas>(overlay,
                           Canvas{.scaleMode = CanvasScaleMode::ConstantPixels, .sortOrder = 10});
    const Entity veil = menu.scene.createEntity("Veil");
    REQUIRE(menu.scene.setParent(veil, overlay).has_value());
    menu.scene.add<UiRect>(veil, UiRect{.anchorMin = {0.0f, 0.0f},
                                        .anchorMax = {1.0f, 1.0f},
                                        .offsetMin = {0.0f, 0.0f},
                                        .offsetMax = {0.0f, 0.0f}});
    menu.scene.add<UiImage>(veil, UiImage{});

    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    const Vec2 first = menu.centreOf(world, 0);
    world.update(menu.scene, window,
                 UiInput{.pointer = first, .pointerDown = true, .pointerPressed = true}, frame);
    world.update(menu.scene, window, UiInput{.pointer = first, .pointerReleased = true}, frame);
    // The veil took the click; the button under it never saw it.
    CHECK_FALSE(world.wasClicked("entry"));
    CHECK(world.pointerOverInterface());

    // Hiding the veil gives the menu back.
    menu.scene.get<UiRect>(veil).visible = false;
    world.update(menu.scene, window, UiInput{.pointer = first}, frame);
    CHECK(world.hovered() == menu.buttons[0]);
}

TEST_CASE("A canvas that answers nothing lets the pointer through", "[ui][world]")
{
    Menu menu(2);
    menu.scene.get<Canvas>(menu.canvas).interactive = false;
    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    const Vec2 first = menu.centreOf(world, 0);
    world.update(menu.scene, window, UiInput{.pointer = first}, frame);
    CHECK_FALSE(world.hovered().isValid());
    CHECK_FALSE(world.pointerOverInterface());
}

TEST_CASE("The tint of a button follows the pointer", "[ui][world]")
{
    Menu menu(2);
    menu.scene.get<UiButton>(menu.buttons[0]).hoverColor = devex::math::Vec4{2.0f, 2.0f, 2.0f, 1.0f};
    UiWorld world;
    world.update(menu.scene, window, UiInput{}, frame);
    const Vec2 first = menu.centreOf(world, 0);
    CHECK(world.tint(menu.buttons[0]).x == Catch::Approx(1.0f));

    world.update(menu.scene, window, UiInput{.pointer = first}, frame);
    CHECK(world.tint(menu.buttons[0]).x == Catch::Approx(2.0f));
    CHECK(world.tint(menu.buttons[1]).x == Catch::Approx(1.0f));

    world.update(menu.scene, window, UiInput{.pointer = Vec2{0.0f, 0.0f}}, frame);
    CHECK(world.tint(menu.buttons[0]).x == Catch::Approx(1.0f));
}

TEST_CASE("Escape and the east button ask to go back", "[ui][world]")
{
    Menu menu(1);
    UiWorld world;
    world.update(menu.scene, window, UiInput{.cancelPressed = true}, frame);
    CHECK(world.wasCancelled());
    world.update(menu.scene, window, UiInput{}, frame);
    CHECK_FALSE(world.wasCancelled());
}

namespace {

// A canvas holding the three elements a settings screen is made of: a field, a slider and a box
// to tick, each placed at a known spot so that the pointer can be aimed at it.
struct Form
{
    Scene scene;
    Entity canvas;
    Entity field;
    Entity slider;
    Entity toggle;

    Form()
    {
        canvas = scene.createEntity("Canvas");
        scene.add<Canvas>(canvas, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
        field = place("Field", Vec2{100.0f, 100.0f}, Vec2{500.0f, 150.0f});
        scene.add<devex::scene::UiText>(field, devex::scene::UiText{.text = "", .size = 32.0f});
        scene.add<devex::scene::UiInput>(
            field, devex::scene::UiInput{.placeholder = "Name", .action = "name"});
        slider = place("Slider", Vec2{100.0f, 200.0f}, Vec2{300.0f, 220.0f});
        scene.add<devex::scene::UiSlider>(
            slider, devex::scene::UiSlider{.value = 0.0f, .handleSize = 0.0f, .action = "volume"});
        toggle = place("Toggle", Vec2{100.0f, 300.0f}, Vec2{130.0f, 330.0f});
        scene.add<devex::scene::UiToggle>(toggle, devex::scene::UiToggle{.action = "fullscreen"});
    }

    // An element of the canvas, at its place in the units of the window.
    Entity place(const char* name, Vec2 min, Vec2 max)
    {
        const Entity entity = scene.createEntity(name);
        REQUIRE(scene.setParent(entity, canvas).has_value());
        scene.add<UiRect>(entity, UiRect{.anchorMin = {0.0f, 0.0f},
                                         .anchorMax = {0.0f, 0.0f},
                                         .offsetMin = min,
                                         .offsetMax = max});
        scene.add<UiImage>(entity, UiImage{});
        return entity;
    }

    [[nodiscard]] std::string& text()
    {
        return scene.get<devex::scene::UiText>(field).text;
    }
};

// A click on a point, as two frames: the press and the release.
void clickAt(UiWorld& world, Scene& scene, Vec2 point)
{
    world.update(scene, window,
                 UiInput{.pointer = point, .pointerDown = true, .pointerPressed = true}, frame);
    world.update(scene, window, UiInput{.pointer = point, .pointerReleased = true}, frame);
}

} // namespace

TEST_CASE("A field takes what is typed once it is clicked on", "[ui][world][field]")
{
    Form form;
    UiWorld world;
    world.update(form.scene, window, UiInput{}, frame);
    CHECK_FALSE(world.isEditing());

    // Clicking the field starts the edit; clicking away ends it.
    world.update(
        form.scene, window,
        UiInput{.pointer = Vec2{300.0f, 120.0f}, .pointerDown = true, .pointerPressed = true},
        frame);
    CHECK(world.editedField() == form.field);

    world.update(form.scene, window, UiInput{.pointer = Vec2{300.0f, 120.0f}, .typed = "Bon"},
                 frame);
    CHECK(form.text() == "Bon");
    world.update(form.scene, window, UiInput{.pointer = Vec2{300.0f, 120.0f}, .typed = "jour"},
                 frame);
    CHECK(form.text() == "Bonjour");

    // Backspace takes one character back, whatever its length in bytes.
    world.update(form.scene, window, UiInput{.typed = "\xc3\xa9"}, frame);
    world.update(form.scene, window, UiInput{.backspacePressed = true}, frame);
    CHECK(form.text() == "Bonjour");

    world.update(
        form.scene, window,
        UiInput{.pointer = Vec2{1000.0f, 900.0f}, .pointerDown = true, .pointerPressed = true},
        frame);
    CHECK_FALSE(world.isEditing());
    CHECK(form.text() == "Bonjour");
}

TEST_CASE("A field selects with the arrows and hands the selection to the clipboard",
          "[ui][world][field]")
{
    Form form;
    UiWorld world;
    world.update(form.scene, window, UiInput{}, frame);
    clickAt(world, form.scene, Vec2{300.0f, 120.0f});
    world.update(form.scene, window, UiInput{.typed = "abc"}, frame);

    // Shift and the left arrow take the last letter in; copying hands it over.
    world.update(form.scene, window, UiInput{.leftPressed = true, .selecting = true}, frame);
    world.update(form.scene, window, UiInput{.copyPressed = true}, frame);
    CHECK(world.clipboardRequest() == "c");

    // Cutting takes it out of the text, and pasting puts it back.
    world.update(form.scene, window, UiInput{.leftPressed = true, .selecting = true}, frame);
    world.update(form.scene, window, UiInput{.cutPressed = true}, frame);
    CHECK(form.text() == "a");
    world.update(form.scene, window, UiInput{.pastePressed = true, .clipboard = "bc"}, frame);
    CHECK(form.text() == "abc");

    // Everything is taken in at once, and what is typed next replaces it.
    world.update(form.scene, window, UiInput{.selectAllPressed = true}, frame);
    world.update(form.scene, window, UiInput{.typed = "x"}, frame);
    CHECK(form.text() == "x");
}

TEST_CASE("A field hides a password, limits its length and ends on Enter", "[ui][world][field]")
{
    Form form;
    form.scene.get<devex::scene::UiInput>(form.field).password = true;
    form.scene.get<devex::scene::UiInput>(form.field).maxLength = 4;
    UiWorld world;
    world.update(form.scene, window, UiInput{}, frame);
    clickAt(world, form.scene, Vec2{300.0f, 120.0f});

    world.update(form.scene, window, UiInput{.typed = "abcdef"}, frame);
    CHECK(form.text() == "abcd");

    // A password is never handed to the clipboard, whatever is asked of it.
    world.update(form.scene, window, UiInput{.selectAllPressed = true}, frame);
    world.update(form.scene, window, UiInput{.copyPressed = true}, frame);
    CHECK(world.clipboardRequest().empty());

    world.update(form.scene, window, UiInput{.submitPressed = true}, frame);
    CHECK(world.wasSubmitted("name"));
    CHECK(world.wasSubmitted(form.field));
    CHECK_FALSE(world.isEditing());
    CHECK(form.text() == "abcd");
}

TEST_CASE("A field that takes several lines keeps Enter for itself", "[ui][world][field]")
{
    Form form;
    form.scene.get<devex::scene::UiInput>(form.field).multiline = true;
    UiWorld world;
    world.update(form.scene, window, UiInput{}, frame);
    clickAt(world, form.scene, Vec2{300.0f, 120.0f});

    world.update(form.scene, window, UiInput{.typed = "a"}, frame);
    world.update(form.scene, window, UiInput{.submitPressed = true}, frame);
    world.update(form.scene, window, UiInput{.typed = "b"}, frame);
    CHECK(form.text() == "a\nb");
    CHECK(world.isEditing());
    CHECK_FALSE(world.wasSubmitted("name"));

    // Escape gives the keyboard back without touching what was typed.
    world.update(form.scene, window, UiInput{.cancelPressed = true}, frame);
    CHECK_FALSE(world.isEditing());
    CHECK_FALSE(world.wasCancelled());
    CHECK(form.text() == "a\nb");
}

TEST_CASE("A slider follows the pointer and the arrows", "[ui][world][slider]")
{
    Form form;
    UiWorld world;
    world.update(form.scene, window, UiInput{}, frame);

    // The track runs from 100 to 300: its middle is half of the range.
    world.update(
        form.scene, window,
        UiInput{.pointer = Vec2{200.0f, 210.0f}, .pointerDown = true, .pointerPressed = true},
        frame);
    CHECK(form.scene.get<devex::scene::UiSlider>(form.slider).value == Catch::Approx(0.5f));
    CHECK(world.wasChanged("volume"));

    // Dragging past the end stops at it, and letting go stops the drag.
    world.update(
        form.scene, window,
        UiInput{.pointer = Vec2{400.0f, 400.0f}, .pointerDown = true, .pointerMoved = true}, frame);
    CHECK(form.scene.get<devex::scene::UiSlider>(form.slider).value == Catch::Approx(1.0f));
    world.update(form.scene, window,
                 UiInput{.pointer = Vec2{400.0f, 400.0f}, .pointerReleased = true}, frame);
    world.update(form.scene, window,
                 UiInput{.pointer = Vec2{100.0f, 400.0f}, .pointerMoved = true}, frame);
    CHECK(form.scene.get<devex::scene::UiSlider>(form.slider).value == Catch::Approx(1.0f));

    // The arrows move the slider that has the focus, and leave the focus where it is.
    world.setFocus(form.scene, form.slider);
    world.update(form.scene, window, UiInput{.moveX = -1}, frame);
    CHECK(form.scene.get<devex::scene::UiSlider>(form.slider).value == Catch::Approx(0.95f));
    CHECK(world.focused() == form.slider);
}

TEST_CASE("A box is ticked by a click and by the submit button", "[ui][world][toggle]")
{
    Form form;
    UiWorld world;
    world.update(form.scene, window, UiInput{}, frame);

    clickAt(world, form.scene, Vec2{115.0f, 315.0f});
    CHECK(form.scene.get<devex::scene::UiToggle>(form.toggle).value);
    CHECK(world.wasChanged("fullscreen"));

    clickAt(world, form.scene, Vec2{115.0f, 315.0f});
    CHECK_FALSE(form.scene.get<devex::scene::UiToggle>(form.toggle).value);

    world.setFocus(form.scene, form.toggle);
    world.update(form.scene, window, UiInput{.submitPressed = true}, frame);
    CHECK(form.scene.get<devex::scene::UiToggle>(form.toggle).value);
}

TEST_CASE("A binding writes what it reads into the text beside it", "[ui][world][binding]")
{
    Form form;
    const Entity label = form.place("Label", Vec2{600.0f, 100.0f}, Vec2{900.0f, 140.0f});
    form.scene.add<devex::scene::UiText>(label, devex::scene::UiText{.text = "", .size = 24.0f});
    form.scene.add<devex::scene::Transform>(
        label, devex::scene::Transform{.position = devex::math::Vec3{1.25f, 0.0f, 0.0f}});
    form.scene.add<devex::scene::UiBinding>(label,
                                            devex::scene::UiBinding{.component = "Transform",
                                                                    .field = "position.x",
                                                                    .format = "x = {} m",
                                                                    .decimals = 1});

    UiWorld world;
    world.update(form.scene, window, UiInput{}, frame);
    CHECK(form.scene.get<devex::scene::UiText>(label).text == "x = 1.2 m");

    form.scene.get<devex::scene::Transform>(label).position.x = 3.0f;
    world.update(form.scene, window, UiInput{}, frame);
    CHECK(form.scene.get<devex::scene::UiText>(label).text == "x = 3.0 m");
}

TEST_CASE("A canvas hands the styles of its theme to the elements that follow them",
          "[ui][world][theme]")
{
    Form form;
    form.scene.get<UiRect>(form.toggle).style = "box";
    auto shared = std::make_shared<devex::asset::ThemeData>();
    devex::asset::ThemeData& theme = *shared;
    theme.styles.push_back(
        {.name = "box",
         .values = {{.component = "UiImage",
                     .field = "color",
                     .value = "vec4(0.25, 0.5, 0.75, 1)"},
                    {.component = "UiToggle", .field = "check_size", .value = "0.8"},
                    // A component the element does not carry is left alone.
                    {.component = "UiSlider", .field = "value", .value = "0.5"}}});

    UiWorld world;
    world.setThemes([&shared](devex::asset::AssetId) { return shared; });
    form.scene.get<Canvas>(form.canvas).theme = devex::asset::AssetId::generate();
    world.update(form.scene, window, UiInput{}, frame);

    CHECK(form.scene.get<UiImage>(form.toggle).color.z == Catch::Approx(0.75f));
    CHECK(form.scene.get<devex::scene::UiToggle>(form.toggle).checkSize == Catch::Approx(0.8f));
    // The elements that name no style keep what they carry.
    CHECK(form.scene.get<UiImage>(form.field).color.z == Catch::Approx(1.0f));
}

namespace {

// A real font, baked once, for the tests that place a click between letters or draw a field.
[[nodiscard]] const devex::asset::FontData& bakedFont()
{
    static const devex::asset::FontData baked = [] {
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

[[nodiscard]] devex::ui::FontRef fontRef(devex::asset::AssetId)
{
    devex::render::TextureHandle atlas;
    atlas.index = 0;
    atlas.generation = 1;
    return devex::ui::FontRef{.data = &bakedFont(), .atlas = atlas};
}

} // namespace

TEST_CASE("A field being edited draws its selection, its letters and its cursor",
          "[ui][world][field]")
{
    for (const bool password : {false, true})
    {
        Form form;
        form.scene.get<devex::scene::UiInput>(form.field).password = password;
        UiWorld world;
        world.setFonts(&fontRef);
        world.update(form.scene, window, UiInput{}, frame);
        clickAt(world, form.scene, Vec2{300.0f, 120.0f});
        world.update(form.scene, window, UiInput{.typed = "ab"}, frame);
        world.update(form.scene, window, UiInput{.selectAllPressed = true}, frame);

        const devex::ui::EditState* const edit = world.editStateOf(form.field);
        REQUIRE(edit != nullptr);
        CHECK(edit->selectionMin == 0);
        // A dot takes three bytes where a letter takes one.
        CHECK(edit->selectionMax == (password ? 6u : 2u));

        devex::render::RenderWorld frameWorld;
        world.build(form.scene, devex::ui::DrawContext{.fonts = &fontRef}, frameWorld);
        // The selection, the two letters and the cursor, in batches of their own.
        CHECK(frameWorld.uiDraws.size() >= 3);
        CHECK(frameWorld.uiIndices.size() >= 6 * 4);
    }
}

TEST_CASE("The text of a dropdown stands off its left edge and leaves room for its arrow", "[ui][world][controls]")
{
    // Where the letters of the chosen option start and end, aligned one way or the other.
    const auto drawnText = [](devex::scene::TextAlign align) {
        Scene scene;
        const Entity canvas = scene.createEntity("Canvas");
        scene.add<Canvas>(canvas, Canvas{.scaleMode = devex::scene::CanvasScaleMode::ConstantPixels});
        const Entity choice = scene.createEntity("Choice");
        REQUIRE(scene.setParent(choice, canvas).has_value());
        scene.add<UiRect>(choice, UiRect{.anchorMin = {0.0f, 0.0f},
                                         .anchorMax = {0.0f, 0.0f},
                                         .offsetMin = {100.0f, 100.0f},
                                         .offsetMax = {300.0f, 140.0f}});
        scene.add<devex::scene::UiText>(choice, devex::scene::UiText{.size = 20.0f, .align = align, .wrap = false});
        scene.add<devex::scene::UiDropdown>(choice, devex::scene::UiDropdown{.options = {"Normal"}});
        UiWorld world;
        world.setFonts(&fontRef);
        world.update(scene, window, UiInput{}, frame);

        devex::render::RenderWorld drawn;
        world.build(scene, devex::ui::DrawContext{.fonts = &fontRef}, drawn);
        Vec2 span{1.0e9f, -1.0e9f};
        for (const devex::render::UiDraw& draw : drawn.uiDraws)
        {
            if (draw.kind != devex::render::UiDrawKind::Text)
            {
                continue;
            }
            for (std::uint32_t index = draw.firstIndex; index < draw.firstIndex + draw.indexCount; ++index)
            {
                const float x = drawn.uiVertices[drawn.uiIndices[index]].position.x;
                span.x = std::min(span.x, x);
                span.y = std::max(span.y, x);
            }
        }
        REQUIRE(span.y > span.x);
        return span;
    };
    // The quads of the letters reach a little past them, by the spread of the atlas.
    const Vec2 left = drawnText(devex::scene::TextAlign::Left);
    CHECK(left.x > 100.0f + 40.0f * 0.3f - 4.0f);
    // The arrow is drawn 0.45 of the height from the right edge, 0.28 of it wide on each side.
    const Vec2 right = drawnText(devex::scene::TextAlign::Right);
    CHECK(right.y < 300.0f - 40.0f * (0.45f + 0.28f));
}

TEST_CASE("A turned text turns its letters with it", "[ui][world]")
{
    // How far the letters of a line spread across and down, turned or not.
    const auto spread = [](float rotation) {
        Scene scene;
        const Entity canvas = scene.createEntity("Canvas");
        scene.add<Canvas>(canvas, Canvas{.scaleMode = devex::scene::CanvasScaleMode::ConstantPixels});
        const Entity title = scene.createEntity("Title");
        REQUIRE(scene.setParent(title, canvas).has_value());
        scene.add<UiRect>(title, UiRect{.anchorMin = {0.0f, 0.0f},
                                        .anchorMax = {0.0f, 0.0f},
                                        .offsetMin = {400.0f, 400.0f},
                                        .offsetMax = {700.0f, 440.0f},
                                        .pivot = {0.0f, 0.5f},
                                        .rotation = rotation});
        scene.add<devex::scene::UiText>(title, devex::scene::UiText{.text = "Collision layers", .size = 24.0f, .wrap = false});
        UiWorld world;
        world.setFonts(&fontRef);
        world.update(scene, window, UiInput{}, frame);
        devex::render::RenderWorld drawn;
        world.build(scene, devex::ui::DrawContext{.fonts = &fontRef}, drawn);
        Vec2 low{1.0e9f};
        Vec2 high{-1.0e9f};
        for (const devex::render::UiDraw& draw : drawn.uiDraws)
        {
            for (std::uint32_t index = draw.firstIndex; draw.kind == devex::render::UiDrawKind::Text && index < draw.firstIndex + draw.indexCount;
                 ++index)
            {
                const Vec2 point = drawn.uiVertices[drawn.uiIndices[index]].position;
                low = Vec2{std::min(low.x, point.x), std::min(low.y, point.y)};
                high = Vec2{std::max(high.x, point.x), std::max(high.y, point.y)};
            }
        }
        REQUIRE(high.x > low.x);
        return high - low;
    };
    // A line lies across; turned by a quarter about its left end, it stands up, above that end.
    const Vec2 flat = spread(0.0f);
    CHECK(flat.x > flat.y * 3.0f);
    const Vec2 standing = spread(-devex::math::radians(90.0f));
    CHECK(standing.y > standing.x * 3.0f);
    CHECK(standing.y == Catch::Approx(flat.x).margin(1.0f));
}

TEST_CASE("A tooltip without a font of its own speaks with the font of the interface around it", "[ui][world][controls]")
{
    Scene scene;
    const Entity canvas = scene.createEntity("Canvas");
    scene.add<Canvas>(canvas, Canvas{.scaleMode = devex::scene::CanvasScaleMode::ConstantPixels});
    const auto element = [&](const char* name, Vec2 min, Vec2 max) {
        const Entity entity = scene.createEntity(name);
        REQUIRE(scene.setParent(entity, canvas).has_value());
        scene.add<UiRect>(entity, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = min, .offsetMax = max});
        return entity;
    };
    // A box with no text, and far below it the one text of the screen, which names its font.
    const Entity box = element("Box", {100.0f, 100.0f}, {140.0f, 140.0f});
    scene.add<UiImage>(box);
    scene.add<UiButton>(box);
    scene.add<devex::scene::UiTooltip>(box, devex::scene::UiTooltip{.text = "Fullscreen", .delay = 0.1f});
    const Entity label = element("Label", {100.0f, 600.0f}, {400.0f, 640.0f});
    scene.add<devex::scene::UiText>(label, devex::scene::UiText{.text = "Settings",
                                                                .font = devex::asset::AssetId::generate(),
                                                                .size = 24.0f,
                                                                .wrap = false});

    // A game has no default font: only the fonts it names resolve.
    const auto fonts = [](devex::asset::AssetId id) {
        return id.isValid() ? fontRef(id) : devex::ui::FontRef{};
    };
    UiWorld world;
    world.setFonts(fonts);
    world.update(scene, window, UiInput{.pointer = {120.0f, 120.0f}}, frame);
    world.update(scene, window, UiInput{.pointer = {120.0f, 120.0f}}, std::chrono::milliseconds(200));

    devex::render::RenderWorld drawn;
    world.build(scene, devex::ui::DrawContext{.fonts = fonts}, drawn);
    // Letters near the pointer, besides those of the label far below.
    bool tooltipLetters = false;
    for (const devex::render::UiDraw& draw : drawn.uiDraws)
    {
        if (draw.kind != devex::render::UiDrawKind::Text)
        {
            continue;
        }
        for (std::uint32_t index = draw.firstIndex; index < draw.firstIndex + draw.indexCount; ++index)
        {
            tooltipLetters = tooltipLetters || drawn.uiVertices[drawn.uiIndices[index]].position.y < 300.0f;
        }
    }
    CHECK(tooltipLetters);
}

TEST_CASE("A click lands between the letters of a field", "[ui][world][field]")
{
    Form form;
    form.text() = "abcdef";
    UiWorld world;
    world.setFonts(&fontRef);
    world.update(form.scene, window, UiInput{}, frame);

    // Far to the right of the letters: after the last one. At the left edge: before the first.
    clickAt(world, form.scene, Vec2{490.0f, 125.0f});
    REQUIRE(world.editStateOf(form.field) != nullptr);
    CHECK(world.editStateOf(form.field)->caret == 6);
    clickAt(world, form.scene, Vec2{101.0f, 125.0f});
    CHECK(world.editStateOf(form.field)->caret == 0);

    // Dragging from the start takes letters in.
    world.update(form.scene, window,
                 UiInput{.pointer = Vec2{101.0f, 125.0f}, .pointerDown = true,
                         .pointerPressed = true},
                 frame);
    world.update(form.scene, window,
                 UiInput{.pointer = Vec2{490.0f, 125.0f}, .pointerDown = true,
                         .pointerMoved = true},
                 frame);
    CHECK(world.editStateOf(form.field)->selectionMin == 0);
    CHECK(world.editStateOf(form.field)->selectionMax == 6);
}

namespace {

[[nodiscard]] std::shared_ptr<devex::asset::ThemeData> boxTheme(const char* color)
{
    auto theme = std::make_shared<devex::asset::ThemeData>();
    theme->styles.push_back(
        {.name = "box", .values = {{.component = "UiImage", .field = "color", .value = color}}});
    return theme;
}

} // namespace

TEST_CASE("An element names its style, found in the theme of the canvas above it",
          "[ui][world][theme]")
{
    Form form;
    const std::shared_ptr<devex::asset::ThemeData> theme = boxTheme("vec4(1, 0, 0, 1)");
    const devex::asset::AssetId id = devex::asset::AssetId::generate();
    form.scene.get<Canvas>(form.canvas).theme = id;
    const devex::ui::ThemeSource themes = [&](devex::asset::AssetId asked) {
        return asked == id ? theme : nullptr;
    };

    // An element that names no style has nothing to show.
    CHECK(devex::ui::styleOf(form.scene, form.toggle, themes).name.empty());

    form.scene.get<UiRect>(form.toggle).style = "box";
    const devex::ui::ElementStyle found = devex::ui::styleOf(form.scene, form.toggle, themes);
    CHECK(found.name == "box");
    CHECK(found.theme == id);
    REQUIRE(found.style != nullptr);
    CHECK(found.sets("UiImage", "color"));
    CHECK_FALSE(found.sets("UiImage", "texture"));
    CHECK_FALSE(found.sets("UiText", "color"));

    // A name the theme does not carry is found as missing, and so is a canvas without a theme.
    form.scene.get<UiRect>(form.toggle).style = "nothing";
    const devex::ui::ElementStyle missing = devex::ui::styleOf(form.scene, form.toggle, themes);
    CHECK(missing.data != nullptr);
    CHECK(missing.style == nullptr);
    form.scene.get<Canvas>(form.canvas).theme = {};
    CHECK_FALSE(devex::ui::styleOf(form.scene, form.toggle, themes).theme.isValid());
}

TEST_CASE("A theme that changes applies at the next frame", "[ui][world][theme]")
{
    Form form;
    form.scene.get<UiRect>(form.toggle).style = "box";
    form.scene.get<Canvas>(form.canvas).theme = devex::asset::AssetId::generate();
    std::shared_ptr<const devex::asset::ThemeData> current = boxTheme("vec4(0.25, 0, 0, 1)");
    UiWorld world;
    world.setThemes([&current](devex::asset::AssetId) { return current; });

    world.update(form.scene, window, UiInput{}, frame);
    CHECK(form.scene.get<UiImage>(form.toggle).color.x == Catch::Approx(0.25f));

    // The file was saved again: the asset manager hands out the new theme, read once more.
    current = boxTheme("vec4(0.5, 0, 0, 1)");
    world.update(form.scene, window, UiInput{}, frame);
    CHECK(form.scene.get<UiImage>(form.toggle).color.x == Catch::Approx(0.5f));
}

TEST_CASE("The styles apply without an interface world, as the editor applies them",
          "[ui][theme]")
{
    Form form;
    form.scene.get<UiRect>(form.toggle).style = "box";
    form.scene.get<Canvas>(form.canvas).theme = devex::asset::AssetId::generate();
    const std::shared_ptr<devex::asset::ThemeData> theme = boxTheme("vec4(0.75, 0, 0, 1)");
    // A value a style writes badly is left alone rather than breaking the others.
    theme->styles.front().values.push_back({.component = "UiImage", .field = "corner_radius", .value = "vec4("});
    theme->styles.front().values.push_back({.component = "UiImage", .field = "raycast_target", .value = "false"});

    devex::ui::ThemeApplier applier;
    applier.apply(form.scene);
    CHECK(form.scene.get<UiImage>(form.toggle).color.x == Catch::Approx(1.0f));

    applier.setThemes([&theme](devex::asset::AssetId) { return theme; });
    applier.apply(form.scene);
    CHECK(form.scene.get<UiImage>(form.toggle).color.x == Catch::Approx(0.75f));
    CHECK(form.scene.get<UiImage>(form.toggle).cornerRadius == Catch::Approx(0.0f));
    CHECK_FALSE(form.scene.get<UiImage>(form.toggle).raycastTarget);
    // The elements that follow no style keep their own values.
    CHECK(form.scene.get<UiImage>(form.slider).color.x == Catch::Approx(1.0f));
}

TEST_CASE("The editor draws an interface smaller, inside the frame of its game", "[ui][draw]")
{
    Scene scene;
    const Entity canvas = scene.createEntity("Canvas");
    scene.add<Canvas>(canvas, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity panel = scene.createEntity("Panel");
    REQUIRE(scene.setParent(panel, canvas).has_value());
    scene.add<UiRect>(panel, UiRect{.anchorMin = {0.0f, 0.0f},
                                    .anchorMax = {0.0f, 0.0f},
                                    .offsetMin = {100.0f, 50.0f},
                                    .offsetMax = {300.0f, 150.0f}});
    scene.add<UiImage>(panel, UiImage{.cornerRadius = 10.0f});

    devex::ui::LayoutResult layout;
    devex::ui::layoutCanvas(scene, canvas, window, layout);
    devex::render::RenderWorld world;
    const devex::ui::DrawListMark mark = devex::ui::markDrawList(world);
    devex::ui::buildDrawList(scene, layout, devex::ui::DrawContext{}, world);
    REQUIRE(world.uiDraws.size() == 1);
    REQUIRE(world.uiVertices.size() == 4);

    // Half as large, from 40 pixels right and 20 down.
    devex::ui::placeDrawList(world, mark, Vec2{40.0f, 20.0f}, 0.5f);
    Vec2 low{1e9f};
    Vec2 high{-1e9f};
    for (const devex::render::UiVertex& vertex : world.uiVertices)
    {
        low = devex::math::min(low, vertex.position);
        high = devex::math::max(high, vertex.position);
    }
    CHECK(low.x == Catch::Approx(90.0f));
    CHECK(low.y == Catch::Approx(45.0f));
    CHECK(high.x == Catch::Approx(190.0f));
    CHECK(high.y == Catch::Approx(95.0f));
    const devex::render::UiDraw& draw = world.uiDraws.front();
    CHECK(draw.kind == devex::render::UiDrawKind::RoundedQuad);
    CHECK(draw.rect.x == Catch::Approx(90.0f));
    CHECK(draw.rect.w == Catch::Approx(95.0f));
    CHECK(draw.radius == Catch::Approx(5.0f));
    // An element that cuts nothing still draws on the whole image.
    CHECK(draw.clip == devex::math::Vec4{0.0f});

    // What was there before the mark stays where it was.
    const devex::ui::DrawListMark second = devex::ui::markDrawList(world);
    devex::ui::placeDrawList(world, second, Vec2{1000.0f, 1000.0f}, 2.0f);
    CHECK(world.uiDraws.front().rect.x == Catch::Approx(90.0f));
}

namespace {

// One element on a canvas one unit per pixel, drawn with a context.
devex::render::RenderWorld drawOne(Scene& scene, Entity element, const devex::ui::DrawContext& context)
{
    static_cast<void>(element);
    Entity canvas;
    for (auto [entity, component] : scene.view<Canvas>())
    {
        canvas = entity;
    }
    devex::ui::LayoutResult layout;
    devex::ui::layoutCanvas(scene, canvas, window, layout);
    devex::render::RenderWorld world;
    devex::ui::buildDrawList(scene, layout, context, world);
    return world;
}

Entity placedElement(Scene& scene, Vec2 min, Vec2 max)
{
    const Entity canvas = scene.createEntity("Canvas");
    scene.add<Canvas>(canvas, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity element = scene.createEntity("Element");
    REQUIRE(scene.setParent(element, canvas).has_value());
    scene.add<UiRect>(element, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = min, .offsetMax = max});
    return element;
}

} // namespace

TEST_CASE("An image shows a sprite, the part of its texture it covers, in its shape", "[ui][draw]")
{
    Scene scene;
    const Entity image = placedElement(scene, {0.0f, 0.0f}, {200.0f, 100.0f});
    const devex::asset::AssetId sprite{devex::core::Uuid::fromParts(1, 2)};
    const devex::asset::AssetId texture{devex::core::Uuid::fromParts(1, 3)};
    scene.add<UiImage>(image, UiImage{.texture = sprite, .preserveAspect = true});

    int textureLookups = 0;
    const devex::render::TextureHandle atlas{.index = 7, .generation = 1};
    const devex::ui::DrawContext context{
        .textures =
            [&](devex::asset::AssetId id) {
                ++textureLookups;
                return id == texture ? atlas : devex::render::TextureHandle{};
            },
        .sprites = [&](devex::asset::AssetId id) -> std::optional<devex::ui::SpriteImage> {
            if (id != sprite)
            {
                return std::nullopt;
            }
            return devex::ui::SpriteImage{.texture = atlas, .uv = {0.25f, 0.0f, 0.5f, 0.5f}, .size = {32.0f, 32.0f}};
        },
    };
    const devex::render::RenderWorld drawn = drawOne(scene, image, context);
    // A sprite is never looked for among the textures.
    CHECK(textureLookups == 0);
    REQUIRE(drawn.uiDraws.size() == 1);
    CHECK(drawn.uiDraws.front().texture == atlas);
    REQUIRE(drawn.uiVertices.size() == 4);
    // The region of the atlas...
    CHECK(drawn.uiVertices[0].uv == Vec2{0.25f, 0.0f});
    CHECK(drawn.uiVertices[2].uv == Vec2{0.5f, 0.5f});
    // ...kept square in the middle of its wide rectangle.
    CHECK(drawn.uiVertices[0].position.x == Catch::Approx(50.0f));
    CHECK(drawn.uiVertices[0].position.y == Catch::Approx(0.0f));
    CHECK(drawn.uiVertices[2].position.x == Catch::Approx(150.0f));
    CHECK(drawn.uiVertices[2].position.y == Catch::Approx(100.0f));

    // A texture, which is not a sprite, still draws whole and stretched.
    scene.get<UiImage>(image) = UiImage{.texture = texture};
    const devex::render::RenderWorld plain = drawOne(scene, image, context);
    CHECK(textureLookups == 1);
    REQUIRE(plain.uiVertices.size() == 4);
    CHECK(plain.uiVertices[0].uv == Vec2{0.0f, 0.0f});
    CHECK(plain.uiVertices[2].position.x == Catch::Approx(200.0f));

    // Sliced, the borders are fractions of the sprite, inside its region.
    scene.get<UiImage>(image) = UiImage{.texture = sprite, .border = {0.25f, 0.25f, 0.25f, 0.25f}};
    const devex::render::RenderWorld sliced = drawOne(scene, image, context);
    REQUIRE(sliced.uiVertices.size() == 36);
    // The first corner covers a quarter of the sprite: 8 of its 32 pixels, and of its region.
    CHECK(sliced.uiVertices[2].position.x == Catch::Approx(8.0f));
    CHECK(sliced.uiVertices[2].uv.x == Catch::Approx(0.25f + 0.25f * 0.25f));
    CHECK(sliced.uiVertices[2].uv.y == Catch::Approx(0.5f * 0.25f));
}

TEST_CASE("A plot draws its values as a line or as bars, between its bounds", "[ui][draw]")
{
    Scene scene;
    const Entity element = placedElement(scene, {0.0f, 0.0f}, {100.0f, 50.0f});
    scene.add<devex::scene::UiPlot>(element, devex::scene::UiPlot{.values = {0.0f, 1.0f, 0.5f}, .lineWidth = 2.0f});

    // A line: a thin quad per segment between the values.
    const devex::render::RenderWorld line = drawOne(scene, element, devex::ui::DrawContext{});
    REQUIRE(line.uiVertices.size() == 8);
    // The first segment goes from the bottom left to the top of the middle.
    const Vec2 start = (line.uiVertices[0].position + line.uiVertices[3].position) * 0.5f;
    const Vec2 end = (line.uiVertices[1].position + line.uiVertices[2].position) * 0.5f;
    CHECK(start.x == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(start.y == Catch::Approx(50.0f).margin(1e-4f));
    CHECK(end.x == Catch::Approx(50.0f).margin(1e-4f));
    CHECK(end.y == Catch::Approx(0.0f).margin(1e-4f));

    // Bars from the bottom; a value past the top stays at the edge.
    auto& plot = scene.get<devex::scene::UiPlot>(element);
    plot.kind = devex::scene::UiPlotKind::Bars;
    plot.values = {0.5f, 2.0f};
    const devex::render::RenderWorld bars = drawOne(scene, element, devex::ui::DrawContext{});
    REQUIRE(bars.uiVertices.size() == 8);
    CHECK(bars.uiVertices[0].position.y == Catch::Approx(25.0f));
    CHECK(bars.uiVertices[4].position.y == Catch::Approx(0.0f));
    CHECK(bars.uiVertices[4].position.x == Catch::Approx(50.0f));

    // Mirrored around the middle, with the mark of where it plays.
    plot.kind = devex::scene::UiPlotKind::MirroredBars;
    plot.values = {1.0f};
    plot.marker = 0.25f;
    const devex::render::RenderWorld wave = drawOne(scene, element, devex::ui::DrawContext{});
    REQUIRE(wave.uiVertices.size() == 8);
    CHECK(wave.uiVertices[0].position.y == Catch::Approx(0.0f));
    CHECK(wave.uiVertices[2].position.y == Catch::Approx(50.0f));
    // The marker, one unit each side of a quarter of the width.
    CHECK(wave.uiVertices[4].position.x == Catch::Approx(24.0f));
    CHECK(wave.uiVertices[6].position.x == Catch::Approx(26.0f));
}

TEST_CASE("A plot colours each value, draws a second series behind, guides and the value looked at", "[ui][draw]")
{
    Scene scene;
    const Entity element = placedElement(scene, {0.0f, 0.0f}, {100.0f, 50.0f});
    const Vec4 red{1.0f, 0.0f, 0.0f, 1.0f};
    const Vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    scene.add<devex::scene::UiPlot>(element, devex::scene::UiPlot{.values = {0.5f, 0.25f},
                                                                  .kind = devex::scene::UiPlotKind::Bars,
                                                                  .colors = {red, green},
                                                                  .backValues = {1.0f, 1.0f},
                                                                  .backColor = {1.0f, 1.0f, 1.0f, 0.5f},
                                                                  .guides = {0.5f, 2.0f},
                                                                  .guideColor = {0.0f, 0.0f, 1.0f, 1.0f},
                                                                  .highlighted = 1,
                                                                  .highlightColor = {1.0f, 1.0f, 1.0f, 0.25f}});

    // The value looked at first, then the series behind, the series, and the one guide in bounds.
    const devex::render::RenderWorld drawn = drawOne(scene, element, devex::ui::DrawContext{});
    REQUIRE(drawn.uiVertices.size() == 4 * (1 + 2 + 2 + 1));
    const auto quad = [&](std::size_t index) { return &drawn.uiVertices[index * 4]; };
    CHECK(quad(0)[0].position.x == Catch::Approx(50.0f));
    CHECK(quad(0)[0].color.w == Catch::Approx(0.25f));
    // Behind: the whole height, in the colour of each value at half its opacity.
    CHECK(quad(1)[0].position.y == Catch::Approx(0.0f));
    CHECK(quad(1)[0].color == Vec4{1.0f, 0.0f, 0.0f, 0.5f});
    CHECK(quad(2)[0].color == Vec4{0.0f, 1.0f, 0.0f, 0.5f});
    // In front: each bar in its colour.
    CHECK(quad(3)[0].position.y == Catch::Approx(25.0f));
    CHECK(quad(3)[0].color == red);
    CHECK(quad(4)[0].color == green);
    // The guide at half the height, a unit thick; the one past the top is left out.
    CHECK(quad(5)[0].position.y == Catch::Approx(24.5f));
    CHECK(quad(5)[2].position.y == Catch::Approx(25.5f));
    CHECK(quad(5)[0].color == Vec4{0.0f, 0.0f, 1.0f, 1.0f});
}

TEST_CASE("The value of a plot under the pointer is the bar under it, or the nearest point of its line", "[ui][world]")
{
    Scene scene;
    const Entity element = placedElement(scene, {100.0f, 100.0f}, {200.0f, 150.0f});
    scene.add<devex::scene::UiPlot>(element, devex::scene::UiPlot{.values = {1.0f, 2.0f, 3.0f, 4.0f}, .kind = devex::scene::UiPlotKind::Bars});
    UiWorld world;
    const auto at = [&](Vec2 pointer) {
        world.update(scene, window, UiInput{.pointer = pointer}, frame);
        return world.plotValueAt(scene, element);
    };
    // Four bars of 25 units each.
    CHECK(at({101.0f, 120.0f}) == 0);
    CHECK(at({160.0f, 120.0f}) == 2);
    CHECK(at({199.0f, 149.0f}) == 3);
    CHECK(at({90.0f, 120.0f}) == -1);
    // The points of a line stand at 0, 33, 67 and 100 units across.
    scene.get<devex::scene::UiPlot>(element).kind = devex::scene::UiPlotKind::Line;
    CHECK(at({110.0f, 120.0f}) == 0);
    CHECK(at({160.0f, 120.0f}) == 2);
    // An element that is not a plot has no value.
    CHECK(world.plotValueAt(scene, scene.parent(element)) == -1);
}
