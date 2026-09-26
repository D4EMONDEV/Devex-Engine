#include <devex/math/Easing.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using devex::math::Ease;
using devex::math::ease;

TEST_CASE("Every ease starts at 0 and ends at 1", "[math][easing]")
{
    for (std::uint8_t index = 0; index < devex::math::easeCount; ++index)
    {
        const auto curve = static_cast<Ease>(index);
        INFO("ease " << static_cast<int>(index));
        CHECK(ease(curve, 0.0f) == Approx(0.0f).margin(1e-5));
        CHECK(ease(curve, 1.0f) == Approx(1.0f).margin(1e-5));
        // Outside [0, 1], the progress is held at the ends.
        CHECK(ease(curve, -1.0f) == Approx(0.0f).margin(1e-5));
        CHECK(ease(curve, 2.0f) == Approx(1.0f).margin(1e-5));
    }
}

TEST_CASE("Eases have the shapes of their names", "[math][easing]")
{
    CHECK(ease(Ease::Linear, 0.25f) == Approx(0.25f));
    CHECK(ease(Ease::InQuad, 0.5f) == Approx(0.25f));
    CHECK(ease(Ease::OutQuad, 0.5f) == Approx(0.75f));
    CHECK(ease(Ease::InOutQuad, 0.5f) == Approx(0.5f));
    CHECK(ease(Ease::InOutSine, 0.5f) == Approx(0.5f));
    CHECK(ease(Ease::InCubic, 0.5f) < ease(Ease::InQuad, 0.5f));
    // Back pulls away before it goes, and goes beyond before it comes back.
    CHECK(ease(Ease::InBack, 0.2f) < 0.0f);
    CHECK(ease(Ease::OutBack, 0.8f) > 1.0f);
    CHECK(ease(Ease::OutElastic, 0.2f) > 1.0f);
    // Bounce touches the end before it lands there.
    CHECK(ease(Ease::OutBounce, 1.0f / 2.75f) == Approx(1.0f));
    CHECK(ease(Ease::OutBounce, 0.5f) < 1.0f);
    CHECK(ease(Ease::InOutBounce, 0.5f) == Approx(0.5f));
}
