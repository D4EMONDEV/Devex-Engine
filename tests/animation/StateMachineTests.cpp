#include <devex/animation/AnimationWorld.hpp>
#include <devex/animation/Clip.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SpriteComponents.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using devex::animation::AnimationWorld;
using devex::animation::Clip;
using devex::asset::AnimationClipData;
using devex::asset::AnimationPath;
using devex::asset::AnimatorBlend;
using devex::asset::AnimatorData;
using devex::asset::AnimatorParameterType;
using devex::asset::AnimatorTest;
using devex::asset::AssetId;
using devex::core::Duration;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::Transform;

namespace {

// A clip that holds the root bone at a distance along X, which tells what plays and how much.
[[nodiscard]] std::shared_ptr<const Clip> holdClip(float x, float duration = 1.0f)
{
    AnimationClipData data;
    data.name = "Hold";
    data.duration = duration;
    data.joints = {"Root"};
    data.channels.push_back({.joint = 0, .path = AnimationPath::Translation, .times = {0.0f, duration},
                             .values = {x, 0.0f, 0.0f, x, 0.0f, 0.0f}});
    devex::core::Result<std::shared_ptr<const Clip>> clip = Clip::create(std::move(data));
    REQUIRE(clip.has_value());
    return *clip;
}

// Clips at X = 0, 10, 20 and 30 for idle, walk, run and jump.
struct Clips
{
    AssetId idle = AssetId::generate();
    AssetId walk = AssetId::generate();
    AssetId run = AssetId::generate();
    AssetId jump = AssetId::generate();
    std::unordered_map<AssetId, std::shared_ptr<const Clip>> loaded{
        {idle, holdClip(0.0f)}, {walk, holdClip(10.0f)}, {run, holdClip(20.0f, 2.0f)}, {jump, holdClip(30.0f)}};

    [[nodiscard]] std::shared_ptr<const Clip> operator()(AssetId id) const
    {
        const auto found = loaded.find(id);
        return found != loaded.end() ? found->second : nullptr;
    }
};

struct Rig
{
    Scene scene;
    Entity character;
    Entity root;
    AssetId controllerId = AssetId::generate();

    explicit Rig(bool withSprite = false)
    {
        character = scene.createEntity("Character");
        scene.add<Transform>(character);
        scene.add<devex::scene::Animator>(character, devex::scene::Animator{.controller = controllerId});
        root = scene.createEntity("Root");
        scene.add<Transform>(root);
        REQUIRE(scene.setParent(root, character));
        if (withSprite)
        {
            scene.add<devex::scene::SpriteAnimator>(character);
        }
    }

    [[nodiscard]] float rootX() const
    {
        return scene.get<Transform>(root).position.x;
    }
};

[[nodiscard]] AnimatorData locomotion(const Clips& clips)
{
    AnimatorData controller;
    controller.entry = "Idle";
    controller.parameters = {{.name = "Speed"}, {.name = "Jump", .type = AnimatorParameterType::Trigger}};
    controller.states = {
        {.name = "Idle", .motions = {{.clip = clips.idle}}},
        {.name = "Walk", .motions = {{.clip = clips.walk}}},
        {.name = "Jump", .motions = {{.clip = clips.jump}}, .loop = false},
    };
    controller.transitions = {
        {.from = "Idle", .to = "Walk", .duration = 0.0f, .conditions = {{.parameter = "Speed", .test = AnimatorTest::Greater, .value = 0.5f}}},
        {.from = "Walk", .to = "Idle", .duration = 0.0f, .conditions = {{.parameter = "Speed", .test = AnimatorTest::Less, .value = 0.5f}}},
        {.to = "Jump", .duration = 0.0f, .conditions = {{.parameter = "Jump", .test = AnimatorTest::Triggered}}},
        // No condition: it leaves at the end of the jump.
        {.from = "Jump", .to = "Idle", .duration = 0.0f},
    };
    return controller;
}

} // namespace

