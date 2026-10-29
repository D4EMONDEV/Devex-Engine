#include "tools/ToolsState.hpp"

#include <devex/scene/Components.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace devex;
using namespace devex::tools::detail;

TEST_CASE("The brushes of the editor sculpt the terrain under the mouse, a stroke at a time", "[tools][terrain][gpu]")
{
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    ToolsState state(*platform, *window, *renderer, tools::ToolsMode::Editor);

    // A flat terrain of 64 m centered on the origin, whose changes the test counts.
    asset::TerrainMaps maps = asset::makeFlatTerrain(65);
    const asset::AssetId data = asset::AssetId::generate();
    int changes = 0;
    state.terrains = std::make_shared<tools::TerrainAccess>(tools::TerrainAccess{
        .maps = [&](asset::AssetId id) { return id == data ? &maps : nullptr; },
        .changed = [&](asset::AssetId, const asset::TerrainRegion&) { ++changes; },
        .edited = [](asset::AssetId) { return false; },
        .saved = [](asset::AssetId) {},
    });
    scene::Scene scene;
    const scene::Entity ground = scene.createEntity("Ground");
    scene.add<scene::Transform>(ground, scene::Transform{.position = {-32.0f, 0.0f, -32.0f}});
    scene.add<scene::Terrain>(ground, scene::Terrain{.data = data, .size = 64.0f});
    scene.updateTransforms();
    state.selection.set(scene.uuid(ground));

    // Looking down at the middle of the terrain from 20 m up.
    const ViewportView view{.view = math::lookAt(math::Vec3{0.0f, 20.0f, 20.0f}, math::Vec3{0.0f}, math::Vec3{0.0f, 1.0f, 0.0f}),
                            .size = {800.0f, 600.0f}};
    const math::Vec2 middle{400.0f, 300.0f};
    auto& input = const_cast<platform::Input&>(platform->toolsInput());
    double time = 0.0;
    const auto frame = [&](bool down, bool ctrl = false) {
        platform->pollEvents([](const platform::Event&) {});
        input.moveMouse(middle, {});
        input.setMouseButtonDown(platform::MouseButton::Left, down);
        input.setKeyDown(platform::Key::LeftControl, ctrl);
        time += 0.05;
        state.input.begin(input, time, 0.05f);
        return handleTerrainPainting(state, scene, view, middle, true);
    };
    const auto middleHeight = [&] { return asset::terrainHeight(maps, 64.0f, 32.0f, 32.0f); };

    // Without a tool, the viewport selects as usual.
    CHECK_FALSE(frame(false));

    // A stroke of Raise, held for a few frames, then released: one step to undo.
    state.terrainPainter.tool = TerrainBrushTool::Raise;
    state.terrainPainter.strength = 1.0f;
    CHECK(frame(false));
    REQUIRE(state.terrainPainter.hovered.has_value());
    CHECK(math::length(*state.terrainPainter.hovered) < 0.1f);
    for (int index = 0; index < 4; ++index)
    {
        CHECK(frame(true));
    }
    CHECK(frame(false));
    const float raised = middleHeight();
    // Three meters a second at full strength, for a brush of 6 m.
    CHECK(raised > 0.5f);
    CHECK(changes > 0);
    CHECK(state.history.undoCount() == 1);
    REQUIRE(state.history.undo(scene).has_value());
    CHECK(middleHeight() == 0.0f);
    REQUIRE(state.history.redo(scene).has_value());
    CHECK(middleHeight() == raised);

    // Ctrl+click takes the height under the mouse, which Flatten brings the ground to.
    state.terrainPainter.tool = TerrainBrushTool::Flatten;
    frame(true, true);
    frame(false);
    CHECK(state.terrainPainter.flattenHeight > 0.5f);
    CHECK(state.history.undoCount() == 1);

    // Escape puts the brushes down.
    state.viewportFocused = true;
    platform->pollEvents([](const platform::Event&) {});
    input.setKeyDown(platform::Key::Escape, true);
    state.input.begin(input, time + 0.05, 0.05f);
    CHECK_FALSE(handleTerrainPainting(state, scene, view, middle, true));
    CHECK(state.terrainPainter.tool == TerrainBrushTool::None);
}
