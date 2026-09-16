#include <devex/runtime/FixedTimestep.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>

using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;
using devex::runtime::FixedTimestep;

TEST_CASE("Frame time accumulates into whole fixed steps", "[runtime][timestep]")
{
    FixedTimestep timestep(10ms);

    CHECK(timestep.advance(25ms) == 2);
    CHECK_THAT(timestep.alpha(), WithinAbs(0.5, 1e-9));

    CHECK(timestep.advance(3ms) == 0);
    CHECK_THAT(timestep.alpha(), WithinAbs(0.8, 1e-9));

    CHECK(timestep.advance(2ms) == 1);
    CHECK_THAT(timestep.alpha(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Long frames are clamped to avoid a spiral of death", "[runtime][timestep]")
{
    FixedTimestep timestep(10ms, 250ms);

    CHECK(timestep.advance(2s) == 25);
}

TEST_CASE("Negative frame times are ignored", "[runtime][timestep]")
{
    FixedTimestep timestep(10ms);

    CHECK(timestep.advance(-5ms) == 0);
    CHECK_THAT(timestep.alpha(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("A rate in hertz converts to a step without drift", "[runtime][timestep]")
{
    FixedTimestep timestep = FixedTimestep::fromRate(60);

    CHECK(timestep.step() == 16'666'666ns);

    std::uint32_t steps = 0;
    for (int frame = 0; frame < 600; ++frame)
    {
        steps += timestep.advance(16'666'666ns);
    }
    CHECK(steps == 600);
}