TEST_CASE("State machines start in their entry state and follow their transitions", "[animation][statemachine]")
{
    const Clips clips;
    Rig rig;
    const auto controller = std::make_shared<const AnimatorData>(locomotion(clips));
    AnimationWorld world(std::ref(clips), [&](AssetId id) { return id == rig.controllerId ? controller : nullptr; });

    world.update(rig.scene, Duration(0.1));
    CHECK(world.state(rig.character) == "Idle");
    CHECK(rig.rootX() == Catch::Approx(0.0f));
    CHECK(world.parameter(rig.character, "Speed") == 0.0f);

    world.setFloat(rig.character, "Speed", 1.0f);
    world.update(rig.scene, Duration(0.1));
    CHECK(world.state(rig.character) == "Walk");
    CHECK(rig.rootX() == Catch::Approx(10.0f));

    // A trigger lets an "any state" transition through, and is reset by it.
    world.setTrigger(rig.character, "Jump");
    world.update(rig.scene, Duration(0.1));
    CHECK(world.state(rig.character) == "Jump");
    CHECK(rig.rootX() == Catch::Approx(30.0f));
    CHECK(world.parameter(rig.character, "Jump") == 0.0f);
    world.update(rig.scene, Duration(0.5));
    CHECK(world.state(rig.character) == "Jump");
    CHECK(world.stateTime(rig.character) == Catch::Approx(0.6f));

    // At its end, the jump goes back to idle, which walks on at once as the speed is still up.
    world.update(rig.scene, Duration(0.5));
    world.update(rig.scene, Duration(0.1));
    CHECK(world.state(rig.character) == "Idle");
    world.update(rig.scene, Duration(0.1));
    CHECK(world.state(rig.character) == "Walk");

    // Stopping goes back to the entry state; playing starts again from there.
    world.stop(rig.character);
    CHECK(world.state(rig.character) == "Idle");
    CHECK_FALSE(world.isPlaying(rig.character));
    world.play(rig.scene, rig.character);
    CHECK(world.isPlaying(rig.character));
}

TEST_CASE("Transitions crossfade between states and wait for their exit time", "[animation][statemachine]")
{
    const Clips clips;
    Rig rig;
    AnimatorData data = locomotion(clips);
    data.transitions[0].duration = 0.5f;
    // Walking stops only past the middle of the walk.
    data.transitions[1].exitTime = 0.5f;
    const auto controller = std::make_shared<const AnimatorData>(data);
    AnimationWorld world(std::ref(clips), [&](AssetId id) { return id == rig.controllerId ? controller : nullptr; });
    world.update(rig.scene, Duration(0.0));

    world.setFloat(rig.character, "Speed", 1.0f);
    world.update(rig.scene, Duration(0.25));
    CHECK(world.state(rig.character) == "Walk");
    CHECK(rig.rootX() == Catch::Approx(5.0f).margin(1e-3f));
    const std::optional<devex::animation::AnimatorStatus> status = world.status(rig.character);
    REQUIRE(status);
    CHECK(status->previousState == "Idle");
    CHECK(status->transitionProgress == Catch::Approx(0.5f));
    REQUIRE(status->parameters.size() == 2);
    CHECK(status->parameters[0].name == "Speed");
    CHECK(status->parameters[0].value == 1.0f);

    world.update(rig.scene, Duration(0.25));
    CHECK(rig.rootX() == Catch::Approx(10.0f));
    CHECK(world.status(rig.character)->previousState.empty());

    // Walking again, from its start: it may stop only past the middle of its cycle.
    world.stop(rig.character);
    world.play(rig.scene, rig.character);
    world.update(rig.scene, Duration(0.0));
    REQUIRE(world.state(rig.character) == "Walk");
    world.setFloat(rig.character, "Speed", 0.0f);
    world.update(rig.scene, Duration(0.2));
    world.update(rig.scene, Duration(0.2));
    CHECK(world.state(rig.character) == "Walk");
    world.update(rig.scene, Duration(0.2));
    world.update(rig.scene, Duration(0.0));
    CHECK(world.state(rig.character) == "Idle");
}

