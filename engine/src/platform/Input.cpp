#include <devex/platform/Input.hpp>

namespace devex::platform {
namespace {

[[nodiscard]] std::size_t indexOf(Key key) noexcept
{
    return static_cast<std::size_t>(key);
}

[[nodiscard]] std::size_t indexOf(MouseButton button) noexcept
{
    return static_cast<std::size_t>(button);
}

[[nodiscard]] bool isTracked(Key key) noexcept
{
    return indexOf(key) < keyCount;
}

[[nodiscard]] bool isTracked(MouseButton button) noexcept
{
    return indexOf(button) < mouseButtonCount;
}

// Sticks rest a little off centre: anything closer than this counts as nothing, and the rest is
// stretched back over the whole travel so that a small push still starts at zero.
constexpr float stickDeadZone = 0.2f;

[[nodiscard]] float pastDeadZone(float value) noexcept
{
    if (value > -stickDeadZone && value < stickDeadZone)
    {
        return 0.0f;
    }
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    const float amount = (value < 0.0f ? -value : value) - stickDeadZone;
    return sign * amount / (1.0f - stickDeadZone);
}

template <std::size_t Count>
void applyTransition(std::bitset<Count>& down, std::bitset<Count>& pressed,
                     std::bitset<Count>& released, std::size_t index, bool isDown) noexcept
{
    if (down.test(index) == isDown)
    {
        return;
    }
    down.set(index, isDown);
    (isDown ? pressed : released).set(index);
}

} // namespace

bool Input::isKeyDown(Key key) const noexcept
{
    return isTracked(key) && m_keysDown.test(indexOf(key));
}

bool Input::wasKeyPressed(Key key) const noexcept
{
    return isTracked(key) && m_keysPressed.test(indexOf(key));
}

bool Input::wasKeyReleased(Key key) const noexcept
{
    return isTracked(key) && m_keysReleased.test(indexOf(key));
}

bool Input::isMouseButtonDown(MouseButton button) const noexcept
{
    return isTracked(button) && m_buttonsDown.test(indexOf(button));
}

bool Input::wasMouseButtonPressed(MouseButton button) const noexcept
{
    return isTracked(button) && m_buttonsPressed.test(indexOf(button));
}

bool Input::wasMouseButtonReleased(MouseButton button) const noexcept
{
    return isTracked(button) && m_buttonsReleased.test(indexOf(button));
}

math::Vec2 Input::mousePosition() const noexcept
{
    return m_mousePosition;
}

math::Vec2 Input::mouseDelta() const noexcept
{
    return m_mouseDelta;
}

math::Vec2 Input::mouseWheel() const noexcept
{
    return m_mouseWheel;
}

void Input::beginFrame() noexcept
{
    m_keysPressed.reset();
    m_keysReleased.reset();
    m_buttonsPressed.reset();
    m_buttonsReleased.reset();
    m_mouseDelta = math::Vec2{0.0f};
    m_mouseWheel = math::Vec2{0.0f};
    for (Gamepad& gamepad : m_gamepads)
    {
        gamepad.pressed.reset();
        gamepad.released.reset();
    }
}

bool Input::isGamepadConnected(std::size_t pad) const noexcept
{
    return pad < m_gamepads.size() && m_gamepads[pad].connected;
}

std::size_t Input::gamepadCountConnected() const noexcept
{
    std::size_t count = 0;
    for (const Gamepad& gamepad : m_gamepads)
    {
        count += gamepad.connected ? 1 : 0;
    }
    return count;
}

bool Input::isGamepadButtonDown(GamepadButton button, std::size_t pad) const noexcept
{
    return isGamepadConnected(pad) && m_gamepads[pad].down.test(static_cast<std::size_t>(button));
}

bool Input::wasGamepadButtonPressed(GamepadButton button, std::size_t pad) const noexcept
{
    return isGamepadConnected(pad) && m_gamepads[pad].pressed.test(static_cast<std::size_t>(button));
}

bool Input::wasGamepadButtonReleased(GamepadButton button, std::size_t pad) const noexcept
{
    return pad < m_gamepads.size() &&
           m_gamepads[pad].released.test(static_cast<std::size_t>(button));
}

float Input::gamepadAxis(GamepadAxis axis, std::size_t pad) const noexcept
{
    return isGamepadConnected(pad) ? m_gamepads[pad].axes[static_cast<std::size_t>(axis)] : 0.0f;
}

math::Vec2 Input::gamepadLeftStick(std::size_t pad) const noexcept
{
    return math::Vec2{gamepadAxis(GamepadAxis::LeftX, pad), gamepadAxis(GamepadAxis::LeftY, pad)};
}

math::Vec2 Input::gamepadRightStick(std::size_t pad) const noexcept
{
    return math::Vec2{gamepadAxis(GamepadAxis::RightX, pad), gamepadAxis(GamepadAxis::RightY, pad)};
}

void Input::setGamepadConnected(std::size_t pad, bool connected) noexcept
{
    if (pad >= m_gamepads.size())
    {
        return;
    }
    // A pad that goes away releases what it held, so nothing stays pressed for ever.
    if (!connected)
    {
        m_gamepads[pad].released |= m_gamepads[pad].down;
        m_gamepads[pad].down.reset();
        m_gamepads[pad].axes.fill(0.0f);
    }
    m_gamepads[pad].connected = connected;
}

void Input::setGamepadButtonDown(GamepadButton button, std::size_t pad, bool down) noexcept
{
    if (pad < m_gamepads.size() && static_cast<std::size_t>(button) < gamepadButtonCount)
    {
        applyTransition(m_gamepads[pad].down, m_gamepads[pad].pressed, m_gamepads[pad].released,
                        static_cast<std::size_t>(button), down);
    }
}

void Input::setGamepadAxis(GamepadAxis axis, std::size_t pad, float value) noexcept
{
    if (pad >= m_gamepads.size() || static_cast<std::size_t>(axis) >= gamepadAxisCount)
    {
        return;
    }
    const bool trigger = axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
    m_gamepads[pad].axes[static_cast<std::size_t>(axis)] = trigger ? value : pastDeadZone(value);
}

void Input::setKeyDown(Key key, bool down) noexcept
{
    if (isTracked(key))
    {
        applyTransition(m_keysDown, m_keysPressed, m_keysReleased, indexOf(key), down);
    }
}

void Input::setMouseButtonDown(MouseButton button, bool down) noexcept
{
    if (isTracked(button))
    {
        applyTransition(m_buttonsDown, m_buttonsPressed, m_buttonsReleased, indexOf(button), down);
    }
}

void Input::moveMouse(math::Vec2 position, math::Vec2 delta) noexcept
{
    m_mousePosition = position;
    m_mouseDelta += delta;
}

void Input::scrollMouse(math::Vec2 delta) noexcept
{
    m_mouseWheel += delta;
}

void Input::releaseAll() noexcept
{
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
    for (Gamepad& gamepad : m_gamepads)
    {
        gamepad.released |= gamepad.down;
        gamepad.down.reset();
    }
}

} // namespace devex::platform
