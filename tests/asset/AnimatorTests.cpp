#include <devex/asset/AnimatorData.hpp>
#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/AnimatorFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using devex::asset::AnimatorBlend;
using devex::asset::AnimatorData;
using devex::asset::AnimatorParameterType;
using devex::asset::AnimatorTest;
using devex::asset::AssetId;

namespace {

[[nodiscard]] AnimatorData sampleAnimator()
{
    const AssetId idle{devex::core::Uuid::fromParts(0x1111, 1)};
    const AssetId walk{devex::core::Uuid::fromParts(0x1111, 2)};
    const AssetId wave{devex::core::Uuid::fromParts(0x1111, 3)};
    AnimatorData animator;
    animator.entry = "Locomotion";
    animator.entryPosition = {-300.0f, 10.0f};
    animator.parameters = {{.name = "Speed", .defaultValue = 0.5f},
                           {.name = "Weapon", .type = AnimatorParameterType::Integer, .defaultValue = 2.0f},
                           {.name = "Grounded", .type = AnimatorParameterType::Bool, .defaultValue = 1.0f},
                           {.name = "Wave", .type = AnimatorParameterType::Trigger},
                           {.name = "Side"}};
    animator.states = {
        {.name = "Locomotion",
         .blend = AnimatorBlend::Linear,
         .motions = {{.clip = idle, .threshold = 0.0f}, {.clip = walk, .threshold = 1.6f}},
         .parameter = "Speed",
         .speedParameter = "Speed",
         .graphPosition = {0.0f, 0.0f}},
        {.name = "Strafe",
         .blend = AnimatorBlend::Planar,
         .motions = {{.clip = idle, .position = {0.0f, 0.0f}}, {.clip = walk, .position = {1.0f, -0.5f}}},
         .parameter = "Side",
         .parameterY = "Speed"},
        {.name = "Waving", .motions = {{.clip = wave}}, .speed = 1.25f, .loop = false, .graphPosition = {200.0f, 80.0f}},
        {.name = "Run", .spriteAnimation = "run"},
    };
    animator.transitions = {
        {.from = "Locomotion",
         .to = "Waving",
         .duration = 0.25f,
         .conditions = {{.parameter = "Wave", .test = AnimatorTest::Triggered},
                        {.parameter = "Speed", .test = AnimatorTest::Less, .value = 0.1f}}},
        {.from = "Waving", .to = "Locomotion", .duration = 0.3f, .exitTime = 0.9f},
        {.to = "Run",
         .duration = 0.0f,
         .conditions = {{.parameter = "Grounded", .test = AnimatorTest::IsFalse},
                        {.parameter = "Weapon", .test = AnimatorTest::NotEquals, .value = 3.0f},
                        {.parameter = "Speed", .test = AnimatorTest::Greater, .value = 2.0f}}},
    };
    return animator;
}

} // namespace

TEST_CASE("Animator files keep states, blend trees, parameters and transitions", "[asset][animator]")
{
    const AnimatorData animator = sampleAnimator();
    REQUIRE(devex::asset::validate(animator).has_value());
    const std::string text = devex::asset::writeAnimatorFile(animator);
    CHECK(text.find("conditions = list(trigger(\"Wave\"), less(\"Speed\", 0.1))") != std::string::npos);
    CHECK(text.find("is(\"Grounded\", false)") != std::string::npos);
    CHECK(text.find("thresholds = list(0, 1.6)") != std::string::npos);
    CHECK(text.find("[transition to=\"Run\"") != std::string::npos);

    const devex::core::Result<AnimatorData> read = devex::asset::parseAnimatorFile(text);
    REQUIRE(read.has_value());
    CHECK(*read == animator);

    const devex::core::Result<AnimatorData> decoded = devex::asset::decodeAnimator(devex::asset::encodeAnimator(animator));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == animator);
}

TEST_CASE("Animators that do not hold together are refused", "[asset][animator]")
{
    const auto refused = [](const auto& change) {
        AnimatorData animator = sampleAnimator();
        change(animator);
        return !devex::asset::validate(animator).has_value();
    };
    CHECK(refused([](AnimatorData& animator) { animator.entry = "Nowhere"; }));
    CHECK(refused([](AnimatorData& animator) { animator.states[1].name = "Locomotion"; }));
    CHECK(refused([](AnimatorData& animator) { animator.parameters[1].name = "Speed"; }));
    CHECK(refused([](AnimatorData& animator) { animator.transitions[0].to = "Nowhere"; }));
    CHECK(refused([](AnimatorData& animator) { animator.transitions[1].from = "Nowhere"; }));
    CHECK(refused([](AnimatorData& animator) { animator.transitions[0].conditions[0].parameter = "Unknown"; }));
    // A trigger is not compared to a number, a float is not equal to one.
    CHECK(refused([](AnimatorData& animator) { animator.transitions[0].conditions[0].test = AnimatorTest::Greater; }));
    CHECK(refused([](AnimatorData& animator) { animator.transitions[0].conditions[1].test = AnimatorTest::Equals; }));
    CHECK(refused([](AnimatorData& animator) { animator.states[0].parameter = "Wave"; }));
    CHECK(refused([](AnimatorData& animator) { animator.states[1].parameterY.clear(); }));
    CHECK(refused([](AnimatorData& animator) { animator.transitions[1].duration = -1.0f; }));
    CHECK_FALSE(refused([](AnimatorData& animator) { animator.transitions.clear(); }));

    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=2 entry=\"\"]").has_value());
    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=1 entry=\"A\"]\n[state name=\"A\" blend=\"3d\"]").has_value());
    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=1 entry=\"A\"]\n[state name=\"A\"]\n"
                                                "[transition to=\"A\" duration=0]\nconditions = list(maybe(\"A\"))")
                    .has_value());
    const devex::core::Result<AnimatorData> empty = devex::asset::parseAnimatorFile("[animator format=1]");
    REQUIRE(empty.has_value());
    CHECK(empty->states.empty());
}
