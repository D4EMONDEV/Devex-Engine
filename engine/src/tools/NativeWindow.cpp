#include "NativeWindow.hpp"

#include "Theme.hpp"
#include "ToolsState.hpp"

#include <devex/core/Log.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace devex::tools::detail {

std::shared_ptr<NativeFormWindow> NativeFormWindow::open(ToolsState& state, const std::string& title, math::Vec2 points)
{
    // As large as asked and as the screen holds.
    const platform::ScreenArea area = state.window.usableArea();
    const auto fitted = [](float wanted, std::uint32_t room) {
        const float most = room == 0 ? wanted : static_cast<float>(room) * 0.92f;
        return static_cast<std::uint32_t>(std::round(std::min(wanted, most)));
    };
    core::Result<platform::Window> window = state.platform.createWindow({
        .title = title,
        .width = fitted(points.x, area.width),
        .height = fitted(points.y, area.height),
        .resizable = true,
        .vulkan = true,
        // In the tests of the tools, as the editor is.
        .hidden = state.window.isHidden(),
    });
    if (!window)
    {
        DEVEX_LOG_DEBUG("The form {} stays inside the editor: {}", title, window.error());
        return nullptr;
    }
    std::shared_ptr<NativeFormWindow> opened(new NativeFormWindow(state, std::move(*window)));
    core::Result<std::uint32_t> target = state.renderer.addWindowTarget(opened->window);
    if (!target)
    {
        DEVEX_LOG_DEBUG("The form {} stays inside the editor: {}", title, target.error());
        return nullptr;
    }
    opened->target = *target;
    const ThemeColors& colors = themeColors();
    opened->window.setTitleBarColors(colors.dark, math::Vec3(colors.outer.x, colors.outer.y, colors.outer.z));
    opened->window.centerOver(state.window);
    opened->window.setOwner(state.window, true);
    state.platform.trackWindowInput(opened->window.id());
    return opened;
}

NativeFormWindow::NativeFormWindow(ToolsState& state, platform::Window opened)
    : window(std::move(opened))
    , m_state(state)
{
}

NativeFormWindow::~NativeFormWindow()
{
    // Typing stops with the window that took it.
    m_state.platform.setTextInput(window, false);
    m_state.platform.untrackWindowInput(window.id());
    if (target != 0)
    {
        m_state.renderer.removeWindowTarget(target);
    }
}

math::Vec2 NativeFormWindow::sizeInPoints() const noexcept
{
    const math::Extent2D size = window.size();
    return math::Vec2(static_cast<float>(size.width), static_cast<float>(size.height));
}

float NativeFormWindow::pixelsPerPoint() const noexcept
{
    const math::Extent2D size = window.size();
    return size.width == 0 ? 1.0f : static_cast<float>(window.pixelSize().width) / static_cast<float>(size.width);
}

void beginNativeWindows(ToolsState& state)
{
    for (const auto& [title, native] : state.nativeWindows)
    {
        native->used = false;
        native->input.begin(state.platform.windowInput(native->window.id()), state.clock, state.input.delta());
    }
}

void pruneNativeWindows(ToolsState& state)
{
    std::erase_if(state.nativeWindows, [](const auto& entry) { return !entry.second->used; });
}

} // namespace devex::tools::detail
