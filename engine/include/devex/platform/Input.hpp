#pragma once

#include <devex/math/Math.hpp>
#include <devex/platform/Key.hpp>

#include <bitset>

namespace devex::platform {

// Keyboard and mouse state for the current frame. "Pressed" and "released" transitions last
// exactly one frame, so read them from onUpdate rather than onFixedUpdate.
class Input
{
public:
    [[nodiscard]] bool isKeyDown(Key key) const noexcept;
    [[nodiscard]] bool wasKeyPressed(Key key) const noexcept;
    [[nodiscard]] bool wasKeyReleased(Key key) const noexcept;

    [[nodiscard]] bool isMouseButtonDown(MouseButton button) const noexcept;
    [[nodiscard]] bool wasMouseButtonPressed(MouseButton button) const noexcept;
    [[nodiscard]] bool wasMouseButtonReleased(MouseButton button) const noexcept;

    // Cursor position in window coordinates.
    [[nodiscard]] math::Vec2 mousePosition() const noexcept;
    // Motion accumulated this frame, also reported while the mouse is captured.
    [[nodiscard]] math::Vec2 mouseDelta() const noexcept;
    // Wheel scrolling accumulated this frame; positive y scrolls away from the user.
    [[nodiscard]] math::Vec2 mouseWheel() const noexcept;

    // State updates, driven by the platform layer.
    void beginFrame() noexcept;
    void setKeyDown(Key key, bool down) noexcept;
    void setMouseButtonDown(MouseButton button, bool down) noexcept;
    void moveMouse(math::Vec2 position, math::Vec2 delta) noexcept;
    void scrollMouse(math::Vec2 delta) noexcept;
    // Releases every key and button, for instance when the window loses focus.
    void releaseAll() noexcept;

private:
    std::bitset<keyCount> m_keysDown;
    std::bitset<keyCount> m_keysPressed;
    std::bitset<keyCount> m_keysReleased;
    std::bitset<mouseButtonCount> m_buttonsDown;
    std::bitset<mouseButtonCount> m_buttonsPressed;
    std::bitset<mouseButtonCount> m_buttonsReleased;
    math::Vec2 m_mousePosition{0.0f};
    math::Vec2 m_mouseDelta{0.0f};
    math::Vec2 m_mouseWheel{0.0f};
};

} // namespace devex::platform
