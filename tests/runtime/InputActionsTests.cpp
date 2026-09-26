#include <devex/runtime/InputActions.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using Catch::Approx;
using devex::asset::InputActionKind;
using devex::asset::InputDirection;
using devex::asset::InputSettings;
using devex::math::Vec2;
using devex::platform::GamepadAxis;
using devex::platform::GamepadButton;
using devex::platform::Input;
using devex::platform::Key;
using devex::runtime::InputActions;

namespace {

// Jump on Space and the south button, Throttle on W and S or the triggers, Move on the keys and
// the left stick, Fire in the Combat context, which starts off.
[[nodiscard]] InputSettings settings()
{
    return InputSettings{
        .contexts = {{.name = "Gameplay"}, {.name = "Combat", .activeAtStart = false}},
        .actions =
            {
                {.name = "Jump", .context = "Gameplay", .bindings = {{"key:Space"}, {"pad:South"}}},
                {.name = "Throttle",
                 .kind = InputActionKind::Axis,
                 .context = "Gameplay",
                 .deadZone = 0.2f,
                 .bindings = {{"key:W"}, {"key:S", InputDirection::Negative}, {"axis:RightTrigger"},
                              {"axis:LeftTrigger", InputDirection::Negative}}},
                {.name = "Move",
                 .kind = InputActionKind::Vector,
                 .bindings = {{"key:W", InputDirection::Up},
                              {"key:S", InputDirection::Down},
                              {"key:A", InputDirection::Left},
                              {"key:D", InputDirection::Right},
                              {"stick:Left"}}},
                {.name = "Fire", .context = "Combat", .bindings = {{"mouse:Left"}, {"axis:RightTrigger"}}},
            },
    };
}

void pressKey(Input& input, Key key, bool down)
{
    input.beginFrame();
    input.setKeyDown(key, down);
}

} // namespace

TEST_CASE("Button actions follow any of their bindings, for one frame on each change", "[runtime][input]")
{
    InputActions actions(settings());
    Input input;
    input.setGamepadConnected(0, true);

    pressKey(input, Key::Space, true);
    actions.update(input, false);
    CHECK(actions.isDown("Jump"));
    CHECK(actions.wasPressed("Jump"));
    CHECK(actions.axis("Jump") == 1.0f);

    input.beginFrame();
    actions.update(input, false);
    CHECK(actions.isDown("Jump"));
    CHECK_FALSE(actions.wasPressed("Jump"));

    // Another binding keeps it down while the first is let go.
    input.beginFrame();
    input.setGamepadButtonDown(GamepadButton::South, 0, true);
    input.setKeyDown(Key::Space, false);
    actions.update(input, false);
    CHECK(actions.isDown("Jump"));
    CHECK_FALSE(actions.wasReleased("Jump"));

    input.beginFrame();
    input.setGamepadButtonDown(GamepadButton::South, 0, false);
    actions.update(input, false);
    CHECK_FALSE(actions.isDown("Jump"));
    CHECK(actions.wasReleased("Jump"));

    // Unknown actions read as nothing.
    CHECK_FALSE(actions.hasAction("Fly"));
    CHECK_FALSE(actions.isDown("Fly"));
    CHECK(actions.vector("Fly") == Vec2{0.0f});
}

TEST_CASE("Axis actions add their keys and axes, past their dead zone", "[runtime][input]")
{
    InputActions actions(settings());
    Input input;
    input.setGamepadConnected(1, true);

    pressKey(input, Key::W, true);
    actions.update(input, false);
    CHECK(actions.axis("Throttle") == 1.0f);
    CHECK(actions.isDown("Throttle"));

    // Opposite keys cancel out.
    input.beginFrame();
    input.setKeyDown(Key::S, true);
    actions.update(input, false);
    CHECK(actions.axis("Throttle") == 0.0f);

    // A trigger of the second gamepad, below then past the dead zone, which spreads the rest.
    input.beginFrame();
    input.setKeyDown(Key::W, false);
    input.setKeyDown(Key::S, false);
    input.setGamepadAxis(GamepadAxis::LeftTrigger, 1, 0.1f);
    actions.update(input, false);
    CHECK(actions.axis("Throttle") == 0.0f);
    input.setGamepadAxis(GamepadAxis::LeftTrigger, 1, 0.6f);
    actions.update(input, false);
    CHECK(actions.axis("Throttle") == Approx(-0.5f));
    CHECK(actions.vector("Throttle").x == Approx(-0.5f));
    CHECK(actions.isDown("Throttle"));
}

TEST_CASE("Vector actions go no further than one, with y up", "[runtime][input]")
{
    InputActions actions(settings());
    Input input;
    input.setGamepadConnected(0, true);

    // Two keys at once go no faster than one.
    input.beginFrame();
    input.setKeyDown(Key::W, true);
    input.setKeyDown(Key::D, true);
    actions.update(input, false);
    const Vec2 diagonal = actions.vector("Move");
    CHECK(diagonal.x == Approx(std::sqrt(0.5f)));
    CHECK(diagonal.y == Approx(std::sqrt(0.5f)));

    // A stick pushed up and halfway, whose y points down on the gamepad.
    input.beginFrame();
    input.setKeyDown(Key::W, false);
    input.setKeyDown(Key::D, false);
    input.setGamepadAxis(GamepadAxis::LeftY, 0, -1.0f);
    actions.update(input, false);
    CHECK(actions.vector("Move").x == Approx(0.0f));
    CHECK(actions.vector("Move").y == Approx(1.0f));
    CHECK(actions.axis("Move") == 0.0f);
}

