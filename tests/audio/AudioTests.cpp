#include <devex/asset/Artifact.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/audio/AudioEngine.hpp>
#include <devex/audio/AudioWorld.hpp>
#include <devex/core/File.hpp>
#include <devex/scene/AudioComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using devex::asset::AssetId;
using devex::asset::AudioClipData;
using devex::asset::AudioEncoding;
using devex::asset::AudioLoading;
using devex::audio::AudioEngine;
using devex::audio::AudioWorld;
using devex::audio::Clip;
using devex::scene::AudioSource;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

const std::filesystem::path dataDirectory{DEVEX_TEST_DATA_DIRECTORY};
constexpr std::uint32_t mixRate = 48000;

[[nodiscard]] std::vector<std::byte> readTone(const char* file)
{
    devex::core::Result<std::vector<std::byte>> bytes = devex::core::readBinaryFile(dataDirectory / "audio" / file);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
}

// The clip of a test file, as its import makes it.
[[nodiscard]] std::shared_ptr<const Clip> loadClip(const char* file, AudioLoading loading)
{
    std::vector<std::byte> bytes = readTone(file);
    const devex::core::Result<devex::audio::ClipInfo> info = devex::audio::probeClip(bytes);
    REQUIRE(info.has_value());
    devex::core::Result<std::shared_ptr<const Clip>> clip = Clip::create(AudioClipData{
        .encoding = info->encoding,
        .loading = loading,
        .channels = info->channels,
        .sampleRate = info->sampleRate,
        .frames = info->frames,
        .waveform = info->waveform,
        .encoded = std::move(bytes),
    });
    REQUIRE(clip.has_value());
    return *clip;
}

[[nodiscard]] std::unique_ptr<AudioEngine> offlineEngine()
{
    devex::core::Result<std::unique_ptr<AudioEngine>> engine =
        AudioEngine::create({.device = false, .sampleRate = mixRate, .channels = 2});
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// The loudness of each channel over the next seconds of the mix.
struct Loudness
{
    float left = 0.0f;
    float right = 0.0f;

    [[nodiscard]] float total() const noexcept
    {
        return left + right;
    }
};

[[nodiscard]] Loudness mix(AudioEngine& engine, double seconds)
{
    std::vector<float> samples(static_cast<std::size_t>(seconds * mixRate) * 2);
    engine.read(samples);
    double left = 0.0;
    double right = 0.0;
    for (std::size_t index = 0; index + 1 < samples.size(); index += 2)
    {
        left += samples[index] * samples[index];
        right += samples[index + 1] * samples[index + 1];
    }
    const double frames = static_cast<double>(samples.size() / 2);
    return {static_cast<float>(std::sqrt(left / frames)), static_cast<float>(std::sqrt(right / frames))};
}

const AssetId toneId{devex::core::Uuid::fromParts(0x70, 1)};
const AssetId streamedId{devex::core::Uuid::fromParts(0x70, 2)};

// A scene heard by a primary camera at the origin, looking down -Z.
struct Stage
{
    Scene scene;
    std::unique_ptr<AudioEngine> engine = offlineEngine();
    std::unordered_map<AssetId, std::shared_ptr<const Clip>> clips{
        {toneId, loadClip("tone.wav", AudioLoading::Decoded)},
        {streamedId, loadClip("tone.ogg", AudioLoading::Streamed)},
    };
    AudioWorld world{*engine, [this](AssetId id) {
                         const auto found = clips.find(id);
                         return found != clips.end() ? found->second : nullptr;
                     }};

    Stage()
    {
        const Entity camera = scene.createEntity("Camera");
        scene.add<devex::scene::Transform>(camera);
        scene.add<devex::scene::Camera>(camera);
        scene.updateTransforms();
    }

    Entity addSource(AudioSource source, devex::math::Vec3 position = {0.0f, 0.0f, 0.0f})
    {
        const Entity entity = scene.createEntity("Source");
        scene.add<devex::scene::Transform>(entity, devex::scene::Transform{.position = position});
        scene.add<AudioSource>(entity, source);
        scene.updateTransforms();
        return entity;
    }
};

} // namespace

