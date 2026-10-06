#include <devex/render/Light2D.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using devex::math::Vec2;
using devex::render::noOccluder2D;
using devex::render::RenderOccluder2D;

namespace {

// The angle distance i of a point light looks along.
[[nodiscard]] float angleOf(std::size_t index, std::size_t count)
{
    return (static_cast<float>(index) + 0.5f) / static_cast<float>(count) * 2.0f * std::numbers::pi_v<float>;
}

} // namespace

TEST_CASE("Point lights keep the distance to the nearest occluder in every direction", "[render][light2d]")
{
    const std::vector<RenderOccluder2D> edges{
        // A wall two meters to the right, another behind it, a roof of another mask above, and a
        // floor beyond the radius.
        {.from = {2.0f, -1.0f}, .to = {2.0f, 1.0f}, .mask = 1},
        {.from = {3.0f, -1.0f}, .to = {3.0f, 1.0f}, .mask = 1},
        {.from = {1.0f, 3.0f}, .to = {-1.0f, 3.0f}, .mask = 2},
        {.from = {-1.0f, -10.0f}, .to = {1.0f, -10.0f}, .mask = 1},
        // An edge through the light itself is left out.
        {.from = {-1.0f, -1.0f}, .to = {1.0f, 1.0f}, .mask = 1},
    };
    constexpr std::size_t count = 1024;
    std::vector<float> distances(count);
    devex::render::pointShadow2D({0.0f, 0.0f}, 5.0f, edges, 1, distances);
    CHECK_THAT(distances[0], WithinAbs(2.0f / std::cos(angleOf(0, count)), 1e-4));
    CHECK_THAT(distances[count - 1], WithinAbs(2.0f, 1e-3));
    // The wall covers 26.6 degrees each way: 24.8 degrees still meets it, 30 does not.
    CHECK_THAT(distances[70], WithinAbs(2.0f / std::cos(angleOf(70, count)), 1e-4));
    CHECK(distances[85] == noOccluder2D);
    // Up, the roof hides only the lights of its mask; down and left, nothing.
    CHECK(distances[count / 4] == noOccluder2D);
    CHECK(distances[count / 2] == noOccluder2D);
    CHECK(distances[count * 3 / 4] == noOccluder2D);
    devex::render::pointShadow2D({0.0f, 0.0f}, 5.0f, edges, 3, distances);
    CHECK_THAT(distances[count / 4], WithinAbs(3.0f / std::sin(angleOf(count / 4, count)), 1e-4));
    // A light whose radius stops short of the wall is not hidden by it.
    devex::render::pointShadow2D({0.0f, 0.0f}, 1.5f, edges, 1, distances);
    CHECK(distances[0] == noOccluder2D);
}

TEST_CASE("Directional lights keep the distance to the nearest occluder along each strip", "[render][light2d]")
{
    // Light falling straight down over a view of ten meters, with the occluders ten meters above it.
    const devex::render::DirectionalShadowFrame2D frame =
        devex::render::directionalShadowFrame({0.0f, -2.0f}, {-5.0f, -5.0f}, {5.0f, 5.0f}, 10.0f);
    CHECK_THAT(frame.direction.y, WithinAbs(-1.0, 1e-6));
    CHECK_THAT(frame.origin.x, WithinAbs(0.0, 1e-5));
    CHECK_THAT(frame.origin.y, WithinAbs(15.0, 1e-5));
    CHECK_THAT(frame.width, WithinAbs(10.0, 1e-5));
    CHECK_THAT(frame.length, WithinAbs(20.0, 1e-5));

    const std::vector<RenderOccluder2D> edges{
        {.from = {-1.0f, 2.0f}, .to = {1.0f, 2.0f}, .mask = 1},
        {.from = {-1.0f, 4.0f}, .to = {0.0f, 4.0f}, .mask = 1},
        {.from = {3.0f, 0.0f}, .to = {4.0f, 0.0f}, .mask = 2},
    };
    constexpr std::size_t count = 100;
    std::vector<float> distances(count);
    devex::render::directionalShadow2D(frame, edges, 1, distances);
    // Strips 50 and 55 start at x = 0.05 and 0.55, under the lower edge only; strip 45 at x = -0.45,
    // under both, meets the higher one first.
    CHECK_THAT(distances[50], WithinAbs(13.0, 1e-4));
    CHECK_THAT(distances[45], WithinAbs(11.0, 1e-4));
    CHECK_THAT(distances[55], WithinAbs(13.0, 1e-4));
    CHECK(distances[10] == noOccluder2D);
    CHECK(distances[85] == noOccluder2D);
    devex::render::directionalShadow2D(frame, edges, 2, distances);
    CHECK_THAT(distances[85], WithinAbs(15.0, 1e-4));
}

TEST_CASE("The rectangle of the 2D plane a camera sees", "[render][light2d]")
{
    // Looking straight down -Z from ten meters, through a view eight meters wide and six high.
    const devex::math::Mat4 view =
        devex::math::lookAt(devex::math::Vec3{1.0f, 2.0f, 10.0f}, devex::math::Vec3{1.0f, 2.0f, 0.0f}, devex::math::Vec3{0.0f, 1.0f, 0.0f});
    const auto seen = devex::render::visiblePlane2D(devex::math::orthographicReverseZ(3.0f, 4.0f / 3.0f, 0.1f, 100.0f) * view);
    REQUIRE(seen.has_value());
    CHECK_THAT(seen->first.x, WithinAbs(-3.0, 1e-3));
    CHECK_THAT(seen->first.y, WithinAbs(-1.0, 1e-3));
    CHECK_THAT(seen->second.x, WithinAbs(5.0, 1e-3));
    CHECK_THAT(seen->second.y, WithinAbs(5.0, 1e-3));

    // A perspective of 90 degrees from ten meters sees ten meters on each side.
    const auto wide = devex::render::visiblePlane2D(
        devex::math::perspectiveReverseZ(devex::math::radians(90.0f), 1.0f, 0.1f) *
        devex::math::lookAt(devex::math::Vec3{0.0f, 0.0f, 10.0f}, devex::math::Vec3{0.0f}, devex::math::Vec3{0.0f, 1.0f, 0.0f}));
    REQUIRE(wide.has_value());
    CHECK_THAT(wide->first.x, WithinAbs(-10.0, 1e-2));
    CHECK_THAT(wide->second.y, WithinAbs(10.0, 1e-2));
}
