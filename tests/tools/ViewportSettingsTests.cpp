#include "tools/Gizmo.hpp"
#include "tools/ViewportSettings.hpp"

#include <devex/ui/Color.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace devex;
using namespace devex::tools::detail;

TEST_CASE("Viewport preferences survive saving and keep independent visibility choices",
          "[tools][viewport]")
{
    ViewportSettings settings;
    settings.gizmoSize = 180.0f;
    settings.gizmoThickness = 3.0f;
    settings.lineWidth = 2.0f;
    settings.iconSize = 32.0f;
    settings.gridSpacing = 0.25f;
    settings.gridOpacity = 0.35f;
    settings.axisX = {0.2f, 0.6f, 1.0f};
    settings.gridColor = {0.4f, 0.3f, 0.2f};
    settings.showGrid = false;
    settings.showCameras = false;
    settings.showAudio = false;
    settings.showColliders = true;
    serialization::TextDocument document;
    document.sections.push_back(writeViewportSettings(settings));
    const auto parsed = serialization::parseText(serialization::writeText(document));
    REQUIRE(parsed);
    const auto read = readViewportSettings(parsed->sections.front());
    CHECK(read.gizmoSize == 180.0f);
    CHECK(read.lineWidth == 2.0f);
    CHECK(read.gizmoThickness == 3.0f);
    CHECK(read.iconSize == 32.0f);
    CHECK(read.gridSpacing == 0.25f);
    CHECK(read.gridOpacity == 0.35f);
    CHECK(read.axisX == settings.axisX);
    CHECK(math::length(read.gridColor - settings.gridColor) < 0.007f);
    CHECK_FALSE(read.showGrid);
    CHECK_FALSE(read.showCameras);
    CHECK_FALSE(read.showAudio);
    CHECK(read.showLights);
    CHECK(read.showColliders);
}

TEST_CASE("Invalid viewport preferences leave usable bounded defaults", "[tools][viewport]")
{
    const auto parsed = serialization::parseText(
        R"([viewport gizmo_size=-500 line_width=100 icon_size="large" grid_spacing=0 grid_opacity=4 axis_x="#broken" show_grid="maybe"])");
    REQUIRE(parsed);
    const auto settings = readViewportSettings(parsed->sections.front());
    const ViewportSettings defaults;
    CHECK(settings.gizmoSize == 40.0f);
    CHECK(settings.lineWidth == 6.0f);
    CHECK(settings.iconSize == defaults.iconSize);
    CHECK(settings.gridSpacing == 0.01f);
    CHECK(settings.gridOpacity == 1.0f);
    CHECK(settings.axisX == defaults.axisX);
    CHECK(settings.showGrid);
}

TEST_CASE("Configured handles are picked and dragged at their drawn size", "[tools][viewport]")
{
    const ViewportView view{.view = math::lookAt(math::Vec3{0.0f, 0.0f, 10.0f}, math::Vec3{0.0f},
                                                 math::Vec3{0.0f, 1.0f, 0.0f}),
                            .size = {800.0f, 600.0f}};
    ViewportSettings settings;
    settings.gizmoSize = 180.0f;
    settings.axisX = {0.0f, 0.5f, 1.0f};
    Gizmo gizmo;
    applyGizmoSettings(gizmo, settings);
    const math::Mat4 world{1.0f};
    const auto center = *view.project(math::Vec3{0.0f});
    CHECK(gizmo.hitTest(view, world, center + math::Vec2{150.0f, 0.0f}) == GizmoHandle::X);
    gizmo.mode = GizmoMode::Rotate;
    gizmo.twoD = true;
    CHECK(gizmo.hitTest(view, world, center + math::Vec2{207.0f, 0.0f}) == GizmoHandle::View);
    gizmo.mode = GizmoMode::Scale;
    const auto start = center + math::Vec2{140.0f, 0.0f};
    gizmo.begin(GizmoHandle::X, view, world, world, {}, start);
    const auto dragged = gizmo.drag(view, start + math::Vec2{90.0f, 0.0f}, false);
    CHECK(dragged.scale.x == Catch::Approx(1.5f).margin(0.01f));
    gizmo.end();
    GizmoGeometry geometry;
    gizmo.mode = GizmoMode::Translate;
    gizmo.draw(view, world, GizmoHandle::None, geometry);
    const auto expected = ui::linearFromSrgb(math::Vec4(settings.axisX, 1.0f));
    CHECK(std::ranges::any_of(geometry.triangles,
                              [&](const auto& vertex) { return vertex.color == expected; }));
}
