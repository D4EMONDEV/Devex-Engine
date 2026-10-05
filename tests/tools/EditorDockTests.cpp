#include "tools/EditorDock.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using devex::tools::detail::DockBar;
using devex::tools::detail::DockLayout;
using devex::tools::detail::DockPlaces;
using devex::tools::detail::DockSlot;
using devex::tools::detail::placeDock;

namespace {

constexpr devex::math::Vec2 areaMin{0.0f, 20.0f};
constexpr devex::math::Vec2 areaMax{1000.0f, 620.0f};
constexpr float gap = 5.0f;
constexpr float tabHeight = 24.0f;

[[nodiscard]] DockPlaces placed(const DockLayout& layout, std::string_view hidden = {})
{
    return placeDock(layout, [&](std::string_view panel) { return panel != hidden; }, areaMin, areaMax, gap, tabHeight);
}

[[nodiscard]] std::size_t at(DockSlot slot)
{
    return static_cast<std::size_t>(slot);
}

} // namespace

TEST_CASE("The dock starts as Godot's, and a panel moves between its places", "[tools][dock]")
{
    DockLayout layout = DockLayout::defaults(true);
    CHECK(layout.slotOf("Scene") == DockSlot::LeftTop);
    CHECK(layout.slotOf("FileSystem") == DockSlot::LeftBottom);
    CHECK(layout.slotOf("Inspector") == DockSlot::RightTop);
    CHECK(layout.slotOf("Animator") == DockSlot::Bottom);
    // Output is in front of the panels under the screens.
    CHECK(layout.front[at(DockSlot::Bottom)] == "Output");
    // Over a game, no panel of animation.
    CHECK_FALSE(DockLayout::defaults(false).slotOf("Animation").has_value());

    // A panel carried to another place leaves its own, which shows the next one.
    layout.move("Output", DockSlot::RightBottom);
    CHECK(layout.slotOf("Output") == DockSlot::RightBottom);
    CHECK(layout.front[at(DockSlot::RightBottom)] == "Output");
    CHECK(layout.front[at(DockSlot::Bottom)] == "Statistics");
    layout.bringToFront("Profiler");
    CHECK(layout.front[at(DockSlot::Bottom)] == "Profiler");

    // Kept with the project, and read back.
    layout.left = 0.25f;
    const std::optional<DockLayout> read = DockLayout::read(layout.write());
    REQUIRE(read.has_value());
    CHECK(read->panels == layout.panels);
    CHECK(read->front == layout.front);
    CHECK(read->left == Catch::Approx(0.25f));
}

TEST_CASE("The places of the dock share the room, and the empty ones close", "[tools][dock]")
{
    DockLayout layout = DockLayout::defaults(true);
    const DockPlaces places = placed(layout);
    // Both sides, the screens between them, and the panels under the screens.
    REQUIRE(places.slots[at(DockSlot::LeftTop)].visible);
    REQUIRE(places.slots[at(DockSlot::LeftBottom)].visible);
    REQUIRE(places.slots[at(DockSlot::RightTop)].visible);
    CHECK_FALSE(places.slots[at(DockSlot::RightBottom)].visible);
    REQUIRE(places.slots[at(DockSlot::Bottom)].visible);
    // Inspector alone takes its whole side.
    CHECK(places.slots[at(DockSlot::RightTop)].max.y == places.slots[at(DockSlot::LeftBottom)].max.y);
    // A gap between the places, and the screens under the tabs of nothing.
    CHECK(places.center.min.x == places.slots[at(DockSlot::LeftTop)].max.x + gap);
    CHECK(places.center.max.x + gap == places.slots[at(DockSlot::RightTop)].min.x);
    CHECK(places.slots[at(DockSlot::Bottom)].min.x == places.center.min.x);
    CHECK(places.contents[at(DockSlot::Bottom)].min.y == places.slots[at(DockSlot::Bottom)].min.y + tabHeight);
    CHECK(places.front[at(DockSlot::LeftTop)] == "Scene");

    // Without the Inspector, the right side closes and the screens take its room.
    const DockPlaces narrower = placed(layout, "Inspector");
    CHECK_FALSE(narrower.slots[at(DockSlot::RightTop)].visible);
    CHECK_FALSE(narrower.bars[static_cast<std::size_t>(DockBar::Right)].visible);
    CHECK(narrower.center.max.x == areaMax.x - gap);
    // Where a tab dropped there would open it again: at the right of the screens.
    CHECK(narrower.targets[at(DockSlot::RightTop)].visible);
    CHECK(narrower.targets[at(DockSlot::RightTop)].max.x == narrower.center.max.x);

    // A place whose panel in front is hidden shows the next one.
    CHECK(placed(layout, "Output").front[at(DockSlot::Bottom)] == "Statistics");

    // The bar at the left of the screens follows a drag.
    const DockPlaces before = placed(layout);
    const devex::math::Vec2 bar = before.bars[static_cast<std::size_t>(DockBar::Left)].min;
    devex::tools::detail::dragDockBar(layout, before, DockBar::Left, devex::math::Vec2(bar.x + 60.0f, bar.y), devex::math::Vec2(0.0f, 0.0f));
    CHECK(placed(layout).center.min.x == Catch::Approx(before.center.min.x + 60.0f).margin(1.0f));
}
