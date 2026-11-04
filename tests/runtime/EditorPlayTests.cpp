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
using runtime::detail::PlayGroup;
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

template <class Controller, class Predicate>
bool until(Controller& process, Predicate predicate, std::chrono::milliseconds limit = 5s)
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

namespace
{
[[nodiscard]] bool logged(const Fixture& fixture, std::string_view text)
{
    return std::ranges::any_of(fixture.logs, [&](const auto& log) { return log.second.find(text) != std::string::npos; });
}
} // namespace

TEST_CASE("Play starts several games, connected to one another, stopped together", "[runtime][playprocess][net]")
{
    Fixture fixture;
    scene::Scene scene;
    static_cast<void>(scene.createEntity("Ask"));
    PlayGroup group;
    REQUIRE(group.start(fixture.project, scene, {}, DEVEX_TEST_PLAY_PROCESS, false, 3, true, platform::ScreenArea{.width = 1600, .height = 900}));
    CHECK(group.status().running);
    CHECK(group.status().starting);

    // Each is in the session as soon as it runs, and the others come. What a game does to host changes
    // nothing, which it sees as done.
    REQUIRE(until(group, [&] { return logged(fixture, "[Player 3] Network instance=3 online=true host=false"); }, 30s));
    REQUIRE(until(group, [&] { return logged(fixture, "[Player 1] Network instance=1 online=true host=true peer=1 others=2 asked=true"); }, 15s));
    REQUIRE(until(group, [&] { return logged(fixture, "[Player 2] Network instance=2 online=true host=false peer=2 others=2 asked=true"); }, 15s));
    CHECK(logged(fixture, "[Player 3] Network instance=3 online=true host=false peer=3 others=2 asked=true"));
    CHECK_FALSE(group.status().starting);

    // Paused and stopped together.
    group.pause(true);
    CHECK(group.pauseRequested());
    REQUIRE(until(group, [&] { return group.status().paused; }));
    group.pause(false);
    REQUIRE(until(group, [&] { return !group.status().paused; }));
    group.stop();
    REQUIRE(until(group, [&] { return !group.status().running; }, 10s));
}

TEST_CASE("Play can leave it to the game to connect its instances", "[runtime][playprocess][net]")
{
    Fixture fixture;
    scene::Scene scene;
    static_cast<void>(scene.createEntity("Arena"));
    PlayGroup group;
    REQUIRE(group.start(fixture.project, scene, {}, DEVEX_TEST_PLAY_PROCESS, false, 2, false, platform::ScreenArea{.width = 1600, .height = 900}));
    // Each knows which it is, and nothing has connected them.
    REQUIRE(until(group, [&] { return logged(fixture, "[Player 1] Network instance=1 online=false") && logged(fixture, "[Player 2] Network instance=2 online=false"); }, 30s));
    group.shutdown();
    CHECK_FALSE(group.status().running);
}

TEST_CASE("Games that come after a failed one in Play do not start", "[runtime][playprocess][net]")
{
    Fixture fixture;
    scene::Scene scene;
    // A game that cannot open its project ends before it is ready. A failed game keeps its snapshot, and
    // the one that never started does not leave one.
    std::filesystem::remove(fixture.project.file);
    PlayGroup group;
    REQUIRE(group.start(fixture.project, scene, {}, DEVEX_TEST_PLAY_PROCESS, false, 2, true, {}));
    REQUIRE(until(group, [&] { return !group.status().running; }, 30s));
    const auto snapshots = std::ranges::distance(std::filesystem::directory_iterator(fixture.project.cacheDirectory() / "play"), std::default_sentinel);
    CHECK(snapshots == 1);
}

TEST_CASE("A game Play started reads which one it is and where its window goes", "[runtime][playprocess][net]")
{
    Fixture fixture;
    scene::Scene scene;
    PlayProcess process;
    const runtime::detail::PlayInstance instance{.index = 2, .count = 4, .port = 47123, .area = platform::ScreenArea{.x = -100, .y = 20, .width = 800, .height = 450}};
    REQUIRE(process.prepare(fixture.project, scene, {}, true, instance));
    const auto directory = process.directory();
    const auto session = runtime::detail::readPlaySession(directory);
    REQUIRE(session);
    CHECK(session->waitForDebugger);
    CHECK(session->instance.index == 2);
    CHECK(session->instance.count == 4);
    CHECK(session->instance.port == 47123);
    REQUIRE(session->instance.area.has_value());
    CHECK(session->instance.area->x == -100);
    CHECK(session->instance.area->y == 20);
    CHECK(session->instance.area->width == 800);
    CHECK(session->instance.area->height == 450);
}

TEST_CASE("A prepared game that never starts leaves nothing behind", "[runtime][playprocess]")
{
    Fixture fixture;
    scene::Scene scene;
    std::filesystem::path directory;
    {
        PlayProcess process;
        REQUIRE(process.prepare(fixture.project, scene, {}, false, {}));
        directory = process.directory();
        CHECK(std::filesystem::exists(directory));
    }
    CHECK_FALSE(std::filesystem::exists(directory));
}

TEST_CASE("The games of Play share the screen, side by side or in a grid", "[runtime][playprocess]")
{
    const platform::ScreenArea screen{.x = 100, .y = 50, .width = 1600, .height = 900};
    const auto first = runtime::detail::playWindowArea(screen, 1, 2);
    const auto second = runtime::detail::playWindowArea(screen, 2, 2);
    CHECK((first.x == 100 && first.y == 50 && first.width == 800 && first.height == 900));
    CHECK((second.x == 900 && second.y == 50 && second.width == 800 && second.height == 900));
    const auto third = runtime::detail::playWindowArea(screen, 3, 3);
    CHECK((third.x == 100 && third.y == 500 && third.width == 800 && third.height == 450));
    const auto fourth = runtime::detail::playWindowArea(screen, 4, 4);
    CHECK((fourth.x == 900 && fourth.y == 500 && fourth.width == 800 && fourth.height == 450));
}
