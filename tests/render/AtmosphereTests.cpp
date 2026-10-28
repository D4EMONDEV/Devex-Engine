#include <devex/render/Atmosphere.hpp>

#include <catch2/catch_test_macros.hpp>

using devex::math::Vec3;
using devex::render::RenderAtmosphere;
using devex::render::atmosphereTransmittance;

TEST_CASE("The air lets the sun through white from above, red along the horizon, and not at all below it", "[render][atmosphere]")
{
    const RenderAtmosphere earth{.enabled = true};

    // Straight up from the ground: most of the light, a little less of the blue.
    const Vec3 zenith = atmosphereTransmittance(earth, 0.0f, 1.0f);
    CHECK(zenith.x > 0.85f);
    CHECK(zenith.z > 0.6f);
    CHECK(zenith.x > zenith.z);

    // A few degrees above the horizon: through hundreds of kilometers of air, red and faint.
    const Vec3 low = atmosphereTransmittance(earth, 0.0f, 0.05f);
    CHECK(low.x < zenith.x);
    CHECK(low.x > low.y);
    CHECK(low.y > low.z);
    CHECK(low.z < 0.2f);

    // Higher up, less air is left above.
    const Vec3 mountain = atmosphereTransmittance(earth, 5.0f, 0.05f);
    CHECK(mountain.z > low.z);

    // Below the horizon, the planet stands in the way.
    CHECK(atmosphereTransmittance(earth, 0.0f, -0.2f) == Vec3{0.0f});

    // Without air, everything goes through.
    const RenderAtmosphere empty{.enabled = true, .rayleighScattering = Vec3{0.0f}, .mieScattering = 0.0f, .mieAbsorption = 0.0f,
                                 .ozoneAbsorption = Vec3{0.0f}};
    CHECK(atmosphereTransmittance(empty, 0.0f, 0.3f).z > 0.9999f);
}
