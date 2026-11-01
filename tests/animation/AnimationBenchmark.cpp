// Hidden CPU measurement: DEVEX_ANIMATION_LOD=0/1 selects full or distant pose sampling.
#include <devex/animation/AnimationWorld.hpp>
#include <devex/platform/Process.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

TEST_CASE("Animation LOD stress benchmark", "[.][benchmark][animation]")
{
    using namespace devex;
    constexpr int characters = 256, joints = 64, frames = 300;
    const bool lod = platform::environmentVariable("DEVEX_ANIMATION_LOD") != "0";
    asset::AnimationClipData data;
    data.name = "Crowd";
    data.duration = 10;
    for (std::uint32_t joint = 0; joint < joints; ++joint)
    {
        data.joints.push_back("Bone" + std::to_string(joint));
        data.channels.push_back({.joint = joint,
                                 .path = asset::AnimationPath::Translation,
                                 .times = {0, 5, 10},
                                 .values = {0, 0, 0, 0.1f, 0.2f, 0.3f, 0, 0, 0}});
        data.channels.push_back({.joint = joint,
                                 .path = asset::AnimationPath::Rotation,
                                 .times = {0, 10},
                                 .values = {0, 0, 0, 1, 0, std::sin(0.2f), 0, std::cos(0.2f)}});
    }
    const auto clip = animation::Clip::create(data);
    REQUIRE(clip);
    const auto id = asset::AssetId::generate();
    scene::Scene scene;
    for (int index = 0; index < characters; ++index)
    {
        const auto root = scene.createEntity("Character");
        scene.add<scene::Transform>(
            root, scene::Transform{.position = {float(index % 16) * 3, 0, float(index / 16) * 3}});
        scene.add<scene::Animator>(root,
                                   scene::Animator{.clip = id, .lodDistance = lod ? 20.0f : 0.0f});
        for (int joint = 0; joint < joints; ++joint)
        {
            const auto bone = scene.createEntity("Bone" + std::to_string(joint));
            scene.add<scene::Transform>(bone);
            REQUIRE(scene.setParent(bone, root));
        }
    }
    scene.updateTransforms();
    animation::AnimationWorld world([&](asset::AssetId) { return *clip; });
    const std::array viewers{math::Vec3{0, 0, 1000}};
    world.setViewPositions(viewers);
    for (int frame = 0; frame < 60; ++frame)
        world.update(scene, core::Duration(1.0 / 60));
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t sampled = 0, held = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        world.update(scene, core::Duration(1.0 / 60));
        sampled += world.stats().sampledPoses;
        held += world.stats().heldPoses;
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("Animation: %d characters x %d joints, %d frames, LOD %s: %.3f ms/update\n",
                characters, joints, frames, lod ? "on" : "off", elapsed / frames);
    std::printf("Pose evaluations: %llu sampled, %llu held\n",
                static_cast<unsigned long long>(sampled), static_cast<unsigned long long>(held));
    CHECK(sampled + held == std::uint64_t(characters) * frames);
}
