#include "tools/GraphEditor.hpp"

#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace devex;
using namespace devex::tools::detail;

TEST_CASE("Graph edits undo and redo snapshots and discard a replaced branch", "[tools][graph]")
{
    GraphHistory<std::string> history;
    std::string value = "initial";
    CHECK_FALSE(history.undo(value));
    CHECK(value == "initial");
    history.record(value);
    value = "edit";
    REQUIRE(history.undo(value));
    CHECK(value == "initial");
    CHECK_FALSE(history.canUndo());
    CHECK(history.canRedo());
    REQUIRE(history.redo(value));
    CHECK(value == "edit");
    REQUIRE(history.undo(value));
    history.record(value);
    value = "new branch";
    CHECK_FALSE(history.redo(value));
    CHECK(value == "new branch");
    REQUIRE(history.undo(value));
    CHECK(value == "initial");
    history.clear();
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("Graph history keeps its most recent 200 completed edits", "[tools][graph]")
{
    GraphHistory<int> history;
    int value = 0;
    for (int edit = 1; edit <= 205; ++edit)
    {
        history.record(value);
        value = edit;
    }
    for (int edit = 204; edit >= 5; --edit)
    {
        REQUIRE(history.undo(value));
        CHECK(value == edit);
    }
    CHECK_FALSE(history.undo(value));
    for (int edit = 6; edit <= 205; ++edit)
    {
        REQUIRE(history.redo(value));
        CHECK(value == edit);
    }
    CHECK_FALSE(history.redo(value));
}

TEST_CASE("Graph pan and zoom map the same point back to graph coordinates", "[tools][graph]")
{
    const GraphView view{{-19.0f, 37.0f}, 0.35f};
    const math::Vec2 point{73.0f, -84.0f};
    const math::Vec2 returned = view.toGraph(view.toArea(point));
    CHECK(returned.x == Catch::Approx(point.x));
    CHECK(returned.y == Catch::Approx(point.y));
    CHECK(view.toArea({0.0f, 0.0f}) == view.origin);
}

TEST_CASE("A graph grid reuses its entities and hides lines outside a smaller view",
          "[tools][graph]")
{
    scene::Scene scene;
    const auto layer = scene.createEntity("Grid");
    std::vector<scene::Entity> lines;
    const GraphView view{{-10.0f, 3.0f}, 0.5f};
    const math::Vec4 color{0.2f, 0.3f, 0.4f, 0.16f};
    syncGraphGrid(scene, layer, lines, view, {256.0f, 128.0f}, color);
    REQUIRE_FALSE(lines.empty());
    const auto original = lines;
    for (auto line : lines)
    {
        CHECK(scene.parent(line) == layer);
        CHECK_FALSE(scene.get<scene::UiImage>(line).raycastTarget);
        CHECK(scene.get<scene::UiImage>(line).color == color);
    }
    syncGraphGrid(scene, layer, lines, view, {32.0f, 32.0f}, color);
    CHECK(lines == original);
    CHECK_FALSE(scene.get<scene::UiRect>(lines.back()).visible);
    syncGraphGrid(scene, layer, lines, view, {256.0f, 128.0f}, color);
    CHECK(lines == original);
    CHECK(scene.get<scene::UiRect>(lines.back()).visible);
}
