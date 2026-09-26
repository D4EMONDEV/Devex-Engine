#include <devex/platform/Input.hpp>
#include <devex/platform/InputSource.hpp>

#include <catch2/catch_test_macros.hpp>

using devex::math::Vec2;
using devex::platform::Input;
using devex::platform::Key;
using devex::platform::MouseButton;

TEST_CASE("Input sources read and write their names", "[platform][input]")
{
    using devex::platform::InputDevice;
    CHECK(devex::platform::parseInputSource("key:Space") == devex::platform::keySource(Key::Space));
    CHECK(devex::platform::parseInputSource("mouse:Right") == devex::platform::mouseSource(MouseButton::Right));
    CHECK(devex::platform::parseInputSource("pad:South") ==
          devex::platform::gamepadSource(devex::platform::GamepadButton::South));
    CHECK(devex::platform::parseInputSource("axis:LeftTrigger") ==
          devex::platform::gamepadSource(devex::platform::GamepadAxis::LeftTrigger));
    CHECK(devex::platform::parseInputSource("stick:Right") ==
          devex::platform::InputSource{InputDevice::GamepadStick, devex::platform::rightStick});
    CHECK_FALSE(devex::platform::parseInputSource("key:Nothing").has_value());
    CHECK_FALSE(devex::platform::parseInputSource("wheel:Up").has_value());
    CHECK_FALSE(devex::platform::parseInputSource("Space").has_value());

    // Every source reads back from its name.
    for (const InputDevice device : {InputDevice::Key, InputDevice::MouseButton, InputDevice::GamepadButton,
                                     InputDevice::GamepadAxis, InputDevice::GamepadStick})
    {
        for (const devex::platform::InputSource source : devex::platform::inputSources(device))
        {
            CHECK(devex::platform::parseInputSource(devex::platform::toString(source)) == source);
            CHECK_FALSE(devex::platform::displayName(source).empty());
        }
    }
    CHECK(devex::platform::inputSources(InputDevice::Key).size() > 100);

    CHECK(devex::platform::displayName(devex::platform::keySource(Key::LeftShift)) == "Left Shift");
    CHECK(devex::platform::displayName(devex::platform::mouseSource(MouseButton::Left)) == "Left Mouse Button");
    CHECK(devex::platform::displayName(devex::platform::gamepadSource(devex::platform::GamepadAxis::RightX)) ==
          "Right Stick X");
    CHECK(devex::platform::isAnalog(*devex::platform::parseInputSource("axis:RightTrigger")));
    CHECK(devex::platform::isKeyboardOrMouse(*devex::platform::parseInputSource("mouse:X1")));
}

TEST_CASE("Key presses and releases last one frame", "[platform][input]")
{
    Input input;

    input.beginFrame();
    input.setKeyDown(Key::W, true);
    CHECK(input.isKeyDown(Key::W));
    CHECK(input.wasKeyPressed(Key::W));
    CHECK_FALSE(input.wasKeyReleased(Key::W));

    input.beginFrame();
    CHECK(input.isKeyDown(Key::W));
    CHECK_FALSE(input.wasKeyPressed(Key::W));

    input.setKeyDown(Key::W, false);
    CHECK_FALSE(input.isKeyDown(Key::W));
    CHECK(input.wasKeyReleased(Key::W));
}

TEST_CASE("A tap shorter than a frame is still reported", "[platform][input]")
{
    Input input;

    input.beginFrame();
    input.setKeyDown(Key::Space, true);
    input.setKeyDown(Key::Space, false);

    CHECK_FALSE(input.isKeyDown(Key::Space));
    CHECK(input.wasKeyPressed(Key::Space));
    CHECK(input.wasKeyReleased(Key::Space));
}

TEST_CASE("Holding a key does not trigger new presses", "[platform][input]")
{
    Input input;
    input.setKeyDown(Key::A, true);

    input.beginFrame();
    input.setKeyDown(Key::A, true);

    CHECK(input.isKeyDown(Key::A));
    CHECK_FALSE(input.wasKeyPressed(Key::A));
}

