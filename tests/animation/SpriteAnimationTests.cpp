#include <devex/animation/SpriteAnimation.hpp>
#include <devex/asset/SpriteData.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SpriteComponents.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using devex::asset::AssetId;
using devex::asset::SpriteAnimationData;
using devex::asset::SpriteFramesData;
using devex::scene::SpriteAnimator;

namespace {

[[nodiscard]] std::vector<AssetId> sprites(std::size_t count)
{
    std::vector<AssetId> ids;
    for (std::size_t index = 0; index < count; ++index)
    {
        ids.push_back(AssetId::generate());
    }
    return ids;
}

} // namespace

TEST_CASE("A looping animation goes round its frames at its rate", "[animation][sprite]")
{
    const SpriteAnimationData run{.name = "run", .fps = 10.0f, .loop = true, .frames = sprites(4)};
    SpriteAnimator animator{.animation = "run"};
    devex::animation::advance(animator, run, 0.05f);
    CHECK(animator.frame == 0);
    devex::animation::advance(animator, run, 0.06f);
    CHECK(animator.frame == 1);
    // A long frame skips ahead, round the loop.
    devex::animation::advance(animator, run, 0.35f);
    CHECK(animator.frame == 0);
    CHECK(animator.playing);

    // Twice as fast, then backwards.
    animator.speed = 2.0f;
    devex::animation::advance(animator, run, 0.1f);
    CHECK(animator.frame == 2);
    animator.speed = -1.0f;
    devex::animation::advance(animator, run, 0.3f);
    CHECK(animator.frame == 3);

    // Paused, nothing moves.
    animator.playing = false;
    devex::animation::advance(animator, run, 1.0f);
    CHECK(animator.frame == 3);
}

TEST_CASE("An animation that does not loop stops on its last frame", "[animation][sprite]")
{
    const SpriteAnimationData jump{.name = "jump", .fps = 10.0f, .loop = false, .frames = sprites(3)};
    SpriteAnimator animator{.animation = "jump"};
    devex::animation::advance(animator, jump, 0.15f);
    CHECK(animator.frame == 1);
    CHECK(animator.playing);
    devex::animation::advance(animator, jump, 1.0f);
    CHECK(animator.frame == 2);
    CHECK_FALSE(animator.playing);
}

TEST_CASE("Naming another animation starts it from its first frame", "[animation][sprite]")
{
    const SpriteFramesData frames{.animations = {{.name = "idle", .fps = 4.0f, .frames = sprites(2)},
                                                 {.name = "run", .fps = 12.0f, .frames = sprites(6)}}};
    SpriteAnimator animator{.frame = 1};
    // No name plays the first animation; the frame the scene posed is kept.
    REQUIRE(devex::animation::animationOf(frames, animator) == &frames.animations[0]);
    devex::animation::advance(animator, frames.animations[0], 0.1f);
    CHECK(animator.frame == 1);
    CHECK(devex::animation::spriteOf(frames, animator) == frames.animations[0].frames[1]);

    animator.animation = "run";
    const SpriteAnimationData* const run = devex::animation::animationOf(frames, animator);
    REQUIRE(run == &frames.animations[1]);
    devex::animation::advance(animator, *run, 0.1f);
    CHECK(animator.frame == 1);
    CHECK(devex::animation::spriteOf(frames, animator) == run->frames[1]);

    // A frame beyond the animation shows its last one; an unknown animation shows nothing.
    animator.frame = 40;
    CHECK(devex::animation::spriteOf(frames, animator) == run->frames.back());
    animator.animation = "fly";
    CHECK(devex::animation::animationOf(frames, animator) == nullptr);
    CHECK_FALSE(devex::animation::spriteOf(frames, animator).isValid());
}

TEST_CASE("The animators of a scene advance with the frames they name", "[animation][sprite]")
{
    devex::scene::Scene scene;
    const AssetId framesId = AssetId::generate();
    auto frames = std::make_shared<const SpriteFramesData>(
        SpriteFramesData{.animations = {{.name = "spin", .fps = 5.0f, .frames = sprites(3)}}});
    const auto source = [&](AssetId id) { return id == framesId ? frames : nullptr; };

    const devex::scene::Entity coin = scene.createEntity("Coin");
    scene.add<SpriteAnimator>(coin, SpriteAnimator{.frames = framesId});
    const devex::scene::Entity broken = scene.createEntity("Broken");
    scene.add<SpriteAnimator>(broken, SpriteAnimator{.frames = AssetId::generate()});

    devex::animation::updateSpriteAnimators(scene, source, 0.25f);
    CHECK(scene.get<SpriteAnimator>(coin).frame == 1);
    CHECK(scene.get<SpriteAnimator>(broken).frame == 0);
}

TEST_CASE("Sprite animations pass the events of the frames they come to", "[animation][sprite][events]")
{
    const SpriteAnimationData run{.name = "run",
                                  .fps = 10.0f,
                                  .frames = sprites(4),
                                  .events = {{.frame = 0, .name = "start"}, {.frame = 2, .name = "step"}}};
    SpriteAnimator animator{.animation = "run"};
    std::vector<std::string_view> passed;
    // An animation that starts passes its first frame.
    devex::animation::advance(animator, run, 0.0f, &passed);
    CHECK(passed == std::vector<std::string_view>{"start"});
    passed.clear();
    devex::animation::advance(animator, run, 0.15f, &passed);
    CHECK(passed.empty());
    devex::animation::advance(animator, run, 0.1f, &passed);
    CHECK(passed == std::vector<std::string_view>{"step"});
    passed.clear();
    // Around the loop.
    devex::animation::advance(animator, run, 0.2f, &passed);
    CHECK(passed == std::vector<std::string_view>{"start"});

    // The scene's animators give their entity with the events.
    devex::scene::Scene scene;
    const devex::scene::Entity knight = scene.createEntity("Knight");
    const AssetId framesId = AssetId::generate();
    scene.add<SpriteAnimator>(knight, SpriteAnimator{.frames = framesId, .animation = "run"});
    const auto frames = std::make_shared<const SpriteFramesData>(SpriteFramesData{.animations = {run}});
    std::vector<devex::animation::FiredAnimationEvent> fired;
    devex::animation::updateSpriteAnimators(scene, [&](AssetId id) { return id == framesId ? frames : nullptr; }, 0.25f, &fired);
    REQUIRE(fired.size() == 2);
    CHECK(fired[0].entity == knight);
    CHECK(fired[0].name == "start");
    CHECK(fired[1].name == "step");
}
