#include "tools/EditorDock.hpp"
#include "tools/Theme.hpp"

#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/TilesetFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>

TEST_CASE("Tile collision drawing edits a draft and saves only an applied valid outline",
          "[tools][tilecollision][gpu]")
{
    using namespace devex;
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("devex-tile-outline-" + core::Uuid::generate().toString());
        ~Directory()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } directory;
    auto project = asset::createProject(directory.path, "Tile Outlines");
    REQUIRE(project);
    const auto file = project->assetsDirectory() / "Shapes.dvxtileset";
    asset::TilesetData original;
    original.tiles = {{.id = 1, .collision = asset::TileCollision::Polygon}};
    original.tiles[0].collisionPoints = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    REQUIRE(core::writeTextFile(file, asset::writeTilesetFile(original)));
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database);
    (*database)->waitForImports();
    static_cast<void>((*database)->update());
    const auto asset = (*database)->findByPath("res://assets/Shapes.dvxtileset");
    REQUIRE(asset);
    REQUIRE((*database)->find(*asset) != nullptr);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window =
        platform->createWindow({.width = 1600, .height = 1600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    auto overlay = tools::ToolsOverlay::create(*platform, *window, *renderer);
    REQUIRE(overlay);
    (*overlay)->setVisible(true);
    (*overlay)->setAssetDatabase(database->get());
    (*overlay)->selectAsset(*asset);
    scene::Scene scene;
    const auto metrics = tools::detail::deriveThemeMetrics({}, window->displayScale());
    const auto dock = tools::detail::placeDock(
        tools::detail::DockLayout::defaults(false), [](std::string_view) { return true; },
        {0, metrics.menuBarHeight}, {1600, 1600}, metrics.dockGap, metrics.tabHeight);
    const auto inspector =
        dock.contents[static_cast<std::size_t>(tools::detail::DockSlot::RightTop)];
    const float pixelsPerPoint =
        static_cast<float>(window->pixelSize().width) / static_cast<float>(window->size().width);
    const auto origin = inspector.min + math::Vec2{metrics.panelPadding};
    math::Vec2 canvasMin{}, canvasMax{}, apply{};
    bool foundCanvas = false, foundApply = false;
    std::string geometry;
    const auto frame = [&](math::Vec2 mouse = {}, bool left = false, bool right = false,
                           std::string_view typed = {}, platform::Key key = platform::Key::Unknown)
    {
        platform->pollEvents([](const auto&) {});
        auto& input = const_cast<platform::Input&>(platform->toolsInput());
        input.moveMouse(mouse, {});
        input.setMouseButtonDown(platform::MouseButton::Left, left);
        input.setMouseButtonDown(platform::MouseButton::Right, right);
        input.addTypedText(typed);
        input.setKeyDown(platform::Key::Escape, key == platform::Key::Escape);
        (*overlay)->update(scene, std::chrono::milliseconds(16));
        auto& world = renderer->beginFrame();
        (*overlay)->prepareRender(scene, world, tools::PlayState::Editing);
        geometry.clear();
        foundCanvas = foundApply = false;
        for (const auto& surface : world.uiSurfaces)
        {
            if (surface.id != 6)
                continue;
            for (const auto& draw : surface.draws)
            {
                for (std::uint32_t offset = 0; offset + 6 <= draw.indexCount; offset += 6)
                {
                    math::Vec2 low{1.0e9f}, high{-1.0e9f};
                    for (std::uint32_t i = 0; i < 6; ++i)
                    {
                        const auto& vertex =
                            surface.vertices[surface.indices[draw.firstIndex + offset + i]];
                        low = math::min(low, vertex.position);
                        high = math::max(high, vertex.position);
                    }
                    const auto extent = high - low;
                    if (extent.x > 120 && extent.y > 100)
                        geometry += std::format(" kind={} texture={} extent={},{};",
                                                static_cast<int>(draw.kind), draw.texture.isValid(),
                                                extent.x, extent.y);
                    if (draw.kind == render::UiDrawKind::Quad && draw.texture.isValid() &&
                        extent.x > 120 && std::abs(extent.x - extent.y) < 1)
                    {
                        canvasMin = origin + low / pixelsPerPoint;
                        canvasMax = origin + high / pixelsPerPoint;
                        foundCanvas = true;
                    }
                    const auto color =
                        surface.vertices[surface.indices[draw.firstIndex + offset]].color;
                    if (draw.kind == render::UiDrawKind::RoundedQuad && extent.x > 30 &&
                        extent.y < 35 && color.b > 0.9f && color.r < 0.2f)
                    {
                        apply = origin + (low + high) * 0.5f / pixelsPerPoint;
                        foundApply = true;
                    }
                }
            }
        }
        REQUIRE(renderer->endFrame());
    };
    for (int i = 0; i < 4; ++i)
        frame();
    INFO(geometry);
    REQUIRE(foundCanvas);
    REQUIRE(foundApply);
    const auto click = [&](math::Vec2 point, bool right = false)
    {
        frame(point, !right, right);
        frame(point);
        frame();
    };
    const auto point = [&](math::Vec2 cell)
    { return canvasMin + math::Vec2{cell.x, 1 - cell.y} * (canvasMax - canvasMin); };
    const auto draw = [&]
    {
        const float scale = tools::detail::effectiveInterfaceScale({}, window->displayScale());
        click({canvasMin.x + 25 * scale, canvasMin.y - 22.5f * scale});
    };
    const auto read = [&]
    {
        const auto text = core::readTextFile(file);
        REQUIRE(text);
        auto tiles = asset::parseTilesetFile(*text);
        REQUIRE(tiles);
        return *tiles;
    };
    const float unfilteredCanvasTop = canvasMin.y;
    const math::Vec2 filter =
        origin +
        math::Vec2{65, 16.5f} * tools::detail::effectiveInterfaceScale({}, window->displayScale());
    click(filter);
    frame(filter, false, false, "One Way");
    frame();
    // The composite shape editor keeps its canvas and Apply button with a matching property.
    REQUIRE(foundCanvas);
    REQUIRE(foundApply);
    REQUIRE(canvasMin.y < unfilteredCanvasTop - metrics.lineHeight);
    frame({}, false, false, {}, platform::Key::Escape);
    frame();
    // Escape while the field is edited clears the filter and restores the other sections.
    frame();
    draw();
    click(point({0.125f, 0.125f}));
    click(point({0.875f, 0.125f}));
    click(point({0.5f, 0.875f}));
    click(point({0.125f, 0.125f})); // Close by clicking the first vertex.
    CHECK(read() == original);
    frame(point({0.125f, 0.125f}), true);
    frame(point({0.1875f, 0.25f}), true);
    frame(point({0.1875f, 0.25f}));
    frame();
    click(point({0.6875f, 0.5f}));       // Insert on the edge.
    click(point({0.6875f, 0.5f}), true); // Remove it again.
    CHECK(read() == original);
    click(apply);
    const auto saved = read();
    REQUIRE(saved.tiles[0].collisionPoints.size() == 3);
    CHECK(saved.tiles[0].collisionPoints[0] == math::Vec2{0.1875f, 0.25f});
    CHECK(saved.tiles[0].collisionPoints[1] == math::Vec2{0.875f, 0.125f});
    CHECK(saved.tiles[0].collisionPoints[2] == math::Vec2{0.5f, 0.875f});
    draw();
    click(point({0.125f, 0.125f}));
    click(point({0.875f, 0.875f}));
    click(point({0.125f, 0.875f}));
    click(point({0.875f, 0.125f}));
    click(point({0.125f, 0.125f}));
    click(apply); // A crossed outline cannot replace the valid collision.
    CHECK(read() == saved);
}