TEST_CASE("Mouse buttons follow the same transitions as keys", "[platform][input]")
{
    Input input;

    input.setMouseButtonDown(MouseButton::Right, true);
    CHECK(input.isMouseButtonDown(MouseButton::Right));
    CHECK(input.wasMouseButtonPressed(MouseButton::Right));
    CHECK_FALSE(input.isMouseButtonDown(MouseButton::Left));

    input.beginFrame();
    input.setMouseButtonDown(MouseButton::Right, false);
    CHECK(input.wasMouseButtonReleased(MouseButton::Right));
}

TEST_CASE("Mouse motion and wheel accumulate during a frame", "[platform][input]")
{
    Input input;

    input.moveMouse({10.0f, 5.0f}, {2.0f, 1.0f});
    input.moveMouse({13.0f, 9.0f}, {3.0f, 4.0f});
    input.scrollMouse({0.0f, 1.0f});
    input.scrollMouse({0.0f, 2.0f});

    CHECK(input.mousePosition() == Vec2{13.0f, 9.0f});
    CHECK(input.mouseDelta() == Vec2{5.0f, 5.0f});
    CHECK(input.mouseWheel() == Vec2{0.0f, 3.0f});

    input.beginFrame();
    CHECK(input.mousePosition() == Vec2{13.0f, 9.0f});
    CHECK(input.mouseDelta() == Vec2{0.0f, 0.0f});
    CHECK(input.mouseWheel() == Vec2{0.0f, 0.0f});
}

TEST_CASE("Releasing all inputs reports releases for held ones only", "[platform][input]")
{
    Input input;
    input.setKeyDown(Key::LeftShift, true);
    input.setMouseButtonDown(MouseButton::Left, true);

    input.beginFrame();
    input.releaseAll();

    CHECK_FALSE(input.isKeyDown(Key::LeftShift));
    CHECK(input.wasKeyReleased(Key::LeftShift));
    CHECK_FALSE(input.wasKeyReleased(Key::W));
    CHECK_FALSE(input.isMouseButtonDown(MouseButton::Left));
    CHECK(input.wasMouseButtonReleased(MouseButton::Left));
}

TEST_CASE("Keys outside the tracked range are ignored", "[platform][input]")
{
    Input input;
    const auto untracked = static_cast<Key>(devex::platform::keyCount + 10);

    input.setKeyDown(untracked, true);

    CHECK_FALSE(input.isKeyDown(untracked));
    CHECK_FALSE(input.wasKeyPressed(untracked));
}

TEST_CASE("Typing is kept apart from the keys, for one frame", "[platform][input]")
{
    Input input;
    input.beginFrame();
    input.addTypedText("h\xc3\xa9");
    input.addTypedText("!");
    CHECK(input.typedText() == "h\xc3\xa9!");

    // A key held down repeats without being pressed again.
    input.setKeyDown(Key::Backspace, true);
    input.beginFrame();
    input.repeatKey(Key::Backspace);
    CHECK(input.wasKeyRepeated(Key::Backspace));
    CHECK_FALSE(input.wasKeyPressed(Key::Backspace));

    input.beginFrame();
    CHECK(input.typedText().empty());
    CHECK_FALSE(input.wasKeyRepeated(Key::Backspace));
}

TEST_CASE("Shortcuts read the letter the layout prints, not the place of the key",
          "[platform][input]")
{
    Input input;
    input.beginFrame();
    // On a French keyboard the key that writes A sits where Q is in the US: the place is Q, the
    // letter is A, and Ctrl+A must select everything.
    input.setKeyDown(Key::Q, true);
    input.pressLetter('a');
    CHECK(input.wasKeyPressed(Key::Q));
    CHECK(input.wasLetterPressed('a'));
    CHECK_FALSE(input.wasLetterPressed('q'));
    CHECK_FALSE(input.wasLetterPressed('A'));

    input.beginFrame();
    CHECK_FALSE(input.wasLetterPressed('a'));
}
