#include <devex/platform/InputSource.hpp>

#include <array>
#include <cctype>
#include <utility>

namespace devex::platform {
namespace {

constexpr std::array<std::pair<Key, std::string_view>, 106> keyNames{{
    {Key::A, "A"},
    {Key::B, "B"},
    {Key::C, "C"},
    {Key::D, "D"},
    {Key::E, "E"},
    {Key::F, "F"},
    {Key::G, "G"},
    {Key::H, "H"},
    {Key::I, "I"},
    {Key::J, "J"},
    {Key::K, "K"},
    {Key::L, "L"},
    {Key::M, "M"},
    {Key::N, "N"},
    {Key::O, "O"},
    {Key::P, "P"},
    {Key::Q, "Q"},
    {Key::R, "R"},
    {Key::S, "S"},
    {Key::T, "T"},
    {Key::U, "U"},
    {Key::V, "V"},
    {Key::W, "W"},
    {Key::X, "X"},
    {Key::Y, "Y"},
    {Key::Z, "Z"},
    {Key::Digit1, "Digit1"},
    {Key::Digit2, "Digit2"},
    {Key::Digit3, "Digit3"},
    {Key::Digit4, "Digit4"},
    {Key::Digit5, "Digit5"},
    {Key::Digit6, "Digit6"},
    {Key::Digit7, "Digit7"},
    {Key::Digit8, "Digit8"},
    {Key::Digit9, "Digit9"},
    {Key::Digit0, "Digit0"},
    {Key::Enter, "Enter"},
    {Key::Escape, "Escape"},
    {Key::Backspace, "Backspace"},
    {Key::Tab, "Tab"},
    {Key::Space, "Space"},
    {Key::Minus, "Minus"},
    {Key::Equals, "Equals"},
    {Key::LeftBracket, "LeftBracket"},
    {Key::RightBracket, "RightBracket"},
    {Key::Backslash, "Backslash"},
    {Key::NonUsHash, "NonUsHash"},
    {Key::Semicolon, "Semicolon"},
    {Key::Apostrophe, "Apostrophe"},
    {Key::Grave, "Grave"},
    {Key::Comma, "Comma"},
    {Key::Period, "Period"},
    {Key::Slash, "Slash"},
    {Key::CapsLock, "CapsLock"},
    {Key::F1, "F1"},
    {Key::F2, "F2"},
    {Key::F3, "F3"},
    {Key::F4, "F4"},
    {Key::F5, "F5"},
    {Key::F6, "F6"},
    {Key::F7, "F7"},
    {Key::F8, "F8"},
    {Key::F9, "F9"},
    {Key::F10, "F10"},
    {Key::F11, "F11"},
    {Key::F12, "F12"},
    {Key::PrintScreen, "PrintScreen"},
    {Key::ScrollLock, "ScrollLock"},
    {Key::Pause, "Pause"},
    {Key::Insert, "Insert"},
    {Key::Home, "Home"},
    {Key::PageUp, "PageUp"},
    {Key::Delete, "Delete"},
    {Key::End, "End"},
    {Key::PageDown, "PageDown"},
    {Key::Right, "Right"},
    {Key::Left, "Left"},
    {Key::Down, "Down"},
    {Key::Up, "Up"},
    {Key::NumLock, "NumLock"},
    {Key::KeypadDivide, "KeypadDivide"},
    {Key::KeypadMultiply, "KeypadMultiply"},
    {Key::KeypadMinus, "KeypadMinus"},
    {Key::KeypadPlus, "KeypadPlus"},
    {Key::KeypadEnter, "KeypadEnter"},
    {Key::Keypad1, "Keypad1"},
    {Key::Keypad2, "Keypad2"},
    {Key::Keypad3, "Keypad3"},
    {Key::Keypad4, "Keypad4"},
    {Key::Keypad5, "Keypad5"},
    {Key::Keypad6, "Keypad6"},
    {Key::Keypad7, "Keypad7"},
    {Key::Keypad8, "Keypad8"},
    {Key::Keypad9, "Keypad9"},
    {Key::Keypad0, "Keypad0"},
    {Key::KeypadPeriod, "KeypadPeriod"},
    {Key::NonUsBackslash, "NonUsBackslash"},
    {Key::Application, "Application"},
    {Key::LeftControl, "LeftControl"},
    {Key::LeftShift, "LeftShift"},
    {Key::LeftAlt, "LeftAlt"},
    {Key::LeftSuper, "LeftSuper"},
    {Key::RightControl, "RightControl"},
    {Key::RightShift, "RightShift"},
    {Key::RightAlt, "RightAlt"},
    {Key::RightSuper, "RightSuper"},
}};

constexpr std::array<std::string_view, mouseButtonCount> mouseNames{"Left", "Middle", "Right", "X1", "X2"};

constexpr std::array<std::string_view, gamepadButtonCount> gamepadButtonNames{
    "South",     "East",         "West",          "North",  "Back",     "Guide",    "Start",     "LeftStick",
    "RightStick", "LeftShoulder", "RightShoulder", "DpadUp", "DpadDown", "DpadLeft", "DpadRight",
};

constexpr std::array<std::string_view, gamepadAxisCount> gamepadAxisNames{
    "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger",
};

constexpr std::array<std::string_view, 2> stickNames{"Left", "Right"};

struct DevicePrefix
{
    InputDevice device;
    std::string_view prefix;
};

constexpr std::array devicePrefixes{
    DevicePrefix{InputDevice::Key, "key"},
    DevicePrefix{InputDevice::MouseButton, "mouse"},
    DevicePrefix{InputDevice::GamepadButton, "pad"},
    DevicePrefix{InputDevice::GamepadAxis, "axis"},
    DevicePrefix{InputDevice::GamepadStick, "stick"},
};

[[nodiscard]] std::string_view nameOf(InputSource source) noexcept
{
    switch (source.device)
    {
    case InputDevice::Key:
        for (const auto& [key, name] : keyNames)
        {
            if (static_cast<std::uint16_t>(key) == source.code)
            {
                return name;
            }
        }
        return {};
    case InputDevice::MouseButton:
        return source.code < mouseNames.size() ? mouseNames[source.code] : std::string_view{};
    case InputDevice::GamepadButton:
        return source.code < gamepadButtonNames.size() ? gamepadButtonNames[source.code] : std::string_view{};
    case InputDevice::GamepadAxis:
        return source.code < gamepadAxisNames.size() ? gamepadAxisNames[source.code] : std::string_view{};
    case InputDevice::GamepadStick:
        return source.code < stickNames.size() ? stickNames[source.code] : std::string_view{};
    }
    return {};
}

// "LeftShoulder" becomes "Left Shoulder", "Digit1" becomes "Digit 1".
[[nodiscard]] std::string spacedWords(std::string_view name)
{
    std::string spaced;
    for (std::size_t index = 0; index < name.size(); ++index)
    {
        const auto character = static_cast<unsigned char>(name[index]);
        const auto previous = index > 0 ? static_cast<unsigned char>(name[index - 1]) : 0;
        const bool startsWord = index > 0 && ((std::isupper(character) != 0 && std::isupper(previous) == 0) ||
                                              (std::isdigit(character) != 0 && std::isdigit(previous) == 0));
        if (startsWord)
        {
            spaced.push_back(' ');
        }
        spaced.push_back(name[index]);
    }
    return spaced;
}

} // namespace

std::optional<InputSource> parseInputSource(std::string_view text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::string_view prefix = text.substr(0, colon);
    const std::string_view name = text.substr(colon + 1);
    for (const DevicePrefix& device : devicePrefixes)
    {
        if (device.prefix != prefix)
        {
            continue;
        }
        for (const InputSource source : inputSources(device.device))
        {
            if (nameOf(source) == name)
            {
                return source;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string toString(InputSource source)
{
    const std::string_view name = nameOf(source);
    if (name.empty())
    {
        return {};
    }
    for (const DevicePrefix& device : devicePrefixes)
    {
        if (device.device == source.device)
        {
            return std::string(device.prefix) + ":" + std::string(name);
        }
    }
    return {};
}

std::string displayName(InputSource source)
{
    const std::string_view name = nameOf(source);
    switch (source.device)
    {
    case InputDevice::Key:
        return spacedWords(name);
    case InputDevice::MouseButton:
        return spacedWords(name) + " Mouse Button";
    case InputDevice::GamepadButton:
        if (name == "LeftStick" || name == "RightStick")
        {
            return spacedWords(name) + " Press";
        }
        return name.starts_with("Dpad") ? "D-pad " + std::string(name.substr(4)) : "Gamepad " + spacedWords(name);
    case InputDevice::GamepadAxis:
        if (name.ends_with("X") || name.ends_with("Y"))
        {
            return spacedWords(name.substr(0, name.size() - 1)) + " Stick " + std::string(name.substr(name.size() - 1));
        }
        return spacedWords(name);
    case InputDevice::GamepadStick:
        return std::string(name) + " Stick";
    }
    return std::string(name);
}

std::vector<InputSource> inputSources(InputDevice device)
{
    std::vector<InputSource> sources;
    switch (device)
    {
    case InputDevice::Key:
        for (const auto& [key, name] : keyNames)
        {
            sources.push_back(keySource(key));
        }
        break;
    case InputDevice::MouseButton:
        for (std::size_t code = 0; code < mouseNames.size(); ++code)
        {
            sources.push_back({device, static_cast<std::uint16_t>(code)});
        }
        break;
    case InputDevice::GamepadButton:
        for (std::size_t code = 0; code < gamepadButtonNames.size(); ++code)
        {
            sources.push_back({device, static_cast<std::uint16_t>(code)});
        }
        break;
    case InputDevice::GamepadAxis:
        for (std::size_t code = 0; code < gamepadAxisNames.size(); ++code)
        {
            sources.push_back({device, static_cast<std::uint16_t>(code)});
        }
        break;
    case InputDevice::GamepadStick:
        sources.push_back({device, leftStick});
        sources.push_back({device, rightStick});
        break;
    }
    return sources;
}

} // namespace devex::platform
