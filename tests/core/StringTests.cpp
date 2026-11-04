#include <devex/core/String.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("Trimming text preserves its contents and each caller's line endings", "[core][string]")
{
    using devex::core::trimmed;
    CHECK(trimmed("").empty());
    CHECK(trimmed(" \t ").empty());
    CHECK(trimmed("\talpha beta \t") == "alpha beta");
    CHECK(trimmed("\nalpha\r\n") == "\nalpha\r\n");
    CHECK(trimmed(" \tmethod()\r\t ", " \t", " \t\r") == "method()");
    CHECK(trimmed("\rmethod()\r", " \t", " \t\r") == "\rmethod()");
    CHECK(trimmed("\r", " \t", " \t\r").empty());
    constexpr std::string_view text = "  existing text\t";
    CHECK(trimmed(text).data() == text.data() + 2);
    static_assert(trimmed(" x ") == "x");
}
