#include <devex/core/Error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>

using devex::core::Error;
using devex::core::ErrorCode;
using devex::core::makeError;
using devex::core::Result;

namespace {

Result<int> parseDigit(char character)
{
    if (character < '0' || character > '9')
    {
        return makeError(ErrorCode::Parse, "'{}' is not a digit", character);
    }
    return character - '0';
}

Result<void> requireEven(int value)
{
    if (value % 2 != 0)
    {
        return makeError(ErrorCode::InvalidArgument, "{} is odd", value);
    }
    return {};
}

} // namespace

TEST_CASE("Result holds the value on success", "[core][error]")
{
    const Result<int> digit = parseDigit('7');

    REQUIRE(digit.has_value());
    CHECK(*digit == 7);
}

TEST_CASE("makeError keeps the code and formats the message", "[core][error]")
{
    const Result<int> digit = parseDigit('x');

    REQUIRE_FALSE(digit.has_value());
    CHECK(digit.error().code == ErrorCode::Parse);
    CHECK(digit.error().message == "'x' is not a digit");
}

TEST_CASE("Result<void> chains with monadic operations", "[core][error]")
{
    const Result<void> even = parseDigit('4').and_then(requireEven);
    const Result<void> odd = parseDigit('3').and_then(requireEven);
    const Result<void> invalid = parseDigit('?').and_then(requireEven);

    CHECK(even.has_value());
    REQUIRE_FALSE(odd.has_value());
    CHECK(odd.error().code == ErrorCode::InvalidArgument);
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::Parse);
}

TEST_CASE("Errors format with their code name", "[core][error]")
{
    CHECK(std::format("{}", ErrorCode::NotFound) == "NotFound");
    CHECK(std::format("{}", Error{ErrorCode::Io, "disk full"}) == "Io: disk full");
    CHECK(std::format("{}", Error{ErrorCode::Unsupported, {}}) == "Unsupported");
}
