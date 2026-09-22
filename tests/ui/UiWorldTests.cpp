#include <devex/asset/Artifact.hpp>
#include <devex/asset/ThemeData.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/UiWorld.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using devex::core::Duration;
using devex::math::Vec2;
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
    devex::asset::ThemeData theme;
    theme.styles.push_back(
        {.name = "box",
         .values = {{.component = "UiImage",
                     .field = "color",
                     .value = "vec4(0.25, 0.5, 0.75, 1)"},
                    {.component = "UiToggle", .field = "check_size", .value = "0.8"},
                    // A component the element does not carry is left alone.
                    {.component = "UiSlider", .field = "value", .value = "0.5"}}});

    UiWorld world;
    world.setThemes([&theme](devex::asset::AssetId) { return &theme; });
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
