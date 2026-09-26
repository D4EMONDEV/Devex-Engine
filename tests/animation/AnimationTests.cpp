#include <devex/animation/AnimationWorld.hpp>
#include <devex/animation/Clip.hpp>
#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/ModelInstantiation.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using devex::animation::AnimationWorld;
using devex::animation::Clip;
using devex::animation::JointPose;
using devex::asset::AnimationChannel;
using devex::asset::AnimationClipData;
using devex::asset::AnimationInterpolation;
using devex::asset::AnimationPath;
using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::core::Duration;
using devex::scene::Animator;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::SkinnedMeshRenderer;
using devex::scene::Transform;

namespace {

const std::filesystem::path dataDirectory{DEVEX_TEST_DATA_DIRECTORY};

// A clip that slides one joint along X and turns another one.
[[nodiscard]] AnimationClipData slideClip(float duration = 1.0f, float distance = 2.0f)
{
    AnimationClipData clip;
    clip.name = "Slide";
    clip.duration = duration;
    clip.joints = {"Root", "Arm"};
    clip.channels.push_back({
        .joint = 0,
        .path = AnimationPath::Translation,
        .times = {0.0f, duration},
        .values = {0.0f, 0.0f, 0.0f, distance, 0.0f, 0.0f},
    });
    clip.channels.push_back({
        .joint = 1,
        .path = AnimationPath::Scale,
        .times = {0.0f, duration},
        .values = {1.0f, 1.0f, 1.0f, 3.0f, 3.0f, 3.0f},
    });
    return clip;
}

[[nodiscard]] std::shared_ptr<const Clip> clipOf(AnimationClipData data)
{
    devex::core::Result<std::shared_ptr<const Clip>> clip = Clip::create(std::move(data));
    REQUIRE(clip.has_value());
    return *clip;
}

// A skeleton of two bones under an animator, as a model would instantiate it.
struct Skeleton
{
    Scene scene;
    Entity animator;
    Entity root;
    Entity arm;
};

[[nodiscard]] Skeleton makeSkeleton()
{
    Skeleton skeleton;
    skeleton.animator = skeleton.scene.createEntity("Character");
    skeleton.scene.add<Transform>(skeleton.animator);
    skeleton.root = skeleton.scene.createEntity("Root");
    skeleton.scene.add<Transform>(skeleton.root);
    REQUIRE(skeleton.scene.setParent(skeleton.root, skeleton.animator).has_value());
    skeleton.arm = skeleton.scene.createEntity("Arm");
    skeleton.scene.add<Transform>(skeleton.arm);
    REQUIRE(skeleton.scene.setParent(skeleton.arm, skeleton.root).has_value());
    return skeleton;
}

} // namespace

TEST_CASE("Clips read their keys at any time and blend together", "[animation][clip]")
{
    const std::shared_ptr<const Clip> clip = clipOf(slideClip());
    std::vector<JointPose> pose(clip->joints().size());

    clip->sample(0.25f, pose);
    CHECK(pose[0].hasTranslation);
    CHECK(pose[0].translation.x == Catch::Approx(0.5f));
    CHECK_FALSE(pose[0].hasRotation);
    CHECK(pose[1].scale.y == Catch::Approx(1.5f));

    // Times outside the clip hold its ends.
    clip->sample(-1.0f, pose);
    CHECK(pose[0].translation.x == Catch::Approx(0.0f));
    clip->sample(5.0f, pose);
    CHECK(pose[0].translation.x == Catch::Approx(2.0f));

    SECTION("Steps hold the value of the previous key")
    {
        AnimationClipData data = slideClip();
        data.channels[0].interpolation = AnimationInterpolation::Step;
        const std::shared_ptr<const Clip> stepped = clipOf(std::move(data));
        stepped->sample(0.75f, pose);
        CHECK(pose[0].translation.x == Catch::Approx(0.0f));
    }

    SECTION("Blending mixes the poses of two clips")
    {
        std::vector<JointPose> other(pose.size());
        clip->sample(0.0f, pose);
        clip->sample(1.0f, other);
        devex::animation::blendPoses(pose, other, 0.25f);
        CHECK(pose[0].translation.x == Catch::Approx(0.5f));
        CHECK(pose[1].scale.x == Catch::Approx(1.5f));
    }
}

