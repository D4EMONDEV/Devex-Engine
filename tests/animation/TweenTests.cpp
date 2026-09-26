#include <devex/animation/TweenWorld.hpp>
#include <devex/asset/CurveData.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

using Catch::Approx;
using devex::animation::SequenceStep;
using devex::animation::TweenHandle;
using devex::animation::TweenSpec;
using devex::animation::TweenWorld;
using devex::core::Duration;
using devex::math::Ease;
using devex::math::Vec3;
using devex::math::Vec4;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::Transform;
using devex::scene::TweenLoop;
using devex::scene::Tweener;

namespace {

[[nodiscard]] Entity box(Scene& scene, Vec3 position = Vec3{0.0f})
{
    const Entity entity = scene.createEntity("Box");
    scene.add<Transform>(entity).position = position;
    return entity;
}

TweenHandle play(TweenWorld& tweens, Scene& scene, const TweenSpec& spec)
{
    devex::core::Result<TweenHandle> handle = tweens.play(scene, spec);
    REQUIRE(handle.has_value());
    return *handle;
}

void step(TweenWorld& tweens, Scene& scene, double seconds)
{
    tweens.update(scene, Duration(seconds));
}

} // namespace

TEST_CASE("A tween moves a field to its end over its duration", "[animation][tween]")
{
    Scene scene;
    const Entity entity = box(scene, Vec3{1.0f, 0.0f, 0.0f});
    TweenWorld tweens;
    const TweenHandle handle = play(tweens, scene, {.entity = entity,
                                                    .field = "Transform.position",
                                                    .to = Vec4{3.0f, 2.0f, 0.0f, 0.0f},
                                                    .duration = 2.0f,
                                                    .ease = Ease::Linear});
    CHECK(tweens.isPlaying(handle));
    step(tweens, scene, 1.0);
    CHECK(scene.get<Transform>(entity).position.x == Approx(2.0f));
    CHECK(scene.get<Transform>(entity).position.y == Approx(1.0f));
    step(tweens, scene, 1.5);
    CHECK(scene.get<Transform>(entity).position.x == Approx(3.0f));
    CHECK_FALSE(tweens.isPlaying(handle));
    CHECK(tweens.activeCount() == 0);

    // A relative tween adds to where the field is once its delay is over.
    const TweenHandle up = play(tweens, scene, {.entity = entity,
                                                .field = "Transform.position",
                                                .to = Vec4{0.0f, 1.0f, 0.0f, 0.0f},
                                                .relative = true,
                                                .duration = 1.0f,
                                                .delay = 0.5f,
                                                .ease = Ease::Linear});
    scene.get<Transform>(entity).position.y = 5.0f;
    step(tweens, scene, 0.25);
    CHECK(tweens.isPlaying(up));
    CHECK(scene.get<Transform>(entity).position.y == Approx(5.0f));
    step(tweens, scene, 0.75);
    CHECK(scene.get<Transform>(entity).position.y == Approx(5.5f));
    step(tweens, scene, 1.0);
    CHECK(scene.get<Transform>(entity).position.y == Approx(6.0f));
}

TEST_CASE("Tweens animate numbers, colors and rotations", "[animation][tween]")
{
    Scene scene;
    const Entity entity = box(scene);
    scene.add<devex::scene::UiRect>(entity).opacity = 1.0f;
    scene.add<devex::scene::UiImage>(entity);
    TweenWorld tweens;
    play(tweens, scene, {.entity = entity, .field = "UiRect.opacity", .to = Vec4{0.0f}, .duration = 1.0f, .ease = Ease::Linear});
    play(tweens, scene, {.entity = entity,
                         .field = "UiImage.color",
                         .to = Vec4{1.0f, 0.0f, 0.0f, 0.5f},
                         .from = Vec4{0.0f, 0.0f, 1.0f, 1.0f},
                         .duration = 1.0f,
                         .ease = Ease::Linear});
    play(tweens, scene, {.entity = entity,
                         .field = "Transform.rotation",
                         .to = Vec4{0.0f, 90.0f, 0.0f, 0.0f},
                         .duration = 1.0f,
                         .ease = Ease::Linear});
    step(tweens, scene, 0.5);
    CHECK(scene.get<devex::scene::UiRect>(entity).opacity == Approx(0.5f));
    const Vec4 color = scene.get<devex::scene::UiImage>(entity).color;
    CHECK(color.x == Approx(0.5f));
    CHECK(color.z == Approx(0.5f));
    CHECK(color.w == Approx(0.75f));
    const Vec3 halfway = devex::math::degrees(devex::math::eulerAngles(scene.get<Transform>(entity).rotation));
    CHECK(halfway.y == Approx(45.0f).margin(0.01));
    step(tweens, scene, 0.5);
    const Vec3 turned = devex::math::degrees(devex::math::eulerAngles(scene.get<Transform>(entity).rotation));
    CHECK(turned.y == Approx(90.0f).margin(0.01));
}

