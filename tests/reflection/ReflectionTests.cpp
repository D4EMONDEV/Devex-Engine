#include <devex/reflection/Reflection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace test {

enum class Mood : std::uint8_t
{
    Calm,
    Angry,
};

struct Health
{
    float current = 100.0f;
    std::int32_t lives = 3;
    std::string label;
    devex::math::Vec3 respawnPoint{0.0f};
};
DEVEX_DECLARE_REFLECTION(Health);

DEVEX_REFLECT(Health)
{
    type.field("current", &Health::current)
        .field("lives", &Health::lives)
        .field("label", &Health::label)
        .field("respawn_point", &Health::respawnPoint);
}

struct Creature
{
    Mood mood = Mood::Calm;
    float heading = 0.0f;
    devex::math::Vec3 tint{1.0f};
};
DEVEX_DECLARE_REFLECTION(Creature);

} // namespace test

template <>
struct devex::reflection::EnumNames<test::Mood>
{
    static constexpr std::array<std::string_view, 2> names{"calm", "angry"};
};

namespace test {

DEVEX_REFLECT(Creature)
{
    type.field("mood", &Creature::mood)
        .field("heading", &Creature::heading, {.angle = true})
        .field("tint", &Creature::tint, {.color = true});
}

} // namespace test

using devex::reflection::ValueKind;

TEST_CASE("Reflected types list their fields in registration order", "[reflection]")
{
    const devex::reflection::TypeInfo& info = devex::reflection::typeInfo<test::Health>();

    CHECK(info.name == "Health");
    REQUIRE(info.fields.size() == 4);
    CHECK(info.fields[0].name == "current");
    CHECK(info.fields[0].kind == ValueKind::Float);
    CHECK(info.fields[1].kind == ValueKind::Int32);
    CHECK(info.fields[2].kind == ValueKind::String);
    CHECK(info.fields[3].kind == ValueKind::Vec3);
    CHECK(info.findField("respawn_point") == &info.fields[3]);
    CHECK(info.findField("missing") == nullptr);
}

TEST_CASE("Field addresses give access to the values of an object", "[reflection]")
{
    const devex::reflection::TypeInfo& info = devex::reflection::typeInfo<test::Health>();
    test::Health health;

    *static_cast<float*>(info.findField("current")->address(&health)) = 42.0f;
    *static_cast<std::string*>(info.findField("label")->address(&health)) = "boss";

    CHECK(health.current == 42.0f);
    CHECK(health.label == "boss");
    const test::Health& constant = health;
    CHECK(*static_cast<const std::int32_t*>(info.findField("lives")->address(&constant)) == 3);
}

TEST_CASE("Enumerations are reflected with their names and hints are kept", "[reflection]")
{
    const devex::reflection::TypeInfo& info = devex::reflection::typeInfo<test::Creature>();
    const devex::reflection::FieldInfo& mood = info.fields[0];
    CHECK(mood.kind == ValueKind::Enum);
    CHECK(mood.enumSize == 1);
    CHECK(mood.enumNames == std::vector<std::string_view>{"calm", "angry"});
    CHECK(info.fields[1].angle);
    CHECK(info.fields[2].color);

    test::Creature creature;
    devex::reflection::writeEnumIndex(mood, mood.address(&creature), 1);
    CHECK(creature.mood == test::Mood::Angry);
    CHECK(devex::reflection::readEnumIndex(mood, mood.address(&creature)) == 1);
}
