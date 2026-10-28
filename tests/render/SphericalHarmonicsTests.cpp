#include <devex/render/CubeMaps.hpp>
#include <devex/render/SphericalHarmonics.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("Irradiance harmonics give what a surface facing each way reflects", "[render][harmonics]")
{
    constexpr std::uint32_t size = 16;
    // A white sky of 2 nits all around: every surface reflects 2, whichever way it faces.
    std::vector<float> uniform(std::size_t{size} * size * 18, 2.0f);
    const devex::render::IrradianceHarmonics even = devex::render::projectIrradiance(uniform, size);
    for (const devex::math::Vec3 normal : {devex::math::Vec3{0.0f, 1.0f, 0.0f}, devex::math::Vec3{1.0f, 0.0f, 0.0f},
                                           devex::math::Vec3{0.0f, -1.0f, -1.0f}})
    {
        CHECK(devex::render::evaluateIrradiance(even, normal).x == Catch::Approx(2.0f).epsilon(0.01));
    }

    // Light from above only, red: a surface facing up takes nearly all of it, one facing down nearly
    // none, one facing sideways about half.
    std::vector<float> above(uniform.size(), 0.0f);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        for (std::uint32_t y = 0; y < size; ++y)
        {
            for (std::uint32_t x = 0; x < size; ++x)
            {
                if (devex::render::cubeFaceDirection(face, x, y, size).y > 0.0f)
                {
                    above[((std::size_t{face} * size + y) * size + x) * 3] = 1.0f;
                }
            }
        }
    }
    const devex::render::IrradianceHarmonics sky = devex::render::projectIrradiance(above, size);
    CHECK(devex::render::evaluateIrradiance(sky, {0.0f, 1.0f, 0.0f}).x == Catch::Approx(1.0f).margin(0.1));
    CHECK(devex::render::evaluateIrradiance(sky, {0.0f, -1.0f, 0.0f}).x < 0.1f);
    CHECK(devex::render::evaluateIrradiance(sky, {1.0f, 0.0f, 0.0f}).x == Catch::Approx(0.5f).margin(0.05));
    CHECK(devex::render::evaluateIrradiance(sky, {0.0f, 1.0f, 0.0f}).y == 0.0f);
}
