// The keyboard and the mouse as the tools of the editor read them: what the devices did this frame,
// from the platform, whatever the tools keep from the game; the clicks counted in twos and the drags
// measured, as the panels need them.
#pragma once

#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Key.hpp>
#include <devex/platform/Platform.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace devex::platform {
class Input;
}

namespace devex::tools::detail {

enum class Mouse : std::uint8_t
{
    Left,
    Right,
    Middle,
};

// The modifiers a shortcut asks for, all of them and none other.
struct DEVEX_API KeyModifiers
{
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    [[nodiscard]] bool operator==(const KeyModifiers&) const noexcept = default;
};

class DEVEX_API EditorInput
{
public:
    // Reads the devices at the start of a frame; `time` counts the seconds of the tools.
    void begin(const platform::Input& input, double time, float delta);

    // The mouse, in the points of the window.
    [[nodiscard]] math::Vec2 mouse() const noexcept;
    [[nodiscard]] math::Vec2 mouseDelta() const noexcept;
    // Wheel turns this frame: up is away from the user, and across is to the right.
    [[nodiscard]] math::Vec2 wheel() const noexcept;
    [[nodiscard]] bool down(Mouse button) const noexcept;
    [[nodiscard]] bool clicked(Mouse button) const noexcept;
    [[nodiscard]] bool released(Mouse button) const noexcept;
    // A second press soon after the first, and close to it.
    [[nodiscard]] bool doubleClicked(Mouse button) const noexcept;
    [[nodiscard]] bool anyDown() const noexcept;
    // Whether the pointer went farther than `threshold` points from where the button was pressed,
    // while it is held; and how far it is now.
    [[nodiscard]] bool dragging(Mouse button, float threshold = 6.0f) const noexcept;
    [[nodiscard]] math::Vec2 dragDelta(Mouse button) const noexcept;
    [[nodiscard]] math::Vec2 pressedAt(Mouse button) const noexcept;

    // Keys are places on the keyboard: the arrows, Enter, F5. With `repeat`, the repeats of a key
    // held down count as presses.
    [[nodiscard]] bool pressed(platform::Key key, bool repeat = true) const noexcept;
    [[nodiscard]] bool keyDown(platform::Key key) const noexcept;
    [[nodiscard]] bool keyReleased(platform::Key key) const noexcept;
    // Letters are what the layout prints on the keys: the shortcuts of text follow them, so that
    // Ctrl+Z undoes on a French keyboard too.
    [[nodiscard]] bool letterPressed(char letter, bool repeat = false) const noexcept;
    [[nodiscard]] bool letterDown(char letter) const noexcept;
    [[nodiscard]] KeyModifiers modifiers() const noexcept;
    [[nodiscard]] bool ctrl() const noexcept;
    [[nodiscard]] bool shift() const noexcept;
    [[nodiscard]] bool alt() const noexcept;
    // A shortcut: the modifiers exactly, and the key or the letter.
    [[nodiscard]] bool chord(KeyModifiers wanted, platform::Key key, bool repeat = false) const noexcept;
    [[nodiscard]] bool chord(KeyModifiers wanted, char letter, bool repeat = false) const noexcept;
    // What the keys wrote this frame, while typing is on.
    [[nodiscard]] const std::string& typed() const noexcept;

    [[nodiscard]] double time() const noexcept;
    [[nodiscard]] float delta() const noexcept;

    // The shape the pointer asks for this frame; the arrow unless a part of the editor asks.
    platform::Cursor cursor = platform::Cursor::Arrow;

private:
    struct Button
    {
        bool down = false;
        bool clicked = false;
        bool released = false;
        bool doubleClicked = false;
        math::Vec2 pressedAt{0.0f};
        double lastClick = -1.0e9;
        math::Vec2 lastClickAt{0.0f};
        float farthest = 0.0f;
    };

    const platform::Input* m_input = nullptr;
    std::array<Button, 3> m_buttons{};
    math::Vec2 m_mouse{0.0f};
    math::Vec2 m_mouseDelta{0.0f};
    math::Vec2 m_wheel{0.0f};
    double m_time = 0.0;
    float m_delta = 0.0f;
};

} // namespace devex::tools::detail
