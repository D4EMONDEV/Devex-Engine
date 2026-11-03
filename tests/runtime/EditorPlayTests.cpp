#include "runtime/EditorPlay.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

using namespace devex;
using runtime::detail::PlayProcess;
using namespace std::chrono_literals;

namespace
{
struct Fixture
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        core::pathFromUtf8("devex play été " + core::Uuid::generate().toString());
    asset::Project project;
    std::vector<std::pair<core::LogLevel, std::string>> logs;
    core::LogSinkId sink;
    Fixture()
    {
        auto created = asset::createProject(root, "Play process");
        REQUIRE(created);
        project = *created;
        sink = core::addLogSink(
            [this](const core::LogRecord& record)
            {
                if (record.category == "test-player")
                    logs.emplace_back(record.level, record.message);
            });
    }
    ~Fixture()
    {
        core::removeLogSink(sink);
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    [[nodiscard]] std::size_t ticks() const
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            logs, [](const auto& log) { return log.second.starts_with("Tick "); }));
    }
};

template <class Predicate>
bool until(PlayProcess& process, Predicate predicate, std::chrono::milliseconds limit = 5s)
{
    const auto end = std::chrono::steady_clock::now() + limit;
    do
    {
        process.update();
        if (predicate())
            return true;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
void pollFor(PlayProcess& process, std::chrono::milliseconds duration)
{
    static_cast<void>(until(process, [] { return false; }, duration));
}
} // namespace

TEST_CASE("A separate player runs the unsaved scene and accepts pause step resume and stop",
          "[runtime][playprocess]")
{
    Fixture fixture;
    const auto projectText = *core::readTextFile(fixture.project.file);
    scene::Scene scene;
    scene.setKind(scene::SceneKind::TwoD);
    static_cast<void>(scene.createEntity("Unsaved player"));
    PlayProcess player;
    REQUIRE(player.start(fixture.project, scene, {}, DEVEX_TEST_PLAY_PROCESS));
    CHECK(player.status().processId != 0);
    CHECK(player.status().processId != platform::currentProcessId());
    CHECK(*core::readTextFile(fixture.project.file) == projectText);
    REQUIRE(until(player, [&] { return fixture.ticks() >= 3; }));
    CHECK(std::ranges::any_of(fixture.logs, [](const auto& log)
                              { return log.second == "Snapshot: Unsaved player"; }));
    CHECK(std::ranges::any_of(fixture.logs,
                              [](const auto& log)
                              {
                                  return log.first == core::LogLevel::Warning &&
                                         log.second.starts_with(
                                             "A quoted \"message\"\nwith another line");
                              }));
    player.pause(true);
    REQUIRE(until(player, [&] { return player.status().paused; }));
    const auto before = fixture.ticks();
    pollFor(player, 150ms);
    CHECK(fixture.ticks() == before);
    player.step();
    REQUIRE(until(player, [&] { return fixture.ticks() == before + 1; }));
    pollFor(player, 100ms);
    CHECK(fixture.ticks() == before + 1);
    CHECK(player.status().paused);
    player.pause(false);
    REQUIRE(until(player, [&] { return !player.status().paused && fixture.ticks() > before + 2; }));
    const auto directory = player.directory();
    player.stop();
    REQUIRE(until(player, [&] { return !player.status().running; }));
    CHECK(std::ranges::any_of(fixture.logs,
                              [](const auto& log) { return log.second == "Shutdown"; }));
    CHECK_FALSE(std::filesystem::exists(directory));
    CHECK(*core::readTextFile(fixture.project.file) == projectText);
    REQUIRE(player.start(fixture.project, scene, {}, DEVEX_TEST_PLAY_PROCESS));
    player.shutdown();
    CHECK_FALSE(player.status().running);
}

TEST_CASE("A failed or hung game process leaves its supervisor usable", "[runtime][playprocess]")
{
    Fixture fixture;
    scene::Scene scene;
    PlayProcess player;
    SECTION("Abrupt exit keeps the failed session and its log")
    {
        static_cast<void>(scene.createEntity("Crash"));
        REQUIRE(player.start(fixture.project, scene, {}, DEVEX_TEST_PLAY_PROCESS));
        const auto directory = player.directory();
        REQUIRE(until(player, [&] { return !player.status().running; }));
        CHECK(std::filesystem::exists(directory / "logs/player.log"));
        CHECK(std::filesystem::exists(directory / "scene.dvxscene"));
    }
    SECTION("Stop terminates a child that no longer processes commands")
    {
        static_cast<void>(scene.createEntity("Hang"));
        REQUIRE(player.start(fixture.project, scene, {}, DEVEX_TEST_PLAY_PROCESS));
        REQUIRE(until(player, [&] { return !player.status().starting; }));
        pollFor(player, 40ms);
        player.stop();
        REQUIRE(until(player, [&] { return !player.status().running; }));
    }
    CHECK_FALSE(player.status().running);
    scene::Scene next;
    static_cast<void>(next.createEntity("Next game"));
    REQUIRE(player.start(fixture.project, next, {}, DEVEX_TEST_PLAY_PROCESS));
    REQUIRE(until(player, [&] { return !player.status().starting; }));
    player.shutdown();
    CHECK_FALSE(player.status().running);
}

TEST_CASE("A missing standalone player reports a launch error without changing the scene",
          "[runtime][playprocess]")
{
    Fixture fixture;
    scene::Scene scene;
    static_cast<void>(scene.createEntity("Unchanged"));
    const auto before = scene::saveScene(scene);
    PlayProcess player;
    CHECK_FALSE(player.start(fixture.project, scene, {}, fixture.root / "missing-player"));
    CHECK_FALSE(player.status().running);
    CHECK(scene::saveScene(scene) == before);
    CHECK_FALSE(std::filesystem::exists(fixture.project.cacheDirectory() / "play"));
}
