#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <unordered_set>

using devex::core::Uuid;

TEST_CASE("UUIDs format and parse in canonical form", "[core][uuid]")
{
    const Uuid uuid = Uuid::fromParts(0x6f1c2a9e3b7d4e21ULL, 0x9a550c8d7e4f1b23ULL);

    CHECK(uuid.toString() == "6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23");
    CHECK(std::format("{}", uuid) == "6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23");
    CHECK(Uuid::parse("6F1C2A9E-3B7D-4E21-9A55-0C8D7E4F1B23") == uuid);
}

TEST_CASE("Malformed UUID text is rejected", "[core][uuid]")
{
    CHECK_FALSE(Uuid::parse("").has_value());
    CHECK_FALSE(Uuid::parse("6f1c2a9e3b7d4e219a550c8d7e4f1b23").has_value());
    CHECK_FALSE(Uuid::parse("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b2g").has_value());
    CHECK_FALSE(Uuid::parse("6f1c2a9e-3b7d4-e21-9a55-0c8d7e4f1b23").has_value());
}

TEST_CASE("Generated UUIDs are random version 4 identifiers", "[core][uuid]")
{
    std::unordered_set<Uuid> generated;
    for (int index = 0; index < 1000; ++index)
    {
        const Uuid uuid = Uuid::generate();
        CHECK((uuid.bytes()[6] & 0xF0) == 0x40);
        CHECK((uuid.bytes()[8] & 0xC0) == 0x80);
        generated.insert(uuid);
    }
    CHECK(generated.size() == 1000);
    CHECK(Uuid{}.isNil());
    CHECK_FALSE(Uuid::generate().isNil());
}