TEST_CASE("Clips of every format are recognized and described", "[audio]")
{
    for (const auto& [file, encoding] : {std::pair{"tone.wav", AudioEncoding::Wav}, std::pair{"tone.ogg", AudioEncoding::Vorbis},
                                         std::pair{"tone.mp3", AudioEncoding::Mp3}, std::pair{"tone.flac", AudioEncoding::Flac}})
    {
        INFO(file);
        const devex::core::Result<devex::audio::ClipInfo> info = devex::audio::probeClip(readTone(file));
        REQUIRE(info.has_value());
        CHECK(info->encoding == encoding);
        CHECK(info->channels == 1);
        CHECK(info->sampleRate == 22050);
        // A quarter of a second, give or take the padding of MP3.
        const double seconds = static_cast<double>(info->frames) / info->sampleRate;
        CHECK(seconds > 0.24);
        CHECK(seconds < 0.33);
        // The tone is at half amplitude.
        REQUIRE(info->waveform.size() == devex::audio::waveformSize);
        const std::uint8_t peak = *std::ranges::max_element(info->waveform);
        CHECK(peak > 110);
        CHECK(peak < 145);
    }
    const std::vector<std::byte> garbage(64, std::byte{7});
    CHECK_FALSE(devex::audio::probeClip(garbage).has_value());
}

TEST_CASE("Clips import as assets that keep their file", "[audio][asset]")
{
    devex::asset::ImportContext context{
        .source = dataDirectory / "audio" / "tone.wav",
        .mainId = toneId,
        .name = "tone",
    };
    devex::core::Result<devex::asset::ImportResult> imported = devex::asset::importAudioFile(context);
    REQUIRE(imported.has_value());
    REQUIRE(imported->artifacts.size() == 1);
    CHECK(imported->artifacts[0].type == devex::asset::AssetType::AudioClip);
    devex::core::Result<AudioClipData> clip = devex::asset::decodeAudioClip(imported->artifacts[0].bytes);
    REQUIRE(clip.has_value());
    // Short clips are decoded once when they load.
    CHECK(clip->loading == AudioLoading::Decoded);
    CHECK(clip->encoded.size() == readTone("tone.wav").size());
    CHECK(clip->seconds() == Catch::Approx(0.25).margin(0.001));
    const devex::core::Result<AudioClipData> info = devex::asset::decodeAudioClipInfo(imported->artifacts[0].bytes);
    REQUIRE(info.has_value());
    CHECK(info->encoded.empty());
    CHECK(info->frames == clip->frames);

    context.options.push_back({"loading", devex::serialization::TextValue(std::string("streamed"))});
    imported = devex::asset::importAudioFile(context);
    REQUIRE(imported.has_value());
    CHECK(devex::asset::decodeAudioClipInfo(imported->artifacts[0].bytes)->loading == AudioLoading::Streamed);

    context.source = dataDirectory / "checker.png";
    CHECK_FALSE(devex::asset::importAudioFile(context).has_value());
}

TEST_CASE("Sources play their clip when the game starts, until its end", "[audio]")
{
    Stage stage;
    const Entity source = stage.addSource({.clip = toneId, .spatial = false});
    CHECK(mix(*stage.engine, 0.05).total() == 0.0f);

    stage.world.update(stage.scene, devex::core::Duration(0.0));
    CHECK(stage.world.isPlaying(source));
    CHECK(stage.world.soundCount() == 1);
    // The tone, at half amplitude: its loudness is about 0.35 on each channel.
    const Loudness playing = mix(*stage.engine, 0.1);
    CHECK(playing.left > 0.2f);
    CHECK(playing.right > 0.2f);

    // The clip lasts a quarter of a second and does not loop.
    static_cast<void>(mix(*stage.engine, 0.3));
    stage.world.update(stage.scene, devex::core::Duration(0.3));
    CHECK_FALSE(stage.world.isPlaying(source));
    CHECK(mix(*stage.engine, 0.05).total() == 0.0f);

    // Play starts it again from the beginning, and a looping clip goes on.
    stage.scene.get<AudioSource>(source).loop = true;
    stage.world.play(stage.scene, source);
    static_cast<void>(mix(*stage.engine, 0.4));
    stage.world.update(stage.scene, devex::core::Duration(0.4));
    CHECK(stage.world.isPlaying(source));
    CHECK(mix(*stage.engine, 0.05).total() > 0.2f);

    // Stop, or removing the component, silences it.
    stage.world.stop(source);
    CHECK_FALSE(stage.world.isPlaying(source));
    CHECK(mix(*stage.engine, 0.05).total() == 0.0f);
    stage.world.play(stage.scene, source);
    stage.scene.remove<AudioSource>(source);
    stage.world.update(stage.scene, devex::core::Duration(0.0));
    CHECK(stage.world.soundCount() == 0);
}

