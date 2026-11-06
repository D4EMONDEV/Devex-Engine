#pragma once

#include "EditorInput.hpp"

#include <devex/math/Math.hpp>
#include <devex/platform/Window.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace devex::tools::detail {

struct ToolsState;

// A window of the system that one panel fills, with the buttons and the frame of the system: the windows of settings and
// of the export, as Godot opens its own. It waits over the editor, which waits for it, and has its own mouse and keyboard.
class NativeFormWindow
{
public:
    // Null when the system or the GPU give no second window: the form then stays inside the editor.
    [[nodiscard]] static std::shared_ptr<NativeFormWindow> open(ToolsState& state, const std::string& title, math::Vec2 points);
    ~NativeFormWindow();

    NativeFormWindow(const NativeFormWindow&) = delete;
    NativeFormWindow& operator=(const NativeFormWindow&) = delete;

    [[nodiscard]] math::Vec2 sizeInPoints() const noexcept;
    [[nodiscard]] float pixelsPerPoint() const noexcept;

    platform::Window window;
    // What the renderer knows the window by, the image of the panel in it, and whether a form used it this frame.
    std::uint32_t target = 0;
    std::uint32_t surface = 0;
    bool used = false;
    // Its devices, which take the place of those of the editor while the form updates.
    EditorInput input;

private:
    NativeFormWindow(ToolsState& state, platform::Window opened);

    ToolsState& m_state;
};

// Where a frame of the editor starts and ends: the devices of the windows, and those no form asked for go.
void beginNativeWindows(ToolsState& state);
void pruneNativeWindows(ToolsState& state);

} // namespace devex::tools::detail
