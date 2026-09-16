#include <devex/core/BuildInfo.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version matches the CMake project version", "[core][build-info]")
{
    CHECK(devex::core::version() == DEVEX_TEST_EXPECTED_VERSION);
}
