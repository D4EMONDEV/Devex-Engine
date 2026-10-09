#include <devex/asset/AnimationEvents.hpp>
#include <devex/asset/Artifact.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using devex::asset::AnimationEvent;
using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::asset::SpriteAnimationEvent;

namespace {

[[nodiscard]] devex::serialization::TextValue parsed(std::string_view text)
{
    const auto document = devex::serialization::parseText(std::string("[test]\nvalue = ") + std::string(text) + "\n");
    REQUIRE(document.has_value());
    const devex::serialization::TextValue* const value = document->sections.front().findProperty("value");
    REQUIRE(value != nullptr);
    return *value;
}

} // namespace

TEST_CASE("Events read and write as lists of event(time, name)", "[asset][animation][events]")
{
    const auto events = devex::asset::readAnimationEvents(parsed(R"(list(event(0.9, "right"), event(0.25, "left")))"));
    REQUIRE(events.has_value());
    // In the order of their times.
    CHECK(*events == std::vector<AnimationEvent>{{.time = 0.25f, .name = "left"}, {.time = 0.9f, .name = "right"}});
    const auto again = devex::asset::readAnimationEvents(devex::asset::writeAnimationEvents(*events));
    REQUIRE(again.has_value());
    CHECK(*again == *events);
    CHECK(devex::serialization::formatValue(devex::asset::writeAnimationEvents(*events)) == R"(list(event(0.25, "left"), event(0.9, "right")))");

    CHECK_FALSE(devex::asset::readAnimationEvents(parsed(R"(event(0.5, "step"))")));
    CHECK_FALSE(devex::asset::readAnimationEvents(parsed(R"(list(event("step", 0.5)))")));
    CHECK_FALSE(devex::asset::readAnimationEvents(parsed(R"(list(event(-1, "step")))")));
    CHECK_FALSE(devex::asset::readAnimationEvents(parsed(R"(list(event(0.5, "")))")));

    const auto frames = devex::asset::readSpriteAnimationEvents(parsed(R"(list(event(4, "step"), event(1, "step")))"));
    REQUIRE(frames.has_value());
    CHECK(*frames == std::vector<SpriteAnimationEvent>{{.frame = 1, .name = "step"}, {.frame = 4, .name = "step"}});
    // Frames are whole.
    CHECK_FALSE(devex::asset::readSpriteAnimationEvents(parsed(R"(list(event(1.5, "step")))")));
}

