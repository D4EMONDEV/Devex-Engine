#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/UiWorld.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
