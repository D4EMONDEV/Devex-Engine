#pragma once

// The frame of the editor, made with the interface of the engine: the menu bar, the status bar, the
// tabs of the scenes and the toolbar of the view. Their menus and their tooltips cannot be drawn in
// strips that thin: they show in a layer over the whole window, which exists only while a menu is
// open or a tooltip shows.
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::tools::detail {

// The images the frame is drawn into, among the interface surfaces of the editor.
inline constexpr std::uint32_t menuBarSurface = 14;
inline constexpr std::uint32_t editorLayerSurface = 15;
inline constexpr std::uint32_t statusBarSurface = 16;
inline constexpr std::uint32_t viewportHeaderSurface = 17;

// The style of a button of a strip: clear until the pointer is on it, tinted while what it stands for
// is on.
[[nodiscard]] inline std::string_view barStyle(const ui::UiWorld& world, scene::Entity button, bool selected) noexcept
{
    return selected ? "bar_selected" : world.hovered() == button ? "bar_hover" : "bar_button";
}

// An entry of a menu: something to do, a mark that is on or off, a menu inside the menu, or a line
// between two groups.
struct MenuEntry
{
    std::optional<Icon> icon;
    std::string label;
    std::string shortcut;
    bool enabled = true;
    // Shows a check at the right of the entry.
    bool checked = false;
    bool separator = false;
    // What choosing the entry does, with the scene on screen at that moment.
    std::function<void(ToolsState&, scene::Scene&)> action;
    // The entries of the menu the entry opens beside it; one level only.
    std::vector<MenuEntry> children;

    [[nodiscard]] static MenuEntry line()
    {
        return MenuEntry{.separator = true};
    }
};

// Opens a menu over the whole window, its top left corner at a place of the screen. `owner` says
// what opened it, such as the title of a menu bar, which then knows its menu is the one open.
DEVEX_API void openEditorMenu(ToolsState& state, std::vector<MenuEntry> entries, ImVec2 at, std::size_t owner);
// What opened the menu that is open, if one is.
[[nodiscard]] DEVEX_API std::optional<std::size_t> editorMenuOwner(const ToolsState& state);
DEVEX_API void closeEditorMenu(ToolsState& state);
// The layer itself: the open menu, and the tooltip a strip asked for. Drawn after every window.
DEVEX_API void drawEditorLayer(ToolsState& state, scene::Scene& scene);
DEVEX_API void renderEditorLayer(ToolsState& state, render::RenderWorld& world);

// The entries that act on the selected entities: cut, copy, paste, duplicate, rename, hide, delete.
DEVEX_API void addEntityEditEntries(ToolsState& state, scene::Scene& scene, std::vector<MenuEntry>& entries);

// The tabs of the scenes and the toolbar of the view, above the image of the viewport, in the window
// of the viewport: in ViewportHeader.cpp.
DEVEX_API void drawViewportHeader(ToolsState& state, scene::Scene& scene);
DEVEX_API void renderViewportHeader(ToolsState& state, render::RenderWorld& world);
// Adds the images of the menu bar, of the status bar, of the header of the viewport and of the layer
// over them to the frame, those drawn this frame.
DEVEX_API void renderEditorFrame(ToolsState& state, render::RenderWorld& world);

} // namespace devex::tools::detail