TEST_CASE("Clips and sprite animations keep their events in their files and artifacts", "[asset][animation][events]")
{
    // The import settings of a model give events to its clips, by the keys of their sub-assets.
    devex::asset::MetaFile meta{.id = AssetId::generate(), .importer = "gltf"};
    meta.subAssets.push_back({.type = AssetType::AnimationClip, .key = "Walk", .id = AssetId::generate()});
    const std::uint64_t without = devex::asset::importSettingsHash(meta);
    meta.subAssets.back().options.push_back({"events", devex::asset::writeAnimationEvents(std::vector<AnimationEvent>{{0.4f, "step"}})});
    CHECK(devex::asset::importSettingsHash(meta) != without);
    const std::string text = devex::asset::writeMetaFile(meta);
    INFO(text);
    CHECK(text.find(R"(events = list(event(0.4, "step")))") != std::string::npos);
    const auto read = devex::asset::parseMetaFile(text);
    REQUIRE(read.has_value());
    REQUIRE(read->subAssets.size() == 1);
    CHECK(read->subAssets.front() == meta.subAssets.front());

    devex::asset::ImportContext context;
    context.subAssets = devex::asset::SubAssetIds({{.type = AssetType::AnimationClip,
                                                    .key = "Walk",
                                                    .id = meta.subAssets.front().id,
                                                    .options = {{"events", parsed(R"(list(event(0.4, "step"), event(2, "late")))")}}}});
    // Events after the end of the clip are left out.
    CHECK(context.animationEvents("Walk", 1.0f) == std::vector<AnimationEvent>{{0.4f, "step"}});
    CHECK(context.animationEvents("Run", 1.0f).empty());

    devex::asset::AnimationClipData clip;
    clip.name = "Walk";
    clip.duration = 1.0f;
    clip.joints = {"Root"};
    clip.channels.push_back({.joint = 0, .times = {0.0f, 1.0f}, .values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}});
    clip.events = {{0.4f, "step"}, {0.9f, "step"}};
    const auto decoded = devex::asset::decodeAnimation(devex::asset::encodeAnimation(clip));
    REQUIRE(decoded.has_value());
    CHECK(decoded->events == clip.events);
    clip.events = {{0.9f, "late"}, {0.4f, "early"}};
    CHECK_FALSE(devex::asset::validate(clip));
    clip.events = {{1.5f, "after"}};
    CHECK_FALSE(devex::asset::validate(clip));

    // Sprite animations keep them in their .dvxframes, on frames.
    devex::asset::SpriteFramesData frames;
    frames.animations.push_back({.name = "run",
                                 .fps = 12.0f,
                                 .frames = {AssetId::generate(), AssetId::generate(), AssetId::generate()},
                                 .events = {{.frame = 1, .name = "step"}}});
    const std::string framesText = devex::asset::writeSpriteFramesFile(frames);
    INFO(framesText);
    CHECK(framesText.find(R"(events = list(event(1, "step")))") != std::string::npos);
    const auto framesRead = devex::asset::parseSpriteFramesFile(framesText);
    REQUIRE(framesRead.has_value());
    CHECK(*framesRead == frames);
    const auto framesDecoded = devex::asset::decodeSpriteFrames(devex::asset::encodeSpriteFrames(frames));
    REQUIRE(framesDecoded.has_value());
    CHECK(*framesDecoded == frames);
    frames.animations.front().events = {{.frame = 3, .name = "beyond"}};
    CHECK_FALSE(devex::asset::validate(frames));
}

TEST_CASE("The events of the clips of a model are set in its import settings", "[asset][database][animation][events]")
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-events-" + devex::core::Uuid::generate().toString());
    {
        const devex::asset::Project project = *devex::asset::createProject(root, "Events");
        std::filesystem::copy_file(std::filesystem::path{DEVEX_SANDBOX_DIRECTORY} / "assets" / "models" / "robot.glb",
                                   project.assetsDirectory() / "robot.glb");
        devex::core::JobSystem jobs(2);
        auto database = devex::asset::AssetDatabase::open(project, jobs, {.watchFiles = false});
        REQUIRE(database.has_value());
        (*database)->waitForImports();
        static_cast<void>((*database)->update());

        std::optional<AssetId> walk;
        for (const devex::asset::AssetInfo& info : (*database)->assets(AssetType::AnimationClip))
        {
            walk = info.name == "Walk" ? std::optional(info.id) : walk;
        }
        REQUIRE(walk.has_value());
        CHECK_FALSE((*database)->subAssetOption(*walk, "events"));
        const std::vector<AnimationEvent> steps{{0.2f, "step"}, {0.6f, "step"}};
        REQUIRE((*database)->setSubAssetOption(*walk, "events", devex::asset::writeAnimationEvents(steps)));
        (*database)->waitForImports();
        static_cast<void>((*database)->update());
        CHECK((*database)->subAssetOption(*walk, "events").has_value());
        const auto bytes = (*database)->loadArtifact(*walk);
        REQUIRE(bytes.has_value());
        const auto clip = devex::asset::decodeAnimation(*bytes);
        REQUIRE(clip.has_value());
        CHECK(clip->events == steps);

        // The .dvxmeta keeps them, and removing them imports the clip without them.
        const auto metaText = devex::core::readTextFile(project.assetsDirectory() / "robot.glb.dvxmeta");
        REQUIRE(metaText.has_value());
        CHECK(metaText->find("event(0.2, \"step\")") != std::string::npos);
        REQUIRE((*database)->setSubAssetOption(*walk, "events", std::nullopt));
        (*database)->waitForImports();
        static_cast<void>((*database)->update());
        const auto without = devex::asset::decodeAnimation(*(*database)->loadArtifact(*walk));
        REQUIRE(without.has_value());
        CHECK(without->events.empty());
    }
    std::filesystem::remove_all(root);
}
