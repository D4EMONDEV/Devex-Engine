#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/runtime/PlayerSettings.hpp>
#include <devex/runtime/SaveGames.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using devex::asset::AssetId;
using devex::math::Vec3;
using devex::runtime::PlayerSettings;
using devex::runtime::SaveGames;
using devex::scene::Entity;
using devex::scene::Scene;

namespace test {

// What a game keeps between its sessions.
struct Progress
{
    int level = 1;
    float health = 100.0f;
    std::string name = "Hero";
    bool tutorialDone = false;
    Vec3 checkpoint{0.0f};
    std::vector<std::string> items;
};
DEVEX_DECLARE_REFLECTION(Progress);

DEVEX_REFLECT(Progress)
{
    type.field("level", &Progress::level);
    type.field("health", &Progress::health);
    type.field("name", &Progress::name);
    type.field("tutorial_done", &Progress::tutorialDone);
    type.field("checkpoint", &Progress::checkpoint);
    type.field("items", &Progress::items);
}

} // namespace test

namespace {

// A folder of its own for each test, removed afterwards.
class TemporaryFolder
{
public:
    TemporaryFolder()
        : m_path(std::filesystem::temp_directory_path() / ("devex-saves-" + devex::core::Uuid::generate().toString()))
    {
    }

    ~TemporaryFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

} // namespace

TEST_CASE("Saves keep the fields of an object in a slot, with what a list of saves shows", "[runtime][saves]")
{
    const TemporaryFolder folder;
    SaveGames saves(folder.path() / "saves");
    saves.addPlayTime(90.5);

    const test::Progress progress{.level = 3,
                                  .health = 42.5f,
                                  .name = "Léa",
                                  .tutorialDone = true,
                                  .checkpoint = {1.0f, 2.0f, 3.0f},
                                  .items = {"key", "map"}};
    REQUIRE(saves.save("1", progress, {.label = "Chapter 2", .scene = false, .thumbnail = false, .version = 4}));
    CHECK(saves.exists("1"));
    CHECK(std::filesystem::exists(folder.path() / "saves" / "1.dvxsave"));
    CHECK_FALSE(saves.exists("2"));

    const devex::core::Result<test::Progress> loaded = saves.load<test::Progress>("1");
    REQUIRE(loaded.has_value());
    CHECK(loaded->level == 3);
    CHECK(loaded->health == 42.5f);
    CHECK(loaded->name == "Léa");
    CHECK(loaded->tutorialDone);
    CHECK(loaded->checkpoint == Vec3{1.0f, 2.0f, 3.0f});
    CHECK(loaded->items == std::vector<std::string>{"key", "map"});

    const std::optional<devex::runtime::SaveSlot> slot = saves.find("1");
    REQUIRE(slot.has_value());
    CHECK(slot->name == "1");
    CHECK(slot->label == "Chapter 2");
    CHECK(slot->version == 4);
    CHECK(slot->type == "Progress");
    CHECK(slot->playTime == 90.5);
    CHECK_FALSE(slot->hasScene);
    CHECK_FALSE(slot->thumbnail.has_value());
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    CHECK(slot->time <= now);
    CHECK(slot->time > now - 60);

    REQUIRE(saves.save("auto", test::Progress{}, {.scene = false, .thumbnail = false}));
    CHECK(saves.slots().size() == 2);

    // A missing slot is not an error of the file; a name that is no slot is refused.
    const devex::core::Result<test::Progress> missing = saves.load<test::Progress>("9");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == devex::core::ErrorCode::NotFound);
    CHECK_FALSE(saves.save("../escape", progress).has_value());
    CHECK_FALSE(SaveGames::isValidSlot("a/b"));
    CHECK(SaveGames::isValidSlot("Quick save_2.b"));

    REQUIRE(saves.remove("1"));
    CHECK_FALSE(saves.exists("1"));
    CHECK(saves.slots().size() == 1);

    // Without a folder, saves fail rather than go anywhere.
    SaveGames nowhere({});
    CHECK_FALSE(nowhere.save("1", progress).has_value());
    CHECK(nowhere.slots().empty());
}

