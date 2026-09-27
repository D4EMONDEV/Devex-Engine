#include "tools/EditorCamera.hpp"
#include "tools/EditorView.hpp"
#include "tools/TwoDScreen.hpp"

#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using devex::math::Vec2;
using devex::math::Vec3;
using devex::scene::Camera;
using devex::scene::Entity;
using devex::scene::Projection;
using devex::scene::Scene;
using devex::scene::Transform;
using devex::tools::detail::EditorCamera;
using devex::tools::detail::ViewportView;

namespace {

// A scene seen through one camera, standing at a point: a 2D scene for an orthographic camera.
[[nodiscard]] Scene sceneWithCamera(Projection projection, Vec3 position = Vec3{0.0f}, float size = 5.0f)
{
    Scene scene;
    const Entity camera = scene.createEntity("Camera");
    scene.add<Transform>(camera, Transform{.position = position});
    scene.add<Camera>(camera, Camera{.projection = projection, .orthographicSize = size});
    scene.setKind(projection == Projection::Orthographic ? devex::scene::SceneKind::TwoD : devex::scene::SceneKind::ThreeD);
    scene.updateTransforms();
    return scene;
}

} // namespace

TEST_CASE("A scene shows in the screen of its kind, and nothing of it shows in the other", "[tools][editor][2d]")
{
    using devex::tools::detail::ScreenContent;
    using devex::tools::detail::screenContent;
    const Scene level = sceneWithCamera(Projection::Orthographic);
    CHECK(screenContent(true, level) == ScreenContent::Scene);
    CHECK(screenContent(false, level) == ScreenContent::Nothing);
    // The 2D screen of a 3D scene is where its interfaces are edited.
    const Scene world = sceneWithCamera(Projection::Perspective);
    CHECK(screenContent(false, world) == ScreenContent::Scene);
    CHECK(screenContent(true, world) == ScreenContent::Interfaces);

    // A scene opens in the screen of its kind.
    EditorCamera camera;
    devex::tools::detail::fitToScene(camera, level);
    CHECK(camera.isTwoD());
    devex::tools::detail::fitToScene(camera, world);
    CHECK_FALSE(camera.isTwoD());
}

TEST_CASE("The 2D screen draws the interfaces in the frame of what the game shows", "[tools][editor][2d]")
{
    // A 2D camera at (3, 2) showing 4 m above and below: 16 m by 8 m on a screen twice as wide.
    const Scene level = sceneWithCamera(Projection::Orthographic, Vec3{3.0f, 2.0f, 10.0f}, 4.0f);
    const devex::tools::detail::GameFrame frame = devex::tools::detail::gameFrame(level, 2.0f);
    CHECK(frame.camera);
    CHECK_THAT(frame.min.x, WithinAbs(-5.0, 1e-5));
    CHECK_THAT(frame.min.y, WithinAbs(-2.0, 1e-5));
    CHECK_THAT(frame.max.x, WithinAbs(11.0, 1e-5));
    CHECK_THAT(frame.max.y, WithinAbs(6.0, 1e-5));

    // Seen by a 2D view twice as tall, the frame covers half the image, from its top left corner.
    EditorCamera camera;
    camera.setView2D(Vec2{3.0f, 2.0f}, 8.0f);
    camera.setTwoD(true);
    const ViewportView view{.view = camera.view(),
                            .verticalFov = EditorCamera::verticalFov,
                            .size = {800.0f, 400.0f},
                            .orthographic = true,
                            .orthographicSize = camera.orthographicSize()};
    const std::optional<devex::tools::InterfaceFrame> placed = devex::tools::detail::interfaceFrame(frame, view);
    REQUIRE(placed.has_value());
    CHECK(placed->layoutSize == Vec2{800.0f, 400.0f});
    CHECK_THAT(placed->scale, WithinAbs(0.5, 1e-4));
    CHECK_THAT(placed->offset.x, WithinAbs(200.0, 1e-2));
    CHECK_THAT(placed->offset.y, WithinAbs(100.0, 1e-2));

    // A 3D scene edits its interfaces where a new 2D camera would look, at the origin, even when it
    // is seen through an orthographic camera, as a 2.5D game is.
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic})
    {
        Scene world = sceneWithCamera(projection, Vec3{3.0f, 2.0f, 10.0f});
        world.setKind(devex::scene::SceneKind::ThreeD);
        const devex::tools::detail::GameFrame origin = devex::tools::detail::gameFrame(world, 2.0f);
        CHECK_FALSE(origin.camera);
        CHECK_THAT(origin.min.x, WithinAbs(-10.0, 1e-5));
        CHECK_THAT(origin.max.y, WithinAbs(5.0, 1e-5));
    }

    // Opened, the level shows in the 2D screen, on what its game shows.
    EditorCamera opened;
    devex::tools::detail::fitToScene(opened, level);
    CHECK(opened.isTwoD());
    CHECK_THAT(opened.center().x, WithinAbs(3.0, 1e-5));
    CHECK_THAT(opened.center().y, WithinAbs(2.0, 1e-5));
    CHECK(opened.orthographicSize() > 4.0f);
}
