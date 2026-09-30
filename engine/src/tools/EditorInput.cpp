#include "EditorInput.hpp"

#include <devex/platform/Input.hpp>

#include <algorithm>
#include <cmath>

namespace devex::tools::detail {

namespace {

// As long and as close as two presses may be to count as one double click.
constexpr double doubleClickTime = 0.30;
constexpr float doubleClickDistance = 6.0f;

[[nodiscard]] platform::MouseButton platformButton(Mouse button) noexcept
{
    switch (button)
    {
    case Mouse::Left:
        return platform::MouseButton::Left;
    case Mouse::Right:
        return platform::MouseButton::Right;
    case Mouse::Middle:
        return platform::MouseButton::Middle;
    }
    return platform::MouseButton::Left;
}

[[nodiscard]] float lengthOf(math::Vec2 value) noexcept
{
    return std::sqrt(value.x * value.x + value.y * value.y);
}

} // namespace

void EditorInput::begin(const platform::Input& input, double time, float delta)
{
    m_input = &input;
    m_time = time;
    m_delta = delta;
    m_mouse = input.mousePosition();
    m_mouseDelta = input.mouseDelta();
    m_wheel = input.mouseWheel();
    cursor = platform::Cursor::Arrow;
    for (std::size_t index = 0; index < m_buttons.size(); ++index)
    {
        Button& button = m_buttons[index];
        const platform::MouseButton which = platformButton(static_cast<Mouse>(index));
        button.clicked = input.wasMouseButtonPressed(which);
        button.released = input.wasMouseButtonReleased(which);
        button.down = input.isMouseButtonDown(which);
        button.doubleClicked = false;
        if (button.clicked)
        {
            button.doubleClicked = time - button.lastClick <= doubleClickTime && lengthOf(m_mouse - button.lastClickAt) <= doubleClickDistance;
            // A third press starts over, as a new first one.
            button.lastClick = button.doubleClicked ? -1.0e9 : time;
            button.lastClickAt = m_mouse;
            button.pressedAt = m_mouse;
            button.farthest = 0.0f;
        }
        if (button.down)
        {
            button.farthest = std::max(button.farthest, lengthOf(m_mouse - button.pressedAt));
        }
    }
}

math::Vec2 EditorInput::mouse() const noexcept
{
    return m_mouse;
}

math::Vec2 EditorInput::mouseDelta() const noexcept
{
    return m_mouseDelta;
}

math::Vec2 EditorInput::wheel() const noexcept
{
    return m_wheel;
}

bool EditorInput::down(Mouse button) const noexcept
{
    return m_buttons[static_cast<std::size_t>(button)].down;
}

bool EditorInput::clicked(Mouse button) const noexcept
{
    return m_buttons[static_cast<std::size_t>(button)].clicked;
}

bool EditorInput::released(Mouse button) const noexcept
{
    return m_buttons[static_cast<std::size_t>(button)].released;
}

bool EditorInput::doubleClicked(Mouse button) const noexcept
{
    return m_buttons[static_cast<std::size_t>(button)].doubleClicked;
}

bool EditorInput::anyDown() const noexcept
{
    return std::ranges::any_of(m_buttons, [](const Button& button) { return button.down; });
}

bool EditorInput::dragging(Mouse button, float threshold) const noexcept
{
    const Button& state = m_buttons[static_cast<std::size_t>(button)];
    return state.down && state.farthest >= threshold;
}

math::Vec2 EditorInput::dragDelta(Mouse button) const noexcept
{
    const Button& state = m_buttons[static_cast<std::size_t>(button)];
    return state.down || state.released ? m_mouse - state.pressedAt : math::Vec2{0.0f};
}

math::Vec2 EditorInput::pressedAt(Mouse button) const noexcept
{
    return m_buttons[static_cast<std::size_t>(button)].pressedAt;
}

bool EditorInput::pressed(platform::Key key, bool repeat) const noexcept
{
    return m_input != nullptr && (m_input->wasKeyPressed(key) || (repeat && m_input->wasKeyRepeated(key)));
}

bool EditorInput::keyDown(platform::Key key) const noexcept
{
    return m_input != nullptr && m_input->isKeyDown(key);
}

bool EditorInput::keyReleased(platform::Key key) const noexcept
{
    return m_input != nullptr && m_input->wasKeyReleased(key);
}

bool EditorInput::letterPressed(char letter, bool repeat) const noexcept
{
    return m_input != nullptr && (m_input->wasLetterPressed(letter) || (repeat && m_input->wasLetterRepeated(letter)));
}

bool EditorInput::letterDown(char letter) const noexcept
{
    return m_input != nullptr && m_input->isLetterDown(letter);
}

bool EditorInput::ctrl() const noexcept
{
    return keyDown(platform::Key::LeftControl) || keyDown(platform::Key::RightControl);
}

bool EditorInput::shift() const noexcept
{
    return keyDown(platform::Key::LeftShift) || keyDown(platform::Key::RightShift);
}

bool EditorInput::alt() const noexcept
{
    return keyDown(platform::Key::LeftAlt) || keyDown(platform::Key::RightAlt);
}

KeyModifiers EditorInput::modifiers() const noexcept
{
    return KeyModifiers{.ctrl = ctrl(), .shift = shift(), .alt = alt()};
}

bool EditorInput::chord(KeyModifiers wanted, platform::Key key, bool repeat) const noexcept
{
    return modifiers() == wanted && pressed(key, repeat);
}

bool EditorInput::chord(KeyModifiers wanted, char letter, bool repeat) const noexcept
{
    return modifiers() == wanted && letterPressed(letter, repeat);
}

const std::string& EditorInput::typed() const noexcept
{
    static const std::string nothing;
    return m_input != nullptr ? m_input->typedText() : nothing;
}

double EditorInput::time() const noexcept
{
    return m_time;
}

float EditorInput::delta() const noexcept
{
    return m_delta;
}

} // namespace devex::tools::detail
