#include <devex/asset/AnimatorData.hpp>
#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/AnimatorFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using devex::asset::AnimatorBlend;
using devex::asset::AnimatorData;
using devex::asset::AnimatorLayer;
using devex::asset::AnimatorLayerBlend;
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
    animator.layers[0].entry = "Locomotion";
    animator.layers[0].entryPosition = {-300.0f, 10.0f};
    animator.parameters = {{.name = "Speed", .defaultValue = 0.5f},
                           {.name = "Weapon", .type = AnimatorParameterType::Integer, .defaultValue = 2.0f},
                           {.name = "Grounded", .type = AnimatorParameterType::Bool, .defaultValue = 1.0f},
                           {.name = "Wave", .type = AnimatorParameterType::Trigger},
                           {.name = "Side"}};
    animator.layers[0].states = {
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
    animator.layers[0].transitions = {
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
    // A layer that waves an arm over the rest, and one that breathes on top of everything.
    animator.layers.push_back(AnimatorLayer{
        .name = "Gestures",
        .entry = "Empty",
        .states = {{.name = "Empty"}, {.name = "Waving", .motions = {{.clip = wave}}, .graphPosition = {240.0f, 0.0f}}},
        .transitions = {{.from = "Empty", .to = "Waving", .duration = 0.25f, .conditions = {{.parameter = "Wave", .test = AnimatorTest::Triggered}}},
                        {.from = "Waving", .to = "Empty", .duration = 0.3f}},
        .weight = 0.75f,
        .weightParameter = "Side",
        .mask = {"ArmRight", "Head"},
        .maskExcluded = {"HandRight"},
        .entryPosition = {-200.0f, 40.0f},
    });
    animator.layers.push_back(AnimatorLayer{.name = "Breath",
                                            .entry = "Breathing",
                                            .states = {{.name = "Breathing", .motions = {{.clip = idle}}}},
                                            .blending = AnimatorLayerBlend::Additive});
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
    CHECK(text.find("[layer name=\"Gestures\" entry=\"Empty\" blending=\"override\" weight=0.75]") != std::string::npos);
    CHECK(text.find("mask = list(\"ArmRight\", \"Head\")") != std::string::npos);
    CHECK(text.find("mask_excluded = list(\"HandRight\")") != std::string::npos);
    CHECK(text.find("blending=\"additive\"") != std::string::npos);
    // The states of a layer come after it, those of the base layer before the first layer.
    CHECK(text.find("[state name=\"Run\"") < text.find("[layer name=\"Gestures\""));
    CHECK(text.find("[state name=\"Waving\"", text.find("[layer name=\"Gestures\"")) != std::string::npos);

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
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].entry = "Nowhere"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].states[1].name = "Locomotion"; }));
    CHECK(refused([](AnimatorData& animator) { animator.parameters[1].name = "Speed"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].transitions[0].to = "Nowhere"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].transitions[1].from = "Nowhere"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].transitions[0].conditions[0].parameter = "Unknown"; }));
    // A trigger is not compared to a number, a float is not equal to one.
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].transitions[0].conditions[0].test = AnimatorTest::Greater; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].transitions[0].conditions[1].test = AnimatorTest::Equals; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].states[0].parameter = "Wave"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].states[1].parameterY.clear(); }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[0].transitions[1].duration = -1.0f; }));
    CHECK_FALSE(refused([](AnimatorData& animator) { animator.layers[0].transitions.clear(); }));
    // Layers: named once each, weighed from 0 to 1 by a number, with their own states.
    CHECK(refused([](AnimatorData& animator) { animator.layers.clear(); }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[2].name = "Gestures"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[1].name.clear(); }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[1].weight = 1.5f; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[1].weightParameter = "Wave"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[1].entry = "Locomotion"; }));
    CHECK(refused([](AnimatorData& animator) { animator.layers[1].transitions[0].to = "Run"; }));
    // State names are unique in their layer only.
    CHECK_FALSE(refused([](AnimatorData& animator) {
        animator.layers[1].states[0].name = "Locomotion";
        animator.layers[1].entry = "Locomotion";
        animator.layers[1].transitions.clear();
    }));

    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=3 entry=\"\"]").has_value());
    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=2]\n[layer name=\"A\" blending=\"multiply\"]").has_value());
    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=2]\n[layer name=\"A\"]\nmask = list(1)").has_value());
    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=1 entry=\"A\"]\n[state name=\"A\" blend=\"3d\"]").has_value());
    CHECK_FALSE(devex::asset::parseAnimatorFile("[animator format=1 entry=\"A\"]\n[state name=\"A\"]\n"
                                                "[transition to=\"A\" duration=0]\nconditions = list(maybe(\"A\"))")
                    .has_value());
    const devex::core::Result<AnimatorData> empty = devex::asset::parseAnimatorFile("[animator format=1]");
    REQUIRE(empty.has_value());
    REQUIRE(empty->layers.size() == 1);
    CHECK(empty->layers.front().name == devex::asset::baseLayerName);
    CHECK(empty->layers.front().states.empty());
}

TEST_CASE("Animators written before layers read as their base layer", "[asset][animator]")
{
    const devex::core::Result<AnimatorData> read = devex::asset::parseAnimatorFile(
        "[animator format=1 entry=\"Idle\"]\n[parameter name=\"Go\" type=\"bool\"]\n[state name=\"Idle\"]\n[state name=\"Walk\"]\n"
        "[transition from=\"Idle\" to=\"Walk\" duration=0.1]\nconditions = list(is(\"Go\", true))\n");
    REQUIRE(read.has_value());
    REQUIRE(read->layers.size() == 1);
    CHECK(read->layers[0].entry == "Idle");
    CHECK(read->layers[0].states.size() == 2);
    CHECK(read->layers[0].transitions.size() == 1);
    CHECK(read->findLayer("") == 0u);
    CHECK(read->findLayer("Base Layer") == 0u);
    CHECK_FALSE(read->findLayer("Arms").has_value());
}

TEST_CASE("Masks name bones, which bring the bones under them", "[asset][animator]")
{
    const AnimatorLayer layer{.mask = {"ArmRight"}, .maskExcluded = {"HandRight"}};
    CHECK(devex::asset::maskNames(layer, "ArmRight") == true);
    CHECK(devex::asset::maskNames(layer, "HandRight") == false);
    CHECK_FALSE(devex::asset::maskNames(layer, "Spine").has_value());
}
