#include <devex/render/Photometry.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Exposure values map luminance to the display", "[render][photometry]")
{
    CHECK_THAT(devex::render::exposureFromEv100(0.0f), WithinRel(1.0f / 1.2f, 1e-5f));
    CHECK_THAT(devex::render::exposureFromEv100(1.0f), WithinRel(1.0f / 2.4f, 1e-5f));

    // The exposure computed for an average luminance places it at middle grey.
    for (const float luminance : {0.5f, 100.0f, 12000.0f})
    {
        const float ev100 = devex::render::ev100FromAverageLuminance(luminance);
        CHECK_THAT(luminance * devex::render::exposureFromEv100(ev100), WithinRel(0.18f, 1e-4f));
    }
}

TEST_CASE("Light units convert to intensities and colors", "[render][photometry]")
{
    CHECK_THAT(devex::render::luminousIntensityFromPower(4.0f * std::numbers::pi_v<float>), WithinRel(1.0f, 1e-5f));

    const devex::math::Vec3 neutral = devex::render::colorFromTemperature(6500.0f);
    CHECK_THAT(neutral.r, WithinAbs(1.0, 1e-4));
    CHECK_THAT(neutral.g, WithinAbs(1.0, 1e-4));
    CHECK_THAT(neutral.b, WithinAbs(1.0, 1e-4));

    const devex::math::Vec3 warm = devex::render::colorFromTemperature(2700.0f);
    CHECK(warm.r > warm.g);
    CHECK(warm.g > warm.b);
    const devex::math::Vec3 cool = devex::render::colorFromTemperature(12000.0f);
    CHECK(cool.b > cool.r);
    // Colors keep the luminance of white, so temperature does not change brightness.
    CHECK_THAT(0.2126f * warm.r + 0.7152f * warm.g + 0.0722f * warm.b, WithinAbs(1.0, 1e-4));
}

TEST_CASE("Average luminance ignores extremes and exposure adapts smoothly", "[render][photometry]")
{
    std::vector<float> samples(20, 50.0f);
    samples[3] = 1e6f;
    samples[11] = 0.0f;
    CHECK_THAT(devex::render::averageLuminance(samples), WithinRel(50.0f, 1e-4f));
    std::vector<float> empty;
    CHECK(devex::render::averageLuminance(empty) == 0.0f);

    CHECK(devex::render::adaptExposure(10.0f, 14.0f, 2.0f, 0.0f) == 10.0f);
    const float halfway = devex::render::adaptExposure(10.0f, 14.0f, 2.0f, 0.1f);
    CHECK(halfway > 10.0f);
    CHECK(halfway < 14.0f);
    CHECK_THAT(devex::render::adaptExposure(10.0f, 14.0f, 1000.0f, 1.0f), WithinAbs(14.0, 1e-4));
}