TEST_CASE("The first audio update keeps sounds started by game code", "[audio]")
{
    Stage stage;
    const Entity source = stage.addSource({.clip = toneId, .playOnStart = false, .spatial = false});

    // Start systems can play sounds before the world has updated its scene for the first time.
    stage.world.playOneShot(toneId, {0.0f, 0.0f, -1.0f});
    SECTION("A one-shot before the first update")
    {
        stage.world.update(stage.scene, devex::core::Duration(0.0));
        CHECK(stage.world.soundCount() == 1);
        CHECK(mix(*stage.engine, 0.1).total() > 0.2f);
    }
    SECTION("A source and one-shots before the first update")
    {
        stage.world.play(stage.scene, source);
        stage.world.playOneShot(toneId, {0.0f, 0.0f, -1.0f});
        REQUIRE(stage.world.soundCount() == 3);
        stage.world.update(stage.scene, devex::core::Duration(0.0));
        CHECK(stage.world.isPlaying(source));
        CHECK(stage.world.soundCount() == 3);
        CHECK(mix(*stage.engine, 0.1).total() > 0.2f);
    }
}

TEST_CASE("Changing scenes removes old sounds and keeps new explicit plays", "[audio]")
{
    Stage stage;
    stage.addSource({.clip = toneId, .loop = true, .spatial = false});
    stage.world.update(stage.scene, devex::core::Duration(0.0));
    stage.world.playOneShot(toneId, {0.0f, 0.0f, -1.0f});
    REQUIRE(stage.world.soundCount() == 2);

    Scene nextScene;
    static_cast<void>(nextScene.createEntity("Camera"));
    // The new scene reuses the old source's entity handle.
    const Entity nextSource = nextScene.createEntity("Source");
    nextScene.add<AudioSource>(nextSource, AudioSource{.clip = toneId, .playOnStart = false, .spatial = false});
    SECTION("Update changes the scene")
    {
        stage.world.update(nextScene, devex::core::Duration(0.0));
        CHECK(stage.world.soundCount() == 0);
        CHECK_FALSE(stage.world.isPlaying(nextSource));
        CHECK(mix(*stage.engine, 0.1).total() == 0.0f);
    }
    SECTION("Play changes the scene before its first update")
    {
        stage.world.play(nextScene, nextSource);
        CHECK(stage.world.soundCount() == 1);
        stage.world.playOneShot(toneId, {0.0f, 0.0f, -1.0f});
        stage.world.update(nextScene, devex::core::Duration(0.0));
        CHECK(stage.world.isPlaying(nextSource));
        CHECK(stage.world.soundCount() == 2);
        CHECK(mix(*stage.engine, 0.1).total() > 0.2f);
    }
}

TEST_CASE("Spatial sources are heard from where they are", "[audio]")
{
    Stage stage;
    AudioSource source{.clip = toneId, .loop = true, .minDistance = 1.0f, .maxDistance = 50.0f};
    const Entity right = stage.addSource(source, {4.0f, 0.0f, 0.0f});
    stage.world.update(stage.scene, devex::core::Duration(0.0));
    const Loudness fromRight = mix(*stage.engine, 0.1);
    CHECK(fromRight.right > fromRight.left * 2.0f);

    // Moved to the left of the listener.
    stage.scene.get<devex::scene::Transform>(right).position = {-4.0f, 0.0f, 0.0f};
    stage.scene.updateTransforms();
    stage.world.update(stage.scene, devex::core::Duration(1.0));
    static_cast<void>(mix(*stage.engine, 0.05));
    const Loudness fromLeft = mix(*stage.engine, 0.1);
    CHECK(fromLeft.left > fromLeft.right * 2.0f);

    // Farther away, it is quieter.
    stage.scene.get<devex::scene::Transform>(right).position = {-40.0f, 0.0f, 0.0f};
    stage.scene.updateTransforms();
    stage.world.update(stage.scene, devex::core::Duration(1.0));
    static_cast<void>(mix(*stage.engine, 0.05));
    CHECK(mix(*stage.engine, 0.1).total() < fromLeft.total() * 0.3f);
}