TEST_CASE("Linear blend trees mix their clips by a parameter", "[animation][statemachine]")
{
    const std::vector<float> thresholds{3.0f, 0.0f, 1.0f};
    CHECK(devex::animation::linearBlendWeights(thresholds, -1.0f) == std::vector<float>{0.0f, 1.0f, 0.0f});
    CHECK(devex::animation::linearBlendWeights(thresholds, 5.0f) == std::vector<float>{1.0f, 0.0f, 0.0f});
    CHECK(devex::animation::linearBlendWeights(thresholds, 2.0f) == std::vector<float>{0.5f, 0.0f, 0.5f});
    CHECK(devex::animation::linearBlendWeights(thresholds, 0.25f) == std::vector<float>{0.0f, 0.75f, 0.25f});

    const Clips clips;
    Rig rig;
    AnimatorData data;
    data.entry = "Locomotion";
    data.parameters = {{.name = "Speed"}};
    data.states = {{.name = "Locomotion",
                    .blend = AnimatorBlend::Linear,
                    .motions = {{.clip = clips.idle, .threshold = 0.0f},
                                {.clip = clips.walk, .threshold = 1.0f},
                                {.clip = clips.run, .threshold = 3.0f}},
                    .parameter = "Speed"}};
    const auto controller = std::make_shared<const AnimatorData>(data);
    AnimationWorld world(std::ref(clips), [&](AssetId id) { return id == rig.controllerId ? controller : nullptr; });

    world.setFloat(rig.character, "Speed", 2.0f);
    world.update(rig.scene, Duration(0.0));
    CHECK(rig.rootX() == Catch::Approx(15.0f));
    // The state lasts as long as its clips, weighted: half of 1 s and half of 2 s.
    world.update(rig.scene, Duration(0.75));
    CHECK(world.stateTime(rig.character) == Catch::Approx(0.5f));
    CHECK(world.time(rig.character) == Catch::Approx(0.75f));
    world.setFloat(rig.character, "Speed", 0.5f);
    world.update(rig.scene, Duration(0.0));
    CHECK(rig.rootX() == Catch::Approx(5.0f));
}

TEST_CASE("Planar blend trees weigh their clips by where they sit", "[animation][statemachine]")
{
    const std::vector<devex::math::Vec2> positions{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}};
    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        const std::vector<float> weights = devex::animation::planarBlendWeights(positions, positions[index]);
        for (std::size_t other = 0; other < positions.size(); ++other)
        {
            CHECK(weights[other] == Catch::Approx(other == index ? 1.0f : 0.0f).margin(1e-5f));
        }
    }
    const std::vector<float> between = devex::animation::planarBlendWeights(positions, {0.5f, 0.0f});
    CHECK(between[0] == Catch::Approx(0.5f));
    CHECK(between[1] == Catch::Approx(0.5f));
    const std::vector<float> diagonal = devex::animation::planarBlendWeights(positions, {0.5f, 0.5f});
    CHECK(diagonal[1] == Catch::Approx(diagonal[2]));
    CHECK(diagonal[0] + diagonal[1] + diagonal[2] + diagonal[3] + diagonal[4] == Catch::Approx(1.0f));
    CHECK(diagonal[3] == 0.0f);

    const Clips clips;
    Rig rig;
    AnimatorData data;
    data.entry = "Move";
    data.parameters = {{.name = "X"}, {.name = "Y"}};
    data.states = {{.name = "Move",
                    .blend = AnimatorBlend::Planar,
                    .motions = {{.clip = clips.idle, .position = {0.0f, 0.0f}},
                                {.clip = clips.walk, .position = {1.0f, 0.0f}},
                                {.clip = clips.jump, .position = {0.0f, 1.0f}}},
                    .parameter = "X",
                    .parameterY = "Y"}};
    const auto controller = std::make_shared<const AnimatorData>(data);
    AnimationWorld world(std::ref(clips), [&](AssetId id) { return id == rig.controllerId ? controller : nullptr; });
    world.setFloat(rig.character, "Y", 1.0f);
    world.update(rig.scene, Duration(0.0));
    CHECK(rig.rootX() == Catch::Approx(30.0f));
    world.setFloat(rig.character, "X", 0.5f);
    world.setFloat(rig.character, "Y", 0.0f);
    world.update(rig.scene, Duration(0.0));
    CHECK(rig.rootX() == Catch::Approx(5.0f));
}