TEST_CASE("A tween that names nothing it can animate does not play", "[animation][tween]")
{
    Scene scene;
    const Entity entity = box(scene);
    TweenWorld tweens;
    CHECK_FALSE(tweens.play(scene, {.entity = entity, .field = "Nothing.value"}).has_value());
    CHECK_FALSE(tweens.play(scene, {.entity = entity, .field = "Transform.missing"}).has_value());
    // The entity has no UiRect.
    CHECK_FALSE(tweens.play(scene, {.entity = entity, .field = "UiRect.opacity"}).has_value());
    // Text is not a number.
    scene.add<devex::scene::UiRect>(entity);
    scene.add<devex::scene::UiText>(entity);
    CHECK_FALSE(tweens.play(scene, {.entity = entity, .field = "UiText.text"}).has_value());
    CHECK_FALSE(tweens.play(scene, {.entity = Entity{}, .field = "Transform.position"}).has_value());
}

TEST_CASE("Tweens loop, go back and forth, and end with their entity", "[animation][tween]")
{
    Scene scene;
    const Entity entity = box(scene);
    TweenWorld tweens;
    const TweenHandle pingPong = play(tweens, scene, {.entity = entity,
                                                      .field = "Transform.position",
                                                      .to = Vec4{1.0f, 0.0f, 0.0f, 0.0f},
                                                      .duration = 1.0f,
                                                      .ease = Ease::Linear,
                                                      .loop = TweenLoop::PingPong,
                                                      .repeats = 1});
    step(tweens, scene, 1.25);
    CHECK(scene.get<Transform>(entity).position.x == Approx(0.75f));
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(entity).position.x == Approx(0.25f));
    step(tweens, scene, 1.0);
    // Back where it started, once there and once back.
    CHECK(scene.get<Transform>(entity).position.x == Approx(0.0f));
    CHECK_FALSE(tweens.isPlaying(pingPong));

    const TweenHandle forever = play(tweens, scene, {.entity = entity,
                                                     .field = "Transform.position",
                                                     .to = Vec4{2.0f, 0.0f, 0.0f, 0.0f},
                                                     .from = Vec4{0.0f},
                                                     .duration = 1.0f,
                                                     .ease = Ease::Linear,
                                                     .loop = TweenLoop::Restart,
                                                     .repeats = -1});
    for (int frame = 0; frame < 10; ++frame)
    {
        step(tweens, scene, 0.35);
    }
    CHECK(tweens.isPlaying(forever));
    // 3.5 seconds in: halfway through its fourth time.
    CHECK(scene.get<Transform>(entity).position.x == Approx(1.0f));
    scene.destroyEntity(entity);
    step(tweens, scene, 0.1);
    CHECK_FALSE(tweens.isPlaying(forever));
}