TEST_CASE("Animators play, loop and stop on the bones of their entity", "[animation][world]")
{
    Skeleton skeleton = makeSkeleton();
    const AssetId clipId = AssetId::generate();
    const std::shared_ptr<const Clip> clip = clipOf(slideClip());
    AnimationWorld world([&](AssetId id) { return id == clipId ? clip : nullptr; });
    skeleton.scene.add<Animator>(skeleton.animator, Animator{.clip = clipId, .loop = false});

    world.update(skeleton.scene, Duration(0.0));
    CHECK(world.animatorCount() == 1);
    CHECK(world.isPlaying(skeleton.animator));
    CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(0.0f));

    world.update(skeleton.scene, Duration(0.5));
    CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(1.0f));
    CHECK(skeleton.scene.get<Transform>(skeleton.arm).scale.x == Catch::Approx(2.0f));

    // Without looping, the clip stops at its end.
    world.update(skeleton.scene, Duration(1.0));
    CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(2.0f));
    CHECK_FALSE(world.isPlaying(skeleton.animator));

    SECTION("Looping starts over")
    {
        skeleton.scene.get<Animator>(skeleton.animator).loop = true;
        world.play(skeleton.scene, skeleton.animator);
        world.update(skeleton.scene, Duration(1.25));
        CHECK(world.isPlaying(skeleton.animator));
        CHECK(world.time(skeleton.animator) == Catch::Approx(0.25f));
        CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(0.5f));
    }

    SECTION("Pausing the world holds the pose")
    {
        world.play(skeleton.scene, skeleton.animator);
        world.update(skeleton.scene, Duration(0.25));
        world.setPaused(true);
        world.update(skeleton.scene, Duration(0.5));
        CHECK(world.time(skeleton.animator) == Catch::Approx(0.25f));
        CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(0.5f));
    }

    SECTION("An animator that is gone is forgotten")
    {
        skeleton.scene.remove<Animator>(skeleton.animator);
        world.update(skeleton.scene, Duration(0.1));
        CHECK(world.animatorCount() == 0);
    }
}

TEST_CASE("Changing the clip crossfades from the previous pose", "[animation][world]")
{
    Skeleton skeleton = makeSkeleton();
    const AssetId first = AssetId::generate();
    const AssetId second = AssetId::generate();
    const std::shared_ptr<const Clip> slow = clipOf(slideClip(1.0f, 2.0f));
    // A clip that holds the root at 10 units.
    AnimationClipData faraway = slideClip(1.0f, 0.0f);
    faraway.channels[0].values = {10.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f};
    const std::shared_ptr<const Clip> other = clipOf(std::move(faraway));
    AnimationWorld world([&](AssetId id) { return id == first ? slow : id == second ? other : nullptr; });

    skeleton.scene.add<Animator>(skeleton.animator, Animator{.clip = first, .blendTime = 1.0f});
    world.update(skeleton.scene, Duration(0.0));
    world.update(skeleton.scene, Duration(0.5));
    CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(1.0f));

    // Halfway through the fade, the root stands between both clips.
    world.play(skeleton.scene, skeleton.animator, second, 1.0f);
    world.update(skeleton.scene, Duration(0.5));
    const float halfway = skeleton.scene.get<Transform>(skeleton.root).position.x;
    CHECK(halfway > 2.0f);
    CHECK(halfway < 9.0f);
    world.update(skeleton.scene, Duration(0.6));
    CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(10.0f));
    CHECK(skeleton.scene.get<Animator>(skeleton.animator).clip == second);
}

