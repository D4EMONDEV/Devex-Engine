// Where the panels of the editor stand, as in Godot: two places on each side of the screens, one
// under them, each holding its panels as tabs. The places are made with the interface of the engine,
// and each panel stands in the host the dock opens where its place is.
#pragma once

#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/serialization/Text.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::render {
struct RenderWorld;
}

namespace devex::tools::detail {

struct ToolsState;

enum class DockSlot : std::uint8_t
{
    LeftTop,
    LeftBottom,
    RightTop,
    RightBottom,
    Bottom,
};
inline constexpr std::size_t dockSlotCount = 5;

// The bars between the places, which a drag moves: the width of each side, the height of the
// bottom, and where each side is cut in two.
enum class DockBar : std::uint8_t
{
    Left,
    Right,
    Bottom,
    LeftCut,
    RightCut,
};
inline constexpr std::size_t dockBarCount = 5;

// The panels of each place, in the order of their tabs, the one in front, and how the room is shared,
// as parts of the room of the dock.
struct DEVEX_API DockLayout
{
    std::array<std::vector<std::string>, dockSlotCount> panels;
    std::array<std::string, dockSlotCount> front;
    float left = 0.19f;
    float right = 0.22f;
    float bottom = 0.26f;
    float leftCut = 0.5f;
    float rightCut = 0.5f;

    // Scene and FileSystem on the left, Inspector on the right, Output and its neighbours under the
    // screens; the editor has the panels of animation too.
    [[nodiscard]] static DockLayout defaults(bool editor);
    [[nodiscard]] std::optional<DockSlot> slotOf(std::string_view panel) const;
    // Takes a panel to the end of the tabs of a place, in front.
    void move(std::string_view panel, DockSlot to);
    void bringToFront(std::string_view panel);
    // A panel the layout does not hold yet goes where the defaults put it, or under the screens.
    void adopt(std::string_view panel, bool editor);

    // As the settings of a project keep it, and back; what cannot be read keeps the defaults.
    [[nodiscard]] serialization::TextSection write() const;
    [[nodiscard]] static std::optional<DockLayout> read(const serialization::TextSection& section);
};

// A rectangle of the screen, in points of the window.
struct DEVEX_API DockRect
{
    math::Vec2 min{0.0f, 0.0f};
    math::Vec2 max{0.0f, 0.0f};
    bool visible = false;

    [[nodiscard]] bool contains(math::Vec2 point) const noexcept
    {
        return visible && point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
    }
};

// Where everything of the dock stands this frame.
struct DEVEX_API DockPlaces
{
    DockRect area;
    // The screens: the views and the Script screen, or the game behind the tools.
    DockRect center;
    // Each place with its tabs, and under them the panel in front.
    std::array<DockRect, dockSlotCount> slots;
    std::array<DockRect, dockSlotCount> contents;
    std::array<std::string, dockSlotCount> front;
    // Where a tab dragged there goes: the place itself, or where an empty place would open.
    std::array<DockRect, dockSlotCount> targets;
    std::array<DockRect, dockBarCount> bars;
    float gap = 0.0f;
    float tabHeight = 0.0f;
};

// Shares the room between `min` and `max` among the places that show a panel: a place without one
// closes, a side without either of its places too.
[[nodiscard]] DEVEX_API DockPlaces placeDock(const DockLayout& layout, const std::function<bool(std::string_view)>& shown, math::Vec2 min,
                                             math::Vec2 max, float gap, float tabHeight);
// Moves a bar to a point, as a drag that grabbed it `grab` points from its start does.
DEVEX_API void dragDockBar(DockLayout& layout, const DockPlaces& places, DockBar bar, math::Vec2 point, math::Vec2 grab);

// The dock of the editor, drawn before the panels: its places, their tabs and the bars between them.
DEVEX_API void drawEditorDock(ToolsState& state);
DEVEX_API void renderEditorDock(ToolsState& state, render::RenderWorld& world);
// Opens the host of a panel where its place puts it, on the colour of the panels and inside a margin;
// a bare one fills its place edge to edge on the colour around the panels, as the view. False while
// the panel is behind another tab, or has no place. endDockedPanel follows a true only.
[[nodiscard]] DEVEX_API bool beginDockedPanel(ToolsState& state, const char* name, bool bare = false);
DEVEX_API void endDockedPanel(ToolsState& state);
// Shows a panel, in front of its place, and gives it the keyboard.
DEVEX_API void focusPanel(ToolsState& state, std::string_view name);

} // namespace devex::tools::detail
