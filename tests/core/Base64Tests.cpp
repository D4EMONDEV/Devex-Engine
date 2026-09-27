#include <devex/core/Base64.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text)
{
    std::vector<std::byte> bytes;
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(character));
    }
    return bytes;
}

} // namespace

TEST_CASE("Base64 writes the vectors of RFC 4648 and reads them back", "[core][base64]")
{
    const std::vector<std::pair<std::string_view, std::string_view>> vectors{
        {"", ""},         {"f", "Zg=="},        {"fo", "Zm8="},         {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"},
    };
    for (const auto& [text, encoded] : vectors)
    {
        CHECK(devex::core::encodeBase64(bytesOf(text)) == encoded);
        const auto decoded = devex::core::decodeBase64(encoded);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == bytesOf(text));
    }
    // Every byte value survives.
    std::vector<std::byte> all;
    for (int value = 0; value < 256; ++value)
    {
        all.push_back(static_cast<std::byte>(value));
    }
    CHECK(devex::core::decodeBase64(devex::core::encodeBase64(all)) == all);
}

TEST_CASE("Text that is not base64 is refused", "[core][base64]")
{
    CHECK_FALSE(devex::core::decodeBase64("Zm9").has_value());
    CHECK_FALSE(devex::core::decodeBase64("Zm9$").has_value());
    CHECK_FALSE(devex::core::decodeBase64("Z===").has_value());
    CHECK_FALSE(devex::core::decodeBase64("Zg==Zm9v").has_value());
}