TEST_CASE("Tweens pause, resume and end on demand", "[animation][tween]")
{
    Scene scene;
    const Entity entity = box(scene);
    TweenWorld tweens;
    const TweenSpec spec{.entity = entity,
                         .field = "Transform.position",
                         .to = Vec4{4.0f, 0.0f, 0.0f, 0.0f},
                         .duration = 4.0f,
                         .ease = Ease::Linear};
    const TweenHandle handle = play(tweens, scene, spec);
    step(tweens, scene, 1.0);
    tweens.pause(handle);
    step(tweens, scene, 1.0);
    CHECK(scene.get<Transform>(entity).position.x == Approx(1.0f));
    CHECK(tweens.isPlaying(handle));
    tweens.resume(handle);
    step(tweens, scene, 1.0);
    CHECK(scene.get<Transform>(entity).position.x == Approx(2.0f));
    // Every tween holds while the editor pauses the game.
    tweens.setPaused(true);
    step(tweens, scene, 1.0);
    CHECK(scene.get<Transform>(entity).position.x == Approx(2.0f));
    tweens.setPaused(false);

    // Complete jumps to the end at the next update.
    tweens.kill(handle, true);
    step(tweens, scene, 0.0);
    CHECK(scene.get<Transform>(entity).position.x == Approx(4.0f));
    CHECK_FALSE(tweens.isPlaying(handle));

    // Back from 4 to 0, stopped where it is after a second.
    const TweenHandle killed = play(tweens, scene, TweenSpec{.entity = entity,
                                                             .field = "Transform.position",
                                                             .to = Vec4{0.0f},
                                                             .duration = 4.0f,
                                                             .ease = Ease::Linear});
    step(tweens, scene, 1.0);
    tweens.kill(killed);
    step(tweens, scene, 1.0);
    CHECK(scene.get<Transform>(entity).position.x == Approx(3.0f));
    CHECK_FALSE(tweens.isPlaying(killed));
}

TEST_CASE("A sequence plays its steps one after the other", "[animation][tween]")
{
    Scene scene;
    const Entity first = box(scene);
    const Entity second = box(scene);
    TweenWorld tweens;
    std::vector<SequenceStep> steps(3);
    steps[0].tweens.push_back({.entity = first,
                               .field = "Transform.position",
                               .to = Vec4{1.0f, 0.0f, 0.0f, 0.0f},
                               .duration = 1.0f,
                               .ease = Ease::Linear});
    steps[0].tweens.push_back({.entity = second,
                               .field = "Transform.scale",
                               .to = Vec4{2.0f, 2.0f, 2.0f, 0.0f},
                               .duration = 0.5f,
                               .ease = Ease::Linear});
    steps[0].interval = 1.0f;
    steps[1].interval = 0.5f;
    steps[2].tweens.push_back({.entity = second,
                               .field = "Transform.position",
                               .to = Vec4{0.0f, 3.0f, 0.0f, 0.0f},
                               .duration = 1.0f,
                               .ease = Ease::Linear});
    devex::core::Result<TweenHandle> sequence = tweens.playSequence(scene, std::move(steps));
    REQUIRE(sequence.has_value());
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(first).position.x == Approx(0.5f));
    CHECK(scene.get<Transform>(second).scale.x == Approx(2.0f));
    CHECK(scene.get<Transform>(second).position.y == Approx(0.0f));
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(first).position.x == Approx(1.0f));
    // The interval of the first step, then the empty second step and its own.
    for (int frame = 0; frame < 6; ++frame)
    {
        step(tweens, scene, 0.25);
    }
    CHECK(scene.get<Transform>(second).position.y == Approx(0.0f));
    CHECK(tweens.isPlaying(*sequence));
    step(tweens, scene, 0.5);
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(second).position.y > 0.0f);
    for (int frame = 0; frame < 8; ++frame)
    {
        step(tweens, scene, 0.25);
    }
    CHECK(scene.get<Transform>(second).position.y == Approx(3.0f));
    CHECK_FALSE(tweens.isPlaying(*sequence));

    // A step whose entity lacks the component makes the whole sequence fail at once.
    std::vector<SequenceStep> broken(1);
    broken[0].tweens.push_back({.entity = first, .field = "UiRect.opacity"});
    CHECK_FALSE(tweens.playSequence(scene, std::move(broken)).has_value());
}