TEST_CASE("Saves bring back the scene they kept, and ask for a picture", "[runtime][saves]")
{
    const TemporaryFolder folder;
    SaveGames saves(folder.path());
    Scene scene;
    const Entity crate = scene.createEntity("Crate");
    scene.add<devex::scene::Transform>(crate, devex::scene::Transform{.position = {4.0f, 0.0f, 0.0f}});
    const AssetId arena = AssetId::generate();
    saves.setScene(&scene, arena, "arena");
    saves.addPlayTime(12.0);

    REQUIRE(saves.save("quick", test::Progress{.level = 2}));
    std::vector<std::filesystem::path> pictures = saves.takeThumbnailRequests();
    REQUIRE(pictures.size() == 1);
    CHECK(pictures.front() == folder.path() / "quick.png");
    CHECK(saves.takeThumbnailRequests().empty());

    // The game goes on, then comes back to the save.
    scene.get<devex::scene::Transform>(crate).position = {9.0f, 0.0f, 0.0f};
    saves.addPlayTime(30.0);
    CHECK_FALSE(saves.takeRestore().has_value());
    const devex::core::Result<devex::runtime::SaveSlot> slot = [&] {
        test::Progress progress;
        return saves.load("quick", devex::reflection::typeInfo<test::Progress>(), &progress);
    }();
    REQUIRE(slot.has_value());
    CHECK(slot->hasScene);
    CHECK(slot->scene == arena);
    CHECK(slot->sceneName == "arena");
    CHECK(saves.playTime() == 12.0);

    std::optional<SaveGames::Restore> restore = saves.takeRestore();
    REQUIRE(restore.has_value());
    CHECK(restore->slot == "quick");
    CHECK(restore->scene == arena);
    const devex::core::Result<Scene> restored = devex::scene::loadScene(restore->sceneText);
    REQUIRE(restored.has_value());
    const Entity back = restored->findEntity(scene.uuid(crate));
    REQUIRE(back.isValid());
    CHECK(restored->get<devex::scene::Transform>(back).position == Vec3{4.0f, 0.0f, 0.0f});

    // Loading the data alone leaves the scene that plays.
    REQUIRE(saves.load<test::Progress>("quick", false).has_value());
    CHECK_FALSE(saves.takeRestore().has_value());

    // Without a picture, none is asked for and an older one goes.
    REQUIRE(devex::core::writeTextFile(folder.path() / "quick.png", "not a picture"));
    REQUIRE(saves.save("quick", test::Progress{}, {.thumbnail = false}));
    CHECK(saves.takeThumbnailRequests().empty());
    CHECK_FALSE(std::filesystem::exists(folder.path() / "quick.png"));
}

TEST_CASE("Older saves load, and a damaged save falls back on the one before", "[runtime][saves]")
{
    const TemporaryFolder folder;
    SaveGames saves(folder.path());

    // Written by an older game: a field that went away, another that did not exist yet.
    REQUIRE(devex::core::writeTextFile(folder.path() / "old.dvxsave",
                                       "[save format=1 type=\"Progress\" version=1 time=10 play_time=5]\n\n"
                                       "[data]\nlevel = 7\nmana = 30\n"));
    const devex::core::Result<test::Progress> old = saves.load<test::Progress>("old");
    REQUIRE(old.has_value());
    CHECK(old->level == 7);
    CHECK(old->name == "Hero");

    // A save written by a newer game is refused.
    REQUIRE(devex::core::writeTextFile(folder.path() / "new.dvxsave", "[save format=9]\n"));
    const devex::core::Result<test::Progress> newer = saves.load<test::Progress>("new");
    REQUIRE_FALSE(newer.has_value());
    CHECK(newer.error().code == devex::core::ErrorCode::Unsupported);

    REQUIRE(saves.save("1", test::Progress{.level = 1}, {.scene = false, .thumbnail = false}));
    REQUIRE(saves.save("1", test::Progress{.level = 2}, {.scene = false, .thumbnail = false}));
    CHECK(std::filesystem::exists(folder.path() / "1.dvxsave.bak"));
    REQUIRE(devex::core::writeTextFile(folder.path() / "1.dvxsave", "[broken"));
    const devex::core::Result<test::Progress> rescued = saves.load<test::Progress>("1");
    REQUIRE(rescued.has_value());
    CHECK(rescued->level == 1);
}

TEST_CASE("The settings of the player keep volumes, the window and the values of the game", "[runtime][settings]")
{
    PlayerSettings settings;
    CHECK_FALSE(settings.fullscreen().has_value());
    CHECK(settings.volume(PlayerSettings::master) == 1.0f);
    CHECK_FALSE(settings.takeChanges());

    settings.setFullscreen(true);
    settings.setVsync(false);
    settings.setVolume("Master", 0.5f);
    settings.setVolume("Music", 2.0f);
    settings.setValue("language", devex::serialization::TextValue(std::string("fr")));
    settings.setValue("sensitivity", devex::serialization::TextValue(0.25));
    settings.setValue("difficulty", devex::serialization::TextValue(std::int64_t{2}));
    settings.setValue("subtitles", devex::serialization::TextValue(true));
    CHECK(settings.takeChanges());
    CHECK_FALSE(settings.takeChanges());
    // Setting what is already there changes nothing.
    settings.setVolume("Master", 0.5f);
    CHECK_FALSE(settings.takeChanges());
    CHECK(settings.volume("Music") == 1.0f);

    PlayerSettings read;
    read.read(settings.write());
    CHECK(read.fullscreen() == true);
    CHECK(read.vsync() == false);
    CHECK(read.volume("Master") == 0.5f);
    CHECK(read.stringValue("language", "en") == "fr");
    CHECK(read.numberValue("sensitivity", 1.0) == 0.25);
    CHECK(read.integerValue("difficulty", 0) == 2);
    CHECK(read.boolValue("subtitles", false));
    CHECK(read.stringValue("missing", "none") == "none");
    CHECK(read.integerValue("sensitivity", 5) == 0);
    CHECK_FALSE(read.takeChanges());

    CHECK(read.removeValue("language"));
    CHECK_FALSE(read.removeValue("language"));
    CHECK(read.takeChanges());

    // A damaged file leaves the game with its own settings.
    PlayerSettings damaged;
    damaged.read("[volume group=");
    CHECK_FALSE(damaged.fullscreen().has_value());
}
