#include <devex/math/Math.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;
using devex::math::Mat4;
using devex::math::Vec3;
using devex::math::Vec4;

namespace {

Vec3 project(const Mat4& projection, const Vec3& viewPosition)
{
    const Vec4 clip = projection * Vec4(viewPosition, 1.0f);
    return Vec3(clip) / clip.w;
}

} // namespace

TEST_CASE("Reversed depth is 1 at the near plane and tends to 0", "[math][projection]")
{
    const Mat4 projection =
        devex::math::perspectiveReverseZ(devex::math::radians(60.0f), 16.0f / 9.0f, 0.1f);

    CHECK_THAT(project(projection, {0.0f, 0.0f, -0.1f}).z, WithinAbs(1.0, 1e-6));
    CHECK_THAT(project(projection, {0.0f, 0.0f, -1.0f}).z, WithinAbs(0.1, 1e-6));
    CHECK_THAT(project(projection, {0.0f, 0.0f, -1.0e6f}).z, WithinAbs(0.0, 1e-6));
    CHECK(project(projection, {0.0f, 0.0f, -2.0f}).z > project(projection, {0.0f, 0.0f, -3.0f}).z);
}

TEST_CASE("The frustum edges map to the clip space borders", "[math][projection]")
{
    const float fov = devex::math::radians(90.0f);
    const float aspectRatio = 2.0f;
    const Mat4 projection = devex::math::perspectiveReverseZ(fov, aspectRatio, 0.5f);

    // With a 90 degree vertical field of view, the top edge is as far above the axis as ahead.
    const float distance = 10.0f;
    const Vec3 topRight{distance * aspectRatio, distance, -distance};
    const Vec3 projected = project(projection, topRight);

    CHECK_THAT(projected.x, WithinAbs(1.0, 1e-5));
    CHECK_THAT(projected.y, WithinAbs(1.0, 1e-5));
}

TEST_CASE("An orthographic projection keeps sizes and reverses depth between its planes", "[math][projection]")
{
    const Mat4 projection = devex::math::orthographicReverseZ(5.0f, 2.0f, 0.5f, 100.5f);

    CHECK_THAT(project(projection, {0.0f, 0.0f, -0.5f}).z, WithinAbs(1.0, 1e-6));
    CHECK_THAT(project(projection, {0.0f, 0.0f, -100.5f}).z, WithinAbs(0.0, 1e-6));
    CHECK_THAT(project(projection, {0.0f, 0.0f, -50.5f}).z, WithinAbs(0.5, 1e-6));
    // The same point stands at the same place of the image whatever its distance.
    for (const float distance : {1.0f, 20.0f, 90.0f})
    {
        const Vec3 corner = project(projection, {10.0f, 5.0f, -distance});
        CHECK_THAT(corner.x, WithinAbs(1.0, 1e-6));
        CHECK_THAT(corner.y, WithinAbs(1.0, 1e-6));
    }
}