TEST_CASE("Actions of an inactive context read as released, and text typed moves nothing", "[runtime][input]")
{
    InputActions actions(settings());
    Input input;
    input.setGamepadConnected(0, true);

    // Combat starts off: the trigger fires nothing until it is turned on.
    input.beginFrame();
    input.setGamepadAxis(GamepadAxis::RightTrigger, 0, 0.9f);
    actions.update(input, false);
    CHECK_FALSE(actions.isContextActive("Combat"));
    CHECK_FALSE(actions.isDown("Fire"));
    CHECK(actions.setContextActive("Combat", true));
    actions.update(input, false);
    CHECK(actions.isDown("Fire"));
    CHECK(actions.wasPressed("Fire"));
    CHECK_FALSE(actions.setContextActive("Menu", true));

    // Turning Gameplay off releases Jump while its key is held.
    pressKey(input, Key::Space, true);
    actions.update(input, false);
    CHECK(actions.isDown("Jump"));
    CHECK(actions.setContextActive("Gameplay", false));
    input.beginFrame();
    actions.update(input, false);
    CHECK_FALSE(actions.isDown("Jump"));
    CHECK(actions.wasReleased("Jump"));
    CHECK(actions.setContextActive("Gameplay", true));

    // While a field of the interface takes text, keys count for nothing, but gamepads still do.
    input.beginFrame();
    input.setGamepadAxis(GamepadAxis::RightTrigger, 0, 0.0f);
    input.setKeyDown(Key::D, true);
    actions.update(input, true);
    CHECK_FALSE(actions.isDown("Jump"));
    CHECK(actions.vector("Move") == Vec2{0.0f});
    input.setGamepadButtonDown(GamepadButton::South, 0, true);
    actions.update(input, true);
    CHECK(actions.isDown("Jump"));
}

TEST_CASE("Players bind actions to other keys, which are saved apart from the project", "[runtime][input]")
{
    InputActions actions(settings());
    actions.setKeyLabeler([](Key key) { return key == Key::Enter ? std::string("Entrée") : std::string(); });
    Input input;
    input.setGamepadConnected(0, true);

    // The keyboard binding of Jump waits for a key: gamepad buttons do not answer it, and actions
    // read as released meanwhile.
    CHECK(actions.listen("Jump", 0));
    CHECK(actions.isListening());
    input.beginFrame();
    input.setGamepadButtonDown(GamepadButton::East, 0, true);
    actions.update(input, false);
    CHECK(actions.isListening());
    CHECK_FALSE(actions.tookInput());

    pressKey(input, Key::Enter, true);
    actions.update(input, false);
    CHECK_FALSE(actions.isListening());
    CHECK(actions.tookInput());
    // The key that answered does not press the action it now plays.
    CHECK_FALSE(actions.wasPressed("Jump"));
    CHECK(actions.binding("Jump", 0) == devex::platform::keySource(Key::Enter));
    CHECK(actions.bindingLabel("Jump", 0) == "Entrée");
    CHECK(actions.bindingLabel("Jump", 1) == "Gamepad South");
    CHECK(actions.takeChanges());
    CHECK_FALSE(actions.takeChanges());

    pressKey(input, Key::Space, false);
    input.setKeyDown(Key::Enter, false);
    actions.update(input, false);
    pressKey(input, Key::Enter, true);
    actions.update(input, false);
    CHECK(actions.wasPressed("Jump"));

    // The gamepad binding takes a gamepad button; Escape gives up.
    CHECK(actions.listen("Jump", 1));
    pressKey(input, Key::Escape, true);
    actions.update(input, false);
    CHECK_FALSE(actions.isListening());
    CHECK(actions.tookInput());
    CHECK(actions.binding("Jump", 1) == devex::platform::gamepadSource(GamepadButton::South));
    CHECK_FALSE(actions.listen("Jump", 2));
    CHECK_FALSE(actions.listen("Fly", 0));

    // Only what changed is written, and read back into the same project.
    CHECK(actions.rebind("Move", 3, devex::platform::keySource(Key::L)));
    const std::string saved = actions.writeOverrides();
    CHECK(saved.find("key:Enter") != std::string::npos);
    CHECK(saved.find("key:A") == std::string::npos);
    InputActions again(settings());
    again.readOverrides(saved);
    CHECK(again.binding("Jump", 0) == devex::platform::keySource(Key::Enter));
    CHECK(again.binding("Move", 3) == devex::platform::keySource(Key::L));
    CHECK_FALSE(again.takeChanges());

    // Bindings the project no longer has are skipped.
    InputActions other(InputSettings{.actions = {{.name = "Jump", .bindings = {{"key:Space"}}}}});
    other.readOverrides(saved + "[binding action=\"Jump\" index=4 input=\"key:X\"]\n");
    CHECK(other.binding("Jump", 0) == devex::platform::keySource(Key::Enter));
    CHECK(other.bindingCount("Jump") == 1);

    // Everything back as the project says.
    actions.resetBindings();
    CHECK(actions.binding("Jump", 0) == devex::platform::keySource(Key::Space));
    CHECK(actions.binding("Move", 3) == devex::platform::keySource(Key::D));
    CHECK(actions.takeChanges());
}