TEST_CASE("A Tweener plays by itself and stops with it", "[animation][tween]")
{
    Scene scene;
    const Entity entity = box(scene, Vec3{0.0f, 1.0f, 0.0f});
    scene.add<Tweener>(entity, Tweener{.field = "Transform.position",
                                       .to = Vec4{0.0f, 1.0f, 0.0f, 0.0f},
                                       .fromCurrent = true,
                                       .relative = true,
                                       .duration = 1.0f,
                                       .ease = Ease::Linear,
                                       .loop = TweenLoop::PingPong,
                                       .repeats = -1});
    const Entity waiting = box(scene);
    scene.add<Tweener>(waiting, Tweener{.field = "Transform.scale",
                                        .to = Vec4{3.0f, 3.0f, 3.0f, 0.0f},
                                        .fromCurrent = true,
                                        .relative = false,
                                        .duration = 1.0f,
                                        .ease = Ease::Linear,
                                        .loop = TweenLoop::None,
                                        .playOnStart = false});
    TweenWorld tweens;
    step(tweens, scene, 0.0);
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(entity).position.y == Approx(1.5f));
    step(tweens, scene, 1.0);
    CHECK(scene.get<Transform>(entity).position.y == Approx(1.5f));
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(entity).position.y == Approx(1.0f));
    CHECK(scene.get<Transform>(waiting).scale.x == Approx(1.0f));

    devex::core::Result<TweenHandle> started = tweens.playTweener(scene, waiting);
    REQUIRE(started.has_value());
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(waiting).scale.x == Approx(2.0f));
    CHECK(scene.get<Transform>(entity).position.y == Approx(1.5f));

    // Removing the Tweener ends its tween where it is; adding it again starts over.
    scene.remove<Tweener>(entity);
    step(tweens, scene, 0.25);
    CHECK(scene.get<Transform>(entity).position.y == Approx(1.5f));
    CHECK(tweens.activeCount() == 1);
    scene.add<Tweener>(entity, Tweener{.field = "Transform.position", .to = Vec4{0.0f, 2.0f, 0.0f, 0.0f}, .duration = 1.0f});
    step(tweens, scene, 0.0);
    CHECK(tweens.activeCount() == 2);

    // A Tweener that cannot play says so once.
    const Entity broken = box(scene);
    scene.add<Tweener>(broken, Tweener{.field = "UiRect.opacity"});
    step(tweens, scene, 0.1);
    step(tweens, scene, 0.1);
    CHECK(tweens.activeCount() == 2);
    CHECK_FALSE(tweens.playTweener(scene, box(scene)).has_value());
}

TEST_CASE("A curve asset eases a tween in place of its ease", "[animation][tween]")
{
    Scene scene;
    const Entity entity = box(scene);
    const devex::asset::AssetId curveId{devex::core::Uuid::generate()};
    // Straight to 2 at the middle, then back to 1.
    auto curve = std::make_shared<devex::asset::CurveData>(devex::asset::CurveData{
        .keys = {{.time = 0.0f, .value = 0.0f, .inTangent = 4.0f, .outTangent = 4.0f},
                 {.time = 0.5f, .value = 2.0f, .inTangent = 0.0f, .outTangent = 0.0f},
                 {.time = 1.0f, .value = 1.0f, .inTangent = -2.0f, .outTangent = -2.0f}}});
    int lookups = 0;
    TweenWorld tweens([&](devex::asset::AssetId id) -> std::shared_ptr<const devex::asset::CurveData> {
        ++lookups;
        return id == curveId ? curve : nullptr;
    });
    play(tweens, scene, {.entity = entity,
                         .field = "Transform.position",
                         .to = Vec4{10.0f, 0.0f, 0.0f, 0.0f},
                         .duration = 1.0f,
                         .ease = Ease::Linear,
                         .curve = curveId});
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(entity).position.x == Approx(20.0f));
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(entity).position.x == Approx(10.0f));
    CHECK(lookups >= 1);

    // A curve that cannot be loaded leaves the ease.
    play(tweens, scene, {.entity = entity,
                         .field = "Transform.position",
                         .to = Vec4{0.0f},
                         .duration = 1.0f,
                         .ease = Ease::Linear,
                         .curve = devex::asset::AssetId{devex::core::Uuid::generate()}});
    step(tweens, scene, 0.5);
    CHECK(scene.get<Transform>(entity).position.x == Approx(5.0f));
}