TEST_CASE("Root motion moves the entity instead of its root bone", "[animation][world]")
{
    Skeleton skeleton = makeSkeleton();
    const AssetId clipId = AssetId::generate();
    const std::shared_ptr<const Clip> clip = clipOf(slideClip(1.0f, 2.0f));
    AnimationWorld world([&](AssetId id) { return id == clipId ? clip : nullptr; });
    skeleton.scene.add<Animator>(skeleton.animator,
                                 Animator{.clip = clipId, .loop = false, .applyRootMotion = true});

    world.update(skeleton.scene, Duration(0.0));
    world.update(skeleton.scene, Duration(0.5));
    world.update(skeleton.scene, Duration(0.5));
    // The bone stayed where it was authored while the entity walked the whole distance.
    CHECK(skeleton.scene.get<Transform>(skeleton.animator).position.x == Catch::Approx(2.0f));
    CHECK(skeleton.scene.get<Transform>(skeleton.root).position.x == Catch::Approx(0.0f));

    SECTION("A character walks with the speed of its clip")
    {
        Skeleton walker = makeSkeleton();
        walker.scene.add<devex::scene::CharacterController>(walker.animator);
        walker.scene.add<Animator>(walker.animator,
                                   Animator{.clip = clipId, .loop = false, .applyRootMotion = true});
        AnimationWorld characterWorld([&](AssetId id) { return id == clipId ? clip : nullptr; });
        characterWorld.update(walker.scene, Duration(0.0));
        characterWorld.update(walker.scene, Duration(0.5));
        // Half the clip covers one unit in half a second.
        CHECK(walker.scene.get<devex::scene::CharacterController>(walker.animator).velocity.x ==
              Catch::Approx(2.0f));
        CHECK(walker.scene.get<Transform>(walker.animator).position.x == Catch::Approx(0.0f));
    }
}

TEST_CASE("A rigged model imports its skeleton, its skinning and its animations", "[animation][gltf]")
{
    devex::asset::ImportContext context{
        .source = dataDirectory / "animated" / "robot.glb",
        .mainId = AssetId::generate(),
        .name = "robot",
    };
    const auto result = devex::asset::importGltfFile(context);
    REQUIRE(result.has_value());

    const auto model = devex::asset::decodeModel(result->artifacts.front().bytes);
    REQUIRE(model.has_value());
    REQUIRE(model->skins.size() == 1);
    CHECK(model->skins[0].joints.size() == 11);
    CHECK(model->animations.size() == 3);
    const auto skinned = std::ranges::find_if(
        model->nodes, [](const devex::asset::ModelNode& node) { return node.skin >= 0; });
    REQUIRE(skinned != model->nodes.end());
    CHECK(skinned->name == "Body");

    // The mesh carries the joints and weights of its vertices and its bind pose.
    const auto meshArtifact = std::ranges::find(result->artifacts, AssetType::Mesh,
                                                &devex::asset::ImportedArtifact::type);
    REQUIRE(meshArtifact != result->artifacts.end());
    const auto mesh = devex::asset::decodeMesh(meshArtifact->bytes);
    REQUIRE(mesh.has_value());
    CHECK(devex::asset::isSkinned(*mesh));
    CHECK(mesh->skin.size() == mesh->vertices.size());
    CHECK(mesh->inverseBind.size() == 11);
    const auto unweighted = std::ranges::find_if(mesh->skin, [](const devex::asset::VertexSkin& skin) {
        return skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w < 0.99f;
    });
    CHECK(unweighted == mesh->skin.end());

    // Clips drive the bones by name and keep their length.
    std::unordered_map<std::string, AnimationClipData> clips;
    for (const devex::asset::ImportedArtifact& artifact : result->artifacts)
    {
        if (artifact.type == AssetType::AnimationClip)
        {
            const auto clip = devex::asset::decodeAnimation(artifact.bytes);
            REQUIRE(clip.has_value());
            clips.emplace(clip->name, *clip);
        }
    }
    REQUIRE(clips.size() == 3);
    REQUIRE(clips.contains("Walk"));
    const AnimationClipData& walk = clips.at("Walk");
    CHECK(walk.duration == Catch::Approx(1.0f));
    CHECK(std::ranges::find(walk.joints, "LegLeft") != walk.joints.end());
    CHECK(clips.at("Idle").duration == Catch::Approx(2.0f));

    SECTION("Instantiating the model gives the skinned mesh its bones")
    {
        Scene scene;
        const Entity root = devex::scene::instantiateModel(scene, *model, "Robot");
        const auto renderers = scene.view<SkinnedMeshRenderer>();
        const auto first = renderers.begin();
        REQUIRE(first != renderers.end());
        [[maybe_unused]] const auto [entity, renderer] = *first;
        CHECK(renderer.bones.size() == 11);
        CHECK(scene.resolve(renderer.bones[0]).isValid());
        CHECK(scene.name(scene.resolve(renderer.bones[0])) == "Hips");

        // The clips of the file play on the instantiated skeleton.
        const AssetId clipId = AssetId::generate();
        const std::shared_ptr<const Clip> clip = clipOf(clips.at("Walk"));
        AnimationWorld world([&](AssetId id) { return id == clipId ? clip : nullptr; });
        scene.add<Animator>(root, Animator{.clip = clipId});
        world.update(scene, Duration(0.0));
        CHECK(world.isPlaying(root));

        // The walk turns the legs away from the bind pose they were instantiated with.
        Entity leg;
        for ([[maybe_unused]] auto [candidate, transform] : scene.view<Transform>())
        {
            if (scene.name(candidate) == "LegLeft")
            {
                leg = candidate;
            }
        }
        REQUIRE(leg.isValid());
        const devex::math::Quat bind = scene.get<Transform>(leg).rotation;
        world.update(scene, Duration(0.3));
        CHECK(scene.get<Transform>(leg).rotation != bind);
    }
}

