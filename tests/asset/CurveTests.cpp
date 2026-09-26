#include <devex/asset/Artifact.hpp>
#include <devex/asset/CurveData.hpp>
#include <devex/asset/import/CurveFile.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using Catch::Approx;
using devex::asset::CurveData;
using devex::asset::CurveKey;

TEST_CASE("A curve follows its keys and their tangents", "[asset][curve]")
{
    const CurveData line = devex::asset::linearCurve();
    CHECK(line.evaluate(0.0f) == Approx(0.0f));
    CHECK(line.evaluate(0.3f) == Approx(0.3f));
    CHECK(line.evaluate(1.0f) == Approx(1.0f));
    // Held at the first and last values outside the keys.
    CHECK(line.evaluate(-1.0f) == Approx(0.0f));
    CHECK(line.evaluate(4.0f) == Approx(1.0f));

    // Flat tangents: slow at the ends, as a smoothstep.
    CurveData smooth{.keys = {{.time = 0.0f, .value = 0.0f}, {.time = 1.0f, .value = 1.0f}}};
    CHECK(smooth.evaluate(0.5f) == Approx(0.5f));
    CHECK(smooth.evaluate(0.1f) == Approx(0.028f));

    // A key above 1 in the middle overshoots, then comes back.
    CurveData overshoot{.keys = {{.time = 0.0f, .value = 0.0f},
                                 {.time = 0.7f, .value = 1.2f},
                                 {.time = 1.0f, .value = 1.0f}}};
    devex::asset::smoothTangents(overshoot, 1);
    CHECK(overshoot.keys[1].inTangent == Approx(1.0f));
    CHECK(overshoot.evaluate(0.7f) == Approx(1.2f));
    CHECK(overshoot.evaluate(0.85f) > 1.0f);
    CHECK(devex::asset::validate(overshoot).has_value());
}

TEST_CASE("A curve that cannot ease is refused", "[asset][curve]")
{
    CHECK_FALSE(devex::asset::validate(CurveData{.keys = {{.time = 0.0f, .value = 0.0f}}}).has_value());
    CHECK_FALSE(devex::asset::validate(CurveData{.keys = {{.time = 0.5f}, {.time = 0.5f}}}).has_value());
    CHECK_FALSE(devex::asset::validate(CurveData{.keys = {{.time = 1.0f}, {.time = 0.0f}}}).has_value());
    CHECK_FALSE(devex::asset::validate(
                    CurveData{.keys = {{.time = 0.0f, .value = std::numeric_limits<float>::quiet_NaN()}, {.time = 1.0f}}})
                    .has_value());
}

TEST_CASE("A curve file is read, written and cooked", "[asset][curve]")
{
    CurveData curve{.keys = {{.time = 0.0f, .value = 0.0f, .inTangent = 0.0f, .outTangent = 2.5f},
                             {.time = 0.6f, .value = 1.1f, .inTangent = -0.25f, .outTangent = -0.25f},
                             {.time = 1.0f, .value = 1.0f}}};
    const std::string text = devex::asset::writeCurveFile(curve);
    CHECK(text.starts_with("[curve format=1]"));
    CHECK(text.find("value=1.1 ") != std::string::npos);
    devex::core::Result<CurveData> read = devex::asset::parseCurveFile(text);
    REQUIRE(read.has_value());
    CHECK(*read == curve);

    devex::core::Result<CurveData> decoded = devex::asset::decodeCurve(devex::asset::encodeCurve(curve));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == curve);

    CHECK_FALSE(devex::asset::parseCurveFile("[theme]\n").has_value());
    CHECK_FALSE(devex::asset::parseCurveFile("[curve format=2]\n[key time=0 value=0]\n[key time=1 value=1]\n").has_value());
    CHECK_FALSE(devex::asset::parseCurveFile("[curve format=1]\n[key time=0]\n[key time=1 value=1]\n").has_value());
    CHECK_FALSE(devex::asset::parseCurveFile("[curve format=1]\n[key time=0 value=0]\n").has_value());
    const devex::core::Result<CurveData> minimal =
        devex::asset::parseCurveFile("[curve format=1]\n[key time=0 value=0]\n[key time=1 value=1]\n");
    REQUIRE(minimal.has_value());
    CHECK(minimal->keys[0].outTangent == 0.0f);
}
