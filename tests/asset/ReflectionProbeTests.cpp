#include <devex/asset/Artifact.hpp>
#include <devex/asset/ReflectionProbeData.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using devex::asset::ReflectionProbeData;

TEST_CASE("Reflection probes keep the light of their faces in nits, however bright", "[asset][probe]")
{
    constexpr std::uint32_t size = 4;
    // A dim room, and the sun through a window of the first face, far brighter than half floats hold.
    std::vector<float> rgb(std::size_t{size} * size * 3 * 6, 2.5f);
    rgb[0] = 400000.0f;
    rgb[1] = 380000.0f;
    rgb[2] = 300000.0f;
    // Values that are no light at all stay dark.
    rgb[3] = -1.0f;
    const ReflectionProbeData probe = devex::asset::makeReflectionProbe(size, rgb);
    REQUIRE(devex::asset::validate(probe).has_value());
    CHECK(probe.scale < 1.0f);
    CHECK(devex::asset::probeTexel(probe, 0, 0, 0).red == Catch::Approx(400000.0f).epsilon(0.002));
    CHECK(devex::asset::probeTexel(probe, 0, 0, 0).blue == Catch::Approx(300000.0f).epsilon(0.002));
    CHECK(devex::asset::probeTexel(probe, 0, 1, 0).red == 0.0f);
    CHECK(devex::asset::probeTexel(probe, 5, 3, 3).green == Catch::Approx(2.5f).epsilon(0.002));
    // Light within half floats keeps its values as they are.
    CHECK(devex::asset::makeReflectionProbe(size, std::vector<float>(rgb.size(), 10.0f)).scale == 1.0f);

    const devex::core::Result<ReflectionProbeData> decoded =
        devex::asset::decodeReflectionProbe(devex::asset::encodeReflectionProbe(probe));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == probe);

    ReflectionProbeData broken = probe;
    broken.pixels.pop_back();
    CHECK_FALSE(devex::asset::validate(broken).has_value());
    broken = probe;
    broken.size = 0;
    CHECK_FALSE(devex::asset::validate(broken).has_value());
    broken = probe;
    broken.scale = 0.0f;
    CHECK_FALSE(devex::asset::validate(broken).has_value());
}
