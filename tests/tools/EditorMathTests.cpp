#include "tools/EditorCamera.hpp"
#include "tools/EditorView.hpp"
#include "tools/Gizmo.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using Catch::Matchers::WithinAbs;
using devex::math::Mat4;
using devex::math::Vec2;
using devex::math::Vec3;
using devex::tools::detail::EditorCamera;
using devex::tools::detail::Gizmo;
using devex::tools::detail::GizmoHandle;
using devex::tools::detail::GizmoMode;
using devex::tools::detail::GizmoSpace;
using devex::tools::detail::ViewportView;

namespace {

// A 800x600 view from 10 m along +Z, looking at the origin.
[[nodiscard]] ViewportView frontView()
{
    EditorCamera camera;
    camera.lookAt(Vec3{0.0f, 0.0f, 10.0f}, Vec3{0.0f});
    return ViewportView{.view = camera.view(), .verticalFov = EditorCamera::verticalFov, .size = {800.0f, 600.0f}};
}

void checkNear(Vec3 actual, Vec3 expected, float margin = 1e-3f)
{
    CHECK_THAT(actual.x, WithinAbs(expected.x, margin));
    CHECK_THAT(actual.y, WithinAbs(expected.y, margin));
    CHECK_THAT(actual.z, WithinAbs(expected.z, margin));
}

} // namespace

TEST_CASE("Viewport views project points and cast rays through pixels", "[tools][editor]")
{
    const ViewportView view = frontView();
    checkNear(view.cameraPosition(), Vec3{0.0f, 0.0f, 10.0f});
    checkNear(view.cameraForward(), Vec3{0.0f, 0.0f, -1.0f});

    const auto center = view.project(Vec3{0.0f});
    REQUIRE(center.has_value());
    CHECK_THAT(center->x, WithinAbs(400.0f, 1e-2f));
    CHECK_THAT(center->y, WithinAbs(300.0f, 1e-2f));
    // Up in the world is up on screen, where pixel rows grow downwards.
    CHECK(view.project(Vec3{0.0f, 1.0f, 0.0f})->y < 300.0f);
    CHECK(view.project(Vec3{1.0f, 0.0f, 0.0f})->x > 400.0f);
    CHECK_FALSE(view.project(Vec3{0.0f, 0.0f, 20.0f}).has_value());

    // A ray through a projected pixel passes through the point.
    const Vec3 point{1.5f, -0.75f, 2.0f};
    const auto pixel = view.project(point);
    REQUIRE(pixel.has_value());
    const devex::tools::detail::Ray ray = view.ray(*pixel);
    const float distance = devex::math::length(point - ray.origin);
    checkNear(ray.origin + ray.direction * distance, point, 1e-2f);

    // At 10 m with a 60 degree field of view, the 600 pixels cover 2 * 10 * tan(30) meters.
    CHECK_THAT(view.worldSize(Vec3{0.0f}, 600.0f), WithinAbs(20.0f * std::tan(std::numbers::pi_v<float> / 6.0f), 1e-3f));
}

