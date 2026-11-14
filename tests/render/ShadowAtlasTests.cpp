#include <devex/render/ShadowAtlas.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using devex::render::LightType;
using devex::render::RenderLight;
namespace math = devex::math;

namespace {

[[nodiscard]] RenderLight shadowed(LightType type, math::Vec3 position, float intensity)
{
    RenderLight light;
    light.type = type;
    light.position = position;
    light.direction = math::Vec3{0.0f, 0.0f, -1.0f};
    light.range = 10.0f;
    light.intensity = math::Vec3{intensity};
    light.outerAngle = math::radians(30.0f);
    light.castShadows = true;
    return light;
}

// A camera at the origin looking down -Z, far enough to see every light of the tests.
[[nodiscard]] devex::render::Frustum cameraFrustum()
{
    const math::Mat4 view = math::lookAt(math::Vec3{0.0f, 0.0f, 5.0f}, math::Vec3{0.0f}, math::Vec3{0.0f, 1.0f, 0.0f});
    return devex::render::frustumOf(math::perspective(math::radians(90.0f), 1.0f, 0.1f, 500.0f) * view);
}

} // namespace

TEST_CASE("The brightest lights the camera sees get the largest tiles of the shadow atlas", "[render][shadows]")
{
    std::vector<RenderLight> lights{
        shadowed(LightType::Spot, {0.0f, 0.0f, -2.0f}, 1.0f),
        shadowed(LightType::Point, {1.0f, 0.0f, -2.0f}, 100.0f),
        shadowed(LightType::Spot, {0.0f, 0.0f, 2000.0f}, 1000.0f),
    };
    lights.push_back(shadowed(LightType::Spot, {0.0f, 1.0f, -2.0f}, 50.0f));
    lights.back().castShadows = false;

    const devex::render::ShadowAtlasLayout atlas{.size = 4096, .cell = 512, .largeLights = 2};
    const devex::render::ShadowAssignment assigned =
        devex::render::assignShadowViews(lights, cameraFrustum(), math::Vec3{0.0f, 0.0f, 5.0f}, atlas, math::Mat4{1.0f});

    // The point light, the brightest, comes first and owns a face of a cube each; the spot follows.
    REQUIRE(assigned.firstView.size() == lights.size());
    CHECK(assigned.firstView[1] == 0);
    CHECK(assigned.firstView[0] == 6);
    CHECK(assigned.views.size() == 7);
    CHECK(assigned.far[1] == 10.0f);
    // Out of sight, or without a shadow asked for, a light gets none.
    CHECK(assigned.firstView[2] == -1);
    CHECK(assigned.firstView[3] == -1);
    // The first two lights take blocks of two by two cells, in the atlas.
    for (const devex::render::ShadowView& view : assigned.views)
    {
        CHECK(view.tile.z == 1024.0f / 4096.0f);
        CHECK(view.tile.x + view.tile.z <= 1.0f);
        CHECK(view.tile.y + view.tile.w <= 1.0f);
    }
}

TEST_CASE("A light whose shadow does not fit in the atlas keeps its light", "[render][shadows]")
{
    // Three cells by three: one block of two by two, where neither point light places its six faces.
    std::vector<RenderLight> lights{shadowed(LightType::Point, {0.0f, 0.0f, -2.0f}, 10.0f),
                                    shadowed(LightType::Point, {1.0f, 0.0f, -2.0f}, 1.0f)};
    const devex::render::ShadowAtlasLayout atlas{.size = 1536, .cell = 512, .largeLights = 2};
    const devex::render::ShadowAssignment assigned =
        devex::render::assignShadowViews(lights, cameraFrustum(), math::Vec3{0.0f, 0.0f, 5.0f}, atlas, math::Mat4{1.0f});
    CHECK(assigned.firstView[0] == -1);
    CHECK(assigned.firstView[1] == -1);
    CHECK(assigned.views.empty());

    // With single cells, one light gets its six faces, and the three cells left do not hold six more.
    const devex::render::ShadowAssignment small = devex::render::assignShadowViews(
        lights, cameraFrustum(), math::Vec3{0.0f, 0.0f, 5.0f}, {.size = 1536, .cell = 512, .largeLights = 0}, math::Mat4{1.0f});
    CHECK(small.firstView[0] == 0);
    CHECK(small.firstView[1] == -1);
    CHECK(small.views.size() == 6);
}
