#pragma once

#include <devex/platform/Key.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::platform {

// The kind of control an input source is.
enum class InputDevice : std::uint8_t
{
    Key,
    MouseButton,
    GamepadButton,
    // One axis of a stick, or a trigger.
    GamepadAxis,
    // A whole stick, which gives a direction.
    GamepadStick,
};

inline constexpr std::uint16_t leftStick = 0;
inline constexpr std::uint16_t rightStick = 1;

// A control the player presses or moves, which input actions are bound to.
struct InputSource
{
    InputDevice device = InputDevice::Key;
    // The value of the Key, MouseButton, GamepadButton or GamepadAxis, or leftStick and rightStick.
    std::uint16_t code = 0;

    bool operator==(const InputSource&) const = default;
};

[[nodiscard]] constexpr InputSource keySource(Key key) noexcept
{
    return {InputDevice::Key, static_cast<std::uint16_t>(key)};
}

[[nodiscard]] constexpr InputSource mouseSource(MouseButton button) noexcept
{
    return {InputDevice::MouseButton, static_cast<std::uint16_t>(button)};
}

[[nodiscard]] constexpr InputSource gamepadSource(GamepadButton button) noexcept
{
    return {InputDevice::GamepadButton, static_cast<std::uint16_t>(button)};
}

[[nodiscard]] constexpr InputSource gamepadSource(GamepadAxis axis) noexcept
{
    return {InputDevice::GamepadAxis, static_cast<std::uint16_t>(axis)};
}

// Reads "key:Space", "mouse:Left", "pad:South", "axis:LeftTrigger" or "stick:Left". Keys are named
// after their place on a US keyboard, as Key is: "key:W" is the key labelled Z on AZERTY.
[[nodiscard]] std::optional<InputSource> parseInputSource(std::string_view text);
[[nodiscard]] std::string toString(InputSource source);
// A name to show, which does not follow the layout of the keyboard: "Left Shift", "Left Mouse
// Button", "Gamepad South", "Left Trigger", "Left Stick". Platform::keyLabel names keys as the
// layout prints them.
[[nodiscard]] std::string displayName(InputSource source);
// Every source of a device, for the lists a binding is chosen from.
[[nodiscard]] std::vector<InputSource> inputSources(InputDevice device);

// Whether the source moves over a range, as axes and sticks do, rather than being pressed or not.
[[nodiscard]] constexpr bool isAnalog(InputSource source) noexcept
{
    return source.device == InputDevice::GamepadAxis || source.device == InputDevice::GamepadStick;
}

// Whether the source is on the keyboard or the mouse rather than on a gamepad.
[[nodiscard]] constexpr bool isKeyboardOrMouse(InputSource source) noexcept
{
    return source.device == InputDevice::Key || source.device == InputDevice::MouseButton;
}

} // namespace devex::platform
