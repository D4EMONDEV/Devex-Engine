#include <devex/math/Math.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using devex::math::Mat4;
using devex::math::Quat;
using devex::math::Trs;
using devex::math::Vec3;

namespace {

void checkMatricesEqual(const Mat4& actual, const Mat4& expected)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            CHECK_THAT(actual[column][row], WithinAbs(expected[column][row], 1e-5));
        }
    }
}

} // namespace

TEST_CASE("TRS decomposition recovers the composed transform", "[math][transform]")
{
    const Trs original{
        .translation = {1.0f, -2.0f, 3.5f},
        .rotation = devex::math::angleAxis(0.7f, devex::math::normalize(Vec3{1.0f, 2.0f, -0.5f})),
        .scale = {2.0f, 0.5f, 1.5f},
    };

    const Trs decomposed = devex::math::decomposeTrs(devex::math::composeTrs(original));

    checkMatricesEqual(devex::math::composeTrs(decomposed), devex::math::composeTrs(original));
    CHECK_THAT(decomposed.scale.y, WithinAbs(0.5, 1e-5));
}

TEST_CASE("Mirroring transforms decompose with a negative scale", "[math][transform]")
{
    const Mat4 mirrored = devex::math::scale(Mat4{1.0f}, Vec3{-1.0f, 1.0f, 1.0f});

    const Trs decomposed = devex::math::decomposeTrs(mirrored);

    CHECK(decomposed.scale.x < 0.0f);
    checkMatricesEqual(devex::math::composeTrs(decomposed), mirrored);
}
