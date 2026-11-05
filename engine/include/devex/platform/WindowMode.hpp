#pragma once

#include <cstdint>

namespace devex::platform {

// How a window takes the screen.
enum class WindowMode : std::uint8_t
{
    // With its frame, at its size.
    Windowed,
    // No frame, over the whole display at the resolution of the desktop: other windows are a click away.
    Borderless,
    // The display to itself. Going back to the desktop takes a moment.
    Fullscreen,
};

} // namespace devex::platform