TEST_CASE("Lines and planes meet rays where expected", "[tools][editor]")
{
    const devex::tools::detail::Ray ray{Vec3{0.0f, 1.0f, 5.0f}, Vec3{0.0f, 0.0f, -1.0f}};
    const auto parameter = devex::tools::detail::closestParameterOnLine(Vec3{-3.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, ray);
    REQUIRE(parameter.has_value());
    CHECK_THAT(*parameter, WithinAbs(3.0f, 1e-4f));
    CHECK_FALSE(devex::tools::detail::closestParameterOnLine(Vec3{0.0f}, Vec3{0.0f, 0.0f, 1.0f}, ray).has_value());

    const auto hit = devex::tools::detail::intersectPlane(ray, Vec3{0.0f, 0.0f, -2.0f}, Vec3{0.0f, 0.0f, 1.0f});
    REQUIRE(hit.has_value());
    checkNear(*hit, Vec3{0.0f, 1.0f, -2.0f});
    CHECK_FALSE(devex::tools::detail::intersectPlane(ray, Vec3{0.0f, 0.0f, 9.0f}, Vec3{0.0f, 0.0f, 1.0f}).has_value());

    CHECK_THAT(devex::tools::detail::distanceToSegment(Vec2{5.0f, 3.0f}, Vec2{0.0f}, Vec2{10.0f, 0.0f}), WithinAbs(3.0f, 1e-5f));
    CHECK_THAT(devex::tools::detail::distanceToSegment(Vec2{-4.0f, 3.0f}, Vec2{0.0f}, Vec2{10.0f, 0.0f}), WithinAbs(5.0f, 1e-5f));
}

TEST_CASE("The editor camera looks, orbits, pans and frames", "[tools][editor]")
{
    EditorCamera camera;
    camera.lookAt(Vec3{0.0f, 5.0f, 10.0f}, Vec3{0.0f});
    checkNear(camera.position(), Vec3{0.0f, 5.0f, 10.0f});
    checkNear(camera.pivot(), Vec3{0.0f});

    // Orbiting keeps the pivot and the distance.
    const float distance = camera.distance();
    camera.orbit(Vec2{120.0f, 30.0f});
    checkNear(camera.pivot(), Vec3{0.0f});
    CHECK_THAT(devex::math::length(camera.position() - camera.pivot()), WithinAbs(distance, 1e-3f));

    // Looking around keeps the position.
    const Vec3 position = camera.position();
    camera.look(Vec2{-40.0f, 10.0f});
    checkNear(camera.position(), position);

    // Flying forward moves along the view direction.
    const Vec3 forward = camera.forward();
    camera.fly(Vec3{0.0f, 0.0f, -1.0f}, 1.0f, false);
    checkNear(camera.position(), position + forward * camera.speed());

    camera.frame(Vec3{3.0f, 1.0f, -2.0f}, 1.0f);
    checkNear(camera.pivot(), Vec3{3.0f, 1.0f, -2.0f});
    CHECK(camera.distance() > 1.0f);

    // The view matrix maps the pivot straight ahead.
    const devex::math::Vec4 pivotInView = camera.view() * devex::math::Vec4(camera.pivot(), 1.0f);
    CHECK_THAT(pivotInView.x, WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(pivotInView.y, WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(pivotInView.z, WithinAbs(-camera.distance(), 1e-3f));
}

TEST_CASE("Translation handles are hit on screen and move along their axis", "[tools][editor]")
{
    const ViewportView view = frontView();
    Gizmo gizmo;
    const Mat4 world{1.0f};
    const Vec2 center = *view.project(Vec3{0.0f});

    CHECK(gizmo.hitTest(view, world, center) == GizmoHandle::View);
    CHECK(gizmo.hitTest(view, world, center + Vec2{60.0f, 1.0f}) == GizmoHandle::X);
    CHECK(gizmo.hitTest(view, world, center + Vec2{1.0f, -60.0f}) == GizmoHandle::Y);
    CHECK(gizmo.hitTest(view, world, center + Vec2{30.0f, -30.0f}) == GizmoHandle::XY);
    CHECK(gizmo.hitTest(view, world, center + Vec2{200.0f, 200.0f}) == GizmoHandle::None);

    // Dragging the X arrow 1 m to the right, under a parent scaled twice, moves 0.5 m locally.
    const Mat4 parent = devex::math::scale(Mat4{1.0f}, Vec3{2.0f});
    const devex::scene::Transform local{.position = {0.0f, 0.0f, 0.0f}};
    const Vec2 start = center + Vec2{60.0f, 0.0f};
    gizmo.begin(GizmoHandle::X, view, world, parent, local, start);
    REQUIRE(gizmo.isDragging());
    const Vec2 oneMeterRight = *view.project(Vec3{1.0f, 0.0f, 0.0f}) - center;
    const devex::scene::Transform moved = gizmo.drag(view, start + oneMeterRight + Vec2{0.0f, 25.0f}, false);
    checkNear(moved.position, Vec3{0.5f, 0.0f, 0.0f}, 1e-2f);

    // Snapping rounds the world movement to half meters.
    const Vec2 almost = *view.project(Vec3{0.7f, 0.0f, 0.0f}) - center;
    checkNear(gizmo.drag(view, start + almost, true).position, Vec3{0.25f, 0.0f, 0.0f}, 1e-3f);
    gizmo.end();
    CHECK_FALSE(gizmo.isDragging());
}

TEST_CASE("Rotation and scale handles change the local transform", "[tools][editor]")
{
    const ViewportView view = frontView();
    const Mat4 world{1.0f};
    const Vec2 center = *view.project(Vec3{0.0f});

    SECTION("Rotating around Z, which faces the camera")
    {
        Gizmo gizmo;
        gizmo.mode = GizmoMode::Rotate;
        const float radius = Gizmo::sizeInPixels;
        const auto onRing = [&](float degrees) {
            const float angle = devex::math::radians(degrees);
            return center + Vec2{std::cos(angle), -std::sin(angle)} * radius;
        };
        // The Z ring faces the camera, inside the larger view ring. The X and Y rings are seen
        // edge-on, as lines through the center.
        CHECK(gizmo.hitTest(view, world, onRing(45.0f)) == GizmoHandle::Z);
        CHECK(gizmo.hitTest(view, world, center + Vec2{radius * 1.15f, 0.0f}) == GizmoHandle::View);

        gizmo.begin(GizmoHandle::Z, view, world, Mat4{1.0f}, devex::scene::Transform{}, onRing(45.0f));
        // A quarter turn counterclockwise on screen.
        const devex::scene::Transform turned = gizmo.drag(view, onRing(135.0f), false);
        checkNear(turned.rotation * Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 1e-2f);

        // Snapping rounds to 15 degrees.
        const float angle = devex::math::radians(85.0f);
        const devex::scene::Transform snapped =
            gizmo.drag(view, center + Vec2{std::cos(angle), -std::sin(angle)} * radius, true);
        const Vec3 axis = snapped.rotation * Vec3{1.0f, 0.0f, 0.0f};
        CHECK_THAT(std::atan2(axis.y, axis.x), WithinAbs(devex::math::radians(45.0f), 1e-3f));
    }

    SECTION("Scaling along local X")
    {
        Gizmo gizmo;
        gizmo.mode = GizmoMode::Scale;
        gizmo.space = GizmoSpace::World;
        const devex::scene::Transform local{.scale = {2.0f, 1.0f, 1.0f}};
        const Mat4 scaledWorld = local.matrix();
        const Vec2 tip = center + Vec2{Gizmo::sizeInPixels * 0.9f, 0.0f};
        CHECK(gizmo.hitTest(view, scaledWorld, tip) == GizmoHandle::X);

        gizmo.begin(GizmoHandle::X, view, scaledWorld, Mat4{1.0f}, local, tip);
        // Dragging by the gizmo's length doubles the scale.
        const devex::scene::Transform scaled = gizmo.drag(view, tip + Vec2{Gizmo::sizeInPixels, 0.0f}, false);
        CHECK_THAT(scaled.scale.x, WithinAbs(4.0f, 1e-2f));
        CHECK(scaled.scale.y == 1.0f);
        CHECK(scaled.position == local.position);
    }
}
