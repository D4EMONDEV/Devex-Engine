#pragma once

#include <devex/math/Math.hpp>
#include <devex/platform/Key.hpp>

#include <array>
#include <bitset>
#include <cstddef>
#include <string>
#include <string_view>

namespace devex::platform {

// Keyboard and mouse state for the current frame. "Pressed" and "released" transitions last
// exactly one frame, so read them from onUpdate rather than onFixedUpdate.
class Input
{
public:
    [[nodiscard]] bool isKeyDown(Key key) const noexcept;
    [[nodiscard]] bool wasKeyPressed(Key key) const noexcept;
    [[nodiscard]] bool wasKeyReleased(Key key) const noexcept;
    // Whether the system repeated the key this frame, as it does while it is held down. A field
    // that is typed into erases and moves on the repeats as well as on the first press.
    [[nodiscard]] bool wasKeyRepeated(Key key) const noexcept;
    // Whether the key printed with this letter, a to z, was pressed or repeated this frame. Keys
    // are places, which suits moving; the shortcuts of text follow the letters instead, so that
    // Ctrl+A selects everything on a French keyboard too, where A is not where it is in the US.
    [[nodiscard]] bool wasLetterPressed(char letter) const noexcept;

    [[nodiscard]] bool isMouseButtonDown(MouseButton button) const noexcept;
    [[nodiscard]] bool wasMouseButtonPressed(MouseButton button) const noexcept;
    [[nodiscard]] bool wasMouseButtonReleased(MouseButton button) const noexcept;

    // Cursor position in window coordinates.
    [[nodiscard]] math::Vec2 mousePosition() const noexcept;
    // Motion accumulated this frame, also reported while the mouse is captured.
    [[nodiscard]] math::Vec2 mouseDelta() const noexcept;
    // Wheel scrolling accumulated this frame; positive y scrolls away from the user.
    [[nodiscard]] math::Vec2 mouseWheel() const noexcept;

    // What was typed this frame, in UTF-8, while text input is on. Keys and characters are two
    // different things: a key is a place on the keyboard, a character is what the layout, the
    // modifiers and the input method make of it.
    [[nodiscard]] const std::string& typedText() const noexcept;

    // Gamepads are numbered in the order they were plugged in, and keep their number until they
    // are unplugged. Asking about a pad that is not there answers as if nothing were pressed.
    [[nodiscard]] bool isGamepadConnected(std::size_t pad = 0) const noexcept;
    [[nodiscard]] std::size_t gamepadCountConnected() const noexcept;
    [[nodiscard]] bool isGamepadButtonDown(GamepadButton button, std::size_t pad = 0) const noexcept;
    [[nodiscard]] bool wasGamepadButtonPressed(GamepadButton button,
                                               std::size_t pad = 0) const noexcept;
    [[nodiscard]] bool wasGamepadButtonReleased(GamepadButton button,
                                                std::size_t pad = 0) const noexcept;
    // Sticks are already past their dead zone, so a stick at rest reads exactly zero.
    [[nodiscard]] float gamepadAxis(GamepadAxis axis, std::size_t pad = 0) const noexcept;
    [[nodiscard]] math::Vec2 gamepadLeftStick(std::size_t pad = 0) const noexcept;
    [[nodiscard]] math::Vec2 gamepadRightStick(std::size_t pad = 0) const noexcept;

    // State updates, driven by the platform layer.
    void beginFrame() noexcept;
    void setKeyDown(Key key, bool down) noexcept;
    void repeatKey(Key key) noexcept;
    void pressLetter(char letter) noexcept;
    void setMouseButtonDown(MouseButton button, bool down) noexcept;
    void setGamepadConnected(std::size_t pad, bool connected) noexcept;
    void setGamepadButtonDown(GamepadButton button, std::size_t pad, bool down) noexcept;
    void setGamepadAxis(GamepadAxis axis, std::size_t pad, float value) noexcept;
    void moveMouse(math::Vec2 position, math::Vec2 delta) noexcept;
    void scrollMouse(math::Vec2 delta) noexcept;
    void addTypedText(std::string_view text);
    // Releases every key and button, for instance when the window loses focus.
    void releaseAll() noexcept;

private:
    std::bitset<keyCount> m_keysDown;
    std::bitset<keyCount> m_keysPressed;
    std::bitset<keyCount> m_keysReleased;
    std::bitset<keyCount> m_keysRepeated;
    std::bitset<26> m_lettersPressed;
    std::bitset<mouseButtonCount> m_buttonsDown;
    std::bitset<mouseButtonCount> m_buttonsPressed;
    std::bitset<mouseButtonCount> m_buttonsReleased;
    math::Vec2 m_mousePosition{0.0f};
    math::Vec2 m_mouseDelta{0.0f};
    math::Vec2 m_mouseWheel{0.0f};
    std::string m_typedText;

    struct Gamepad
    {
        bool connected = false;
        std::bitset<gamepadButtonCount> down;
        std::bitset<gamepadButtonCount> pressed;
        std::bitset<gamepadButtonCount> released;
        std::array<float, gamepadAxisCount> axes{};
    };
    std::array<Gamepad, gamepadCount> m_gamepads;
};

} // namespace devex::platform
