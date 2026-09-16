#include <devex/core/Hash.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using devex::core::hash64;

TEST_CASE("hash64 matches the XXH64 reference values", "[core][hash]")
{
    CHECK(hash64(std::string_view{}) == 0xEF46DB3751D8E999ULL);
    CHECK(hash64("abc") == 0x44BC2CF5AD770999ULL);
}

TEST_CASE("hash64 covers every byte of long inputs", "[core][hash]")
{
    // Long enough for the 32-byte stripes, the 8-byte and 4-byte tails and the last bytes.
    std::string text(103, 'x');
    const std::uint64_t original = hash64(text);
    for (const std::size_t position : {0uz, 31uz, 32uz, 95uz, 99uz, 102uz})
    {
        std::string changed = text;
        changed[position] = 'y';
        CHECK(hash64(changed) != original);
    }
    CHECK(hash64(text, 1) != original);
    CHECK(devex::core::toHex(0xABCull) == "0000000000000abc");
}
