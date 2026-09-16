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
}

} // namespace devex::platform
