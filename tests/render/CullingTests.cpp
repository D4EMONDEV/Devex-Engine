#include <devex/math/Math.hpp>
#include <devex/render/Culling.hpp>

#include <catch2/catch_test_macros.hpp>

using devex::math::Aabb;
using devex::math::Mat4;
using devex::math::Vec3;
using devex::render::Frustum;
using devex::render::frustumOf;

namespace {

// A camera at the origin looking down -Z, as the engine places them.
[[nodiscard]] Frustum cameraFrustum(float aspectRatio = 1.0f, float nearPlane = 0.1f)
{
    const Mat4 projection =
        devex::math::perspectiveReverseZ(devex::math::radians(60.0f), aspectRatio, nearPlane);
    return frustumOf(projection);
}

[[nodiscard]] Aabb boxAt(Vec3 centre, float size = 0.5f)
{
    return Aabb{centre - Vec3{size}, centre + Vec3{size}};
}

} // namespace

TEST_CASE("A box in front of the camera is kept, one behind it is dropped", "[render][culling]")
{
    const Frustum frustum = cameraFrustum();

    CHECK(frustum.intersects(boxAt({0.0f, 0.0f, -5.0f})));
    CHECK_FALSE(frustum.intersects(boxAt({0.0f, 0.0f, 5.0f})));
    // Just behind the camera, and just in front of the near plane.
    CHECK_FALSE(frustum.intersects(boxAt({0.0f, 0.0f, 1.0f}, 0.2f)));
    CHECK(frustum.intersects(boxAt({0.0f, 0.0f, -1.0f}, 0.2f)));
}

TEST_CASE("A box beside the view is dropped, and one that crosses its edge is kept",
          "[render][culling]")
{
    const Frustum frustum = cameraFrustum();

    // Ten meters away, the view is about eleven meters wide.
    CHECK(frustum.intersects(boxAt({0.0f, 0.0f, -10.0f})));
    CHECK(frustum.intersects(boxAt({5.0f, 0.0f, -10.0f})));
    CHECK_FALSE(frustum.intersects(boxAt({20.0f, 0.0f, -10.0f})));
    CHECK_FALSE(frustum.intersects(boxAt({0.0f, 20.0f, -10.0f})));
    // A box big enough to hold the camera is always visible.
    CHECK(frustum.intersects(boxAt({0.0f, 0.0f, 0.0f}, 100.0f)));
}

TEST_CASE("A wide view keeps what a narrow one drops", "[render][culling]")
{
    const Aabb box = boxAt({8.0f, 0.0f, -10.0f});
    CHECK_FALSE(cameraFrustum(1.0f).intersects(box));
    // Twice as wide as it is tall.
    CHECK(cameraFrustum(2.0f).intersects(box));
}

TEST_CASE("An infinite projection has no far plane", "[render][culling]")
{
    const Frustum frustum = cameraFrustum();
    CHECK(frustum.intersects(boxAt({0.0f, 0.0f, -100000.0f}, 10.0f)));
}

TEST_CASE("A box without a size passes every test", "[render][culling]")
{
    // A mesh whose box was never measured must not disappear.
    CHECK(cameraFrustum().intersects(Aabb{}));
}

TEST_CASE("The sides of a shadow cascade keep what stands behind the light", "[render][culling]")
{
    // An orthographic box looking down, as a cascade of the sun does.
    const Mat4 projection = devex::math::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f, 50.0f);
    const Mat4 view =
        devex::math::lookAt(Vec3{0.0f, 20.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f});
    const Frustum frustum = frustumOf(projection * view);

    CHECK(frustum.intersectsSides(boxAt({0.0f, 0.0f, 0.0f})));
    // High above the box, where a caster still darkens the ground under it.
    CHECK(frustum.intersectsSides(boxAt({0.0f, 200.0f, 0.0f})));
    // Beside it, where nothing it casts can reach.
    CHECK_FALSE(frustum.intersectsSides(boxAt({40.0f, 5.0f, 0.0f})));
}

TEST_CASE("A box follows the transform of its instance", "[render][culling]")
{
    const Aabb unit{Vec3{-0.5f}, Vec3{0.5f}};
    const Mat4 moved = devex::math::translate(Mat4(1.0f), Vec3{10.0f, 0.0f, 0.0f});
    const Aabb result = devex::math::transform(moved, unit);
    CHECK(result.min.x == 9.5f);
    CHECK(result.max.x == 10.5f);

    // A turned box grows to hold what it now covers.
    const Mat4 turned = devex::math::rotate(Mat4(1.0f), devex::math::radians(45.0f), Vec3{0.0f, 1.0f, 0.0f});
    const Aabb spun = devex::math::transform(turned, unit);
    CHECK(spun.max.x > 0.7f);
    CHECK(spun.max.y == 0.5f);
}