TEST_CASE("States play the sprite animations they name", "[animation][statemachine]")
{
    const Clips clips;
    Rig rig(true);
    const AssetId framesId = AssetId::generate();
    rig.scene.get<devex::scene::SpriteAnimator>(rig.character).frames = framesId;
    auto frames = std::make_shared<devex::asset::SpriteFramesData>();
    frames->animations = {{.name = "idle", .fps = 4.0f, .frames = {AssetId::generate()}},
                          {.name = "run", .fps = 8.0f, .frames = {AssetId::generate(), AssetId::generate(), AssetId::generate(), AssetId::generate()}}};
    AnimatorData data;
    data.entry = "Idle";
    data.parameters = {{.name = "Running", .type = AnimatorParameterType::Bool}};
    data.states = {{.name = "Idle", .spriteAnimation = "idle"}, {.name = "Run", .spriteAnimation = "run"}};
    data.transitions = {{.from = "Idle", .to = "Run", .duration = 0.0f, .conditions = {{.parameter = "Running", .test = AnimatorTest::IsTrue}}},
                        {.from = "Run", .to = "Idle", .duration = 0.0f, .conditions = {{.parameter = "Running", .test = AnimatorTest::IsFalse}}}};
    const auto controller = std::make_shared<const AnimatorData>(data);
    AnimationWorld world(std::ref(clips), [&](AssetId id) { return id == rig.controllerId ? controller : nullptr; },
                         [&](AssetId id) { return id == framesId ? frames : nullptr; });

    world.update(rig.scene, Duration(0.0));
    CHECK(rig.scene.get<devex::scene::SpriteAnimator>(rig.character).animation == "idle");
    world.setBool(rig.character, "Running", true);
    world.update(rig.scene, Duration(0.25));
    CHECK(world.state(rig.character) == "Run");
    CHECK(rig.scene.get<devex::scene::SpriteAnimator>(rig.character).animation == "run");
    CHECK(rig.scene.get<devex::scene::SpriteAnimator>(rig.character).playing);
    // Four frames at 8 per second: half a second.
    CHECK(world.stateTime(rig.character) == Catch::Approx(0.5f));
}

TEST_CASE("Animators take a new version of their controller and keep their state", "[animation][statemachine]")
{
    const Clips clips;
    Rig rig;
    std::shared_ptr<const AnimatorData> controller = std::make_shared<const AnimatorData>(locomotion(clips));
    AnimationWorld world(std::ref(clips), [&](AssetId id) { return id == rig.controllerId ? controller : nullptr; });
    world.setFloat(rig.character, "Speed", 1.0f);
    world.update(rig.scene, Duration(0.1));
    REQUIRE(world.state(rig.character) == "Walk");

    // The walk now plays the run clip, and a parameter appears with its value.
    AnimatorData changed = locomotion(clips);
    changed.states[1].motions[0].clip = clips.run;
    changed.parameters.push_back({.name = "Weight", .defaultValue = 0.25f});
    controller = std::make_shared<const AnimatorData>(changed);
    world.update(rig.scene, Duration(0.1));
    CHECK(world.state(rig.character) == "Walk");
    CHECK(rig.rootX() == Catch::Approx(20.0f));
    CHECK(world.parameter(rig.character, "Weight") == 0.25f);
    CHECK(world.parameter(rig.character, "Speed") == 1.0f);

    // Without its controller, the Animator plays its clip again.
    rig.scene.get<devex::scene::Animator>(rig.character).controller = {};
    rig.scene.get<devex::scene::Animator>(rig.character).clip = clips.jump;
    world.update(rig.scene, Duration(0.1));
    CHECK(world.state(rig.character).empty());
    CHECK(rig.rootX() == Catch::Approx(30.0f));
}

TEST_CASE("State machines pass the events of the clip that weighs the most", "[animation][statemachine][events]")
{
    Clips clips;
    // The walk takes a step at the half of its second.
    AnimationClipData walk;
    walk.name = "Walk";
    walk.duration = 1.0f;
    walk.joints = {"Root"};
    walk.channels.push_back({.joint = 0, .path = AnimationPath::Translation, .times = {0.0f, 1.0f},
                             .values = {10.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f}});
    walk.events = {{.time = 0.5f, .name = "step"}};
    devex::core::Result<std::shared_ptr<const Clip>> walking = Clip::create(std::move(walk));
    REQUIRE(walking.has_value());
    clips.loaded[clips.walk] = *walking;
    Rig rig;
    const auto controller = std::make_shared<const AnimatorData>(locomotion(clips));
    AnimationWorld world(std::ref(clips), [&](AssetId id) { return id == rig.controllerId ? controller : nullptr; });

    world.update(rig.scene, Duration(0.0));
    world.setFloat(rig.character, "Speed", 1.0f);
    world.update(rig.scene, Duration(0.25));
    CHECK(world.state(rig.character) == "Walk");
    CHECK(world.events().empty());
    world.update(rig.scene, Duration(0.5));
    REQUIRE(world.events().size() == 1);
    CHECK(world.events().front().name == "step");
    CHECK(world.events().front().entity == rig.character);
    // Around the loop of the state.
    world.update(rig.scene, Duration(1.0));
    CHECK(world.events().size() == 1);
    // The idle clip has none.
    world.setFloat(rig.character, "Speed", 0.0f);
    world.update(rig.scene, Duration(1.0));
    CHECK(world.events().empty());
}