TEST_CASE("A rigged FBX model plays its animations on its instantiated skeleton", "[animation][fbx]")
{
    devex::asset::ImportContext context{
        .source = dataDirectory / "fbx" / "walker.fbx",
        .mainId = AssetId::generate(),
        .name = "walker",
    };
    const auto result = devex::asset::importFbxFile(context);
    REQUIRE(result.has_value());
    const auto model = devex::asset::decodeModel(result->artifacts.front().bytes);
    REQUIRE(model.has_value());
    const auto clipArtifact = std::ranges::find(result->artifacts, AssetType::AnimationClip,
                                                &devex::asset::ImportedArtifact::type);
    REQUIRE(clipArtifact != result->artifacts.end());
    const auto walk = devex::asset::decodeAnimation(clipArtifact->bytes);
    REQUIRE(walk.has_value());

    Scene scene;
    const Entity root = devex::scene::instantiateModel(scene, *model, "Walker");
    const auto renderers = scene.view<SkinnedMeshRenderer>();
    const auto first = renderers.begin();
    REQUIRE(first != renderers.end());
    [[maybe_unused]] const auto [entity, renderer] = *first;
    REQUIRE(renderer.bones.size() == 2);
    Entity knee;
    for (const auto& bone : renderer.bones)
    {
        if (scene.name(scene.resolve(bone)) == "Knee")
        {
            knee = scene.resolve(bone);
        }
    }
    REQUIRE(knee.isValid());

    // A second of the clip bends the knee by 45 degrees.
    const AssetId clipId = AssetId::generate();
    const std::shared_ptr<const Clip> clip = clipOf(*walk);
    AnimationWorld world([&](AssetId id) { return id == clipId ? clip : nullptr; });
    scene.add<Animator>(root, Animator{.clip = clipId, .loop = false});
    const devex::math::Quat bind = scene.get<Transform>(knee).rotation;
    world.update(scene, Duration(0.0));
    world.update(scene, Duration(1.0));
    const float cosine = std::abs(devex::math::dot(bind, scene.get<Transform>(knee).rotation));
    CHECK(2.0f * std::acos(std::min(cosine, 1.0f)) == Catch::Approx(devex::math::radians(45.0f)).margin(1e-2));
}