TEST_CASE("Groups, pauses and one-shot sounds", "[audio]")
{
    Stage stage;
    // A streamed clip, in the second group.
    const Entity music = stage.addSource({.clip = streamedId, .loop = true, .spatial = false, .group = 1});
    stage.world.update(stage.scene, devex::core::Duration(0.0));
    CHECK(mix(*stage.engine, 0.1).total() > 0.3f);

    stage.engine->setGroupVolume(1, 0.0f);
    static_cast<void>(mix(*stage.engine, 0.02));
    CHECK(mix(*stage.engine, 0.1).total() < 0.001f);
    stage.engine->setGroupVolume(1, 1.0f);
    stage.engine->setMasterVolume(0.0f);
    static_cast<void>(mix(*stage.engine, 0.02));
    CHECK(mix(*stage.engine, 0.1).total() < 0.001f);
    stage.engine->setMasterVolume(1.0f);

    // Pausing the world pauses its sounds, which resume where they were. The mixer ends the frames it
    // had already started.
    stage.world.setPaused(true);
    CHECK(mix(*stage.engine, 0.1).total() < 0.02f);
    CHECK(stage.world.isPlaying(music));
    stage.world.setPaused(false);
    CHECK(mix(*stage.engine, 0.1).total() > 0.3f);
    stage.world.pause(music);
    CHECK_FALSE(stage.world.isPlaying(music));
    CHECK(mix(*stage.engine, 0.1).total() < 0.02f);
    stage.world.resume(music);
    CHECK(stage.world.isPlaying(music));
    stage.world.stop(music);

    // A one-shot plays once, then goes.
    stage.world.playOneShot(toneId, {0.0f, 0.0f, -2.0f});
    CHECK(stage.world.soundCount() == 1);
    CHECK(mix(*stage.engine, 0.1).total() > 0.1f);
    static_cast<void>(mix(*stage.engine, 0.3));
    stage.world.update(stage.scene, devex::core::Duration(0.4));
    CHECK(stage.world.soundCount() == 0);

    // Groups are found by name, as the project names them.
    devex::asset::AudioSettings settings;
    settings.groupNames[3] = "Ambience";
    settings.groupVolumes[3] = 0.5f;
    stage.engine->configure(settings);
    CHECK(stage.engine->findGroup("Music") == 1u);
    CHECK(stage.engine->findGroup("Ambience") == 3u);
    CHECK_FALSE(stage.engine->findGroup("Master").has_value());
    CHECK(stage.engine->groupVolume(3) == 0.5f);
}

TEST_CASE("The editor previews clips outside the game", "[audio]")
{
    const std::unique_ptr<AudioEngine> engine = offlineEngine();
    engine->preview(loadClip("tone.flac", AudioLoading::Decoded));
    CHECK(engine->isPreviewing());
    CHECK(mix(*engine, 0.1).total() > 0.3f);
    CHECK(engine->previewSeconds() == Catch::Approx(0.1).margin(0.02));
    static_cast<void>(mix(*engine, 0.3));
    CHECK_FALSE(engine->isPreviewing());
    engine->preview(loadClip("tone.mp3", AudioLoading::Streamed));
    CHECK(mix(*engine, 0.1).total() > 0.2f);
    engine->stopPreview();
    CHECK_FALSE(engine->isPreviewing());
    CHECK(mix(*engine, 0.05).total() == 0.0f);
}

TEST_CASE("Projects keep their audio groups", "[audio][asset]")
{
    devex::asset::Project project;
    project.name = "Loud";
    project.audio.masterVolume = 0.8f;
    project.audio.groupNames[1] = "Soundtrack";
    project.audio.groupVolumes[1] = 0.25f;
    project.audio.groupNames[4] = "Footsteps";
    const std::string text = devex::asset::writeProjectText(project);
    const devex::core::Result<devex::asset::Project> loaded =
        devex::asset::parseProject(text, std::filesystem::path("D:/games/loud/loud.dvxproj"));
    REQUIRE(loaded.has_value());
    CHECK(loaded->audio == project.audio);
    CHECK(devex::asset::writeProjectText(devex::asset::Project{.name = "Quiet"}).find("audio") == std::string::npos);
}
