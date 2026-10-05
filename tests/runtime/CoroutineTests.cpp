#include <devex/animation/TweenWorld.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/Coroutine.hpp>
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using devex::core::Duration;
using devex::runtime::Coroutine;
using devex::runtime::CoroutineContext;
using devex::runtime::CoroutineHandle;
using devex::runtime::CoroutineScheduler;
using devex::runtime::SystemContext;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

// What a coroutine needs to run: a scene and the services of a frame.
struct Game
{
    Game()
    {
        auto created = devex::platform::Platform::create();
        REQUIRE(created.has_value());
        platform = std::make_unique<devex::platform::Platform>(std::move(*created));
        auto opened = platform->createWindow({.hidden = true});
        REQUIRE(opened.has_value());
        window = std::make_unique<devex::platform::Window>(std::move(*opened));
    }

    // One frame of the given length, during which the coroutines go on.
    void frame(double seconds)
    {
        SystemContext context{.scene = scene,
                              .input = platform->input(),
                              .window = *window,
                              .assets = assets,
                              .tweens = &tweens,
                              .coroutines = &coroutines,
                              .delta = Duration(seconds)};
        coroutines.update(context);
        tweens.update(scene, Duration(seconds));
    }

    CoroutineHandle start(const CoroutineScheduler::Function& function, Entity owner = {})
    {
        SystemContext context{.scene = scene,
                              .input = platform->input(),
                              .window = *window,
                              .assets = assets,
                              .tweens = &tweens,
                              .coroutines = &coroutines};
        return coroutines.start(context, function, owner);
    }

    std::unique_ptr<devex::platform::Platform> platform;
    std::unique_ptr<devex::platform::Window> window;
    devex::runtime::AssetManager assets{nullptr, nullptr};
    Scene scene;
    devex::animation::TweenWorld tweens;
    CoroutineScheduler coroutines;
};

Coroutine countFrames(CoroutineContext& co, std::vector<std::string>& log)
{
    log.emplace_back("started");
    co_await co.nextFrame();
    log.emplace_back("frame 1");
    SystemContext& frame = co_await co.wait(1.0f);
    log.push_back(std::format("waited {}", frame.delta.count()));
    co_await co.until([&log](SystemContext&) { return log.size() >= 4; });
    log.emplace_back("done");
}

// Tells when its frame is destroyed, as the destructors of its locals run.
struct Witness
{
    explicit Witness(bool& flag)
        : destroyed(flag)
    {
    }
    ~Witness()
    {
        destroyed = true;
    }
    Witness(const Witness&) = delete;
    Witness& operator=(const Witness&) = delete;
    bool& destroyed;
};

Coroutine waitForever(CoroutineContext& co, bool& destroyed, int& resumed)
{
    Witness witness(destroyed);
    while (true)
    {
        co_await co.nextFrame();
        ++resumed;
    }
}

} // namespace

TEST_CASE("A coroutine waits for frames, time and conditions", "[runtime][coroutine]")
{
    Game game;
    std::vector<std::string> log;
    const CoroutineHandle handle = game.start([&log](CoroutineContext& co) { return countFrames(co, log); });
    // It runs at once until it first waits.
    CHECK(log == std::vector<std::string>{"started"});
    CHECK(game.coroutines.isRunning(handle));
    game.frame(0.1);
    CHECK(log.size() == 2);
    game.frame(0.6);
    game.frame(0.3);
    CHECK(log.size() == 2);
    game.frame(0.2);
    // The frame it resumes in is the one each wait gives.
    REQUIRE(log.size() == 3);
    CHECK(log[2] == "waited 0.2");
    game.frame(0.1);
    CHECK(log.size() == 3);
    log.emplace_back("pushed");
    game.frame(0.1);
    CHECK(log.back() == "done");
    CHECK_FALSE(game.coroutines.isRunning(handle));
    CHECK(game.coroutines.count() == 0);
}

TEST_CASE("A coroutine waits for a tween to end", "[runtime][coroutine]")
{
    Game game;
    const Entity entity = game.scene.createEntity("Door");
    game.scene.add<devex::scene::Transform>(entity);
    bool opened = false;
    game.start([&](CoroutineContext& co) -> Coroutine {
        auto played = co.frame().tweens->play(co.frame().scene, {.entity = entity,
                                                                  .field = "Transform.position",
                                                                  .to = devex::math::Vec4{0.0f, 3.0f, 0.0f, 0.0f},
                                                                  .duration = 1.0f});
        if (!played)
        {
            co_return;
        }
        SystemContext& frame = co_await co.tween(*played);
        opened = frame.scene.get<devex::scene::Transform>(entity).position.y == 3.0f;
    });
    game.frame(0.5);
    game.frame(0.4);
    CHECK_FALSE(opened);
    game.frame(0.2);
    game.frame(0.0);
    CHECK(opened);
}

TEST_CASE("A coroutine ends with its owner, on demand, or with the scene", "[runtime][coroutine]")
{
    Game game;
    const Entity owner = game.scene.createEntity("Owner");
    bool ownedDestroyed = false;
    int ownedResumed = 0;
    game.start([&](CoroutineContext& co) { return waitForever(co, ownedDestroyed, ownedResumed); }, owner);
    bool stoppedDestroyed = false;
    int stoppedResumed = 0;
    const CoroutineHandle stopped =
        game.start([&](CoroutineContext& co) { return waitForever(co, stoppedDestroyed, stoppedResumed); });
    bool clearedDestroyed = false;
    int clearedResumed = 0;
    game.start([&](CoroutineContext& co) { return waitForever(co, clearedDestroyed, clearedResumed); });

    game.frame(0.1);
    CHECK(ownedResumed == 1);
    CHECK(stoppedResumed == 1);
    game.scene.destroyEntity(owner);
    game.coroutines.stop(stopped);
    CHECK_FALSE(game.coroutines.isRunning(stopped));
    game.frame(0.1);
    CHECK(ownedResumed == 1);
    CHECK(ownedDestroyed);
    CHECK(stoppedResumed == 1);
    CHECK(stoppedDestroyed);
    CHECK(clearedResumed == 2);
    CHECK(game.coroutines.count() == 1);
    game.coroutines.clear();
    CHECK(clearedDestroyed);
    CHECK(game.coroutines.count() == 0);
}

TEST_CASE("A coroutine that throws ends and the others go on", "[runtime][coroutine]")
{
    Game game;
    int resumed = 0;
    game.start([&](CoroutineContext& co) -> Coroutine {
        co_await co.nextFrame();
        throw std::runtime_error("broken");
    });
    game.start([&](CoroutineContext& co) -> Coroutine {
        co_await co.nextFrame();
        ++resumed;
        // Coroutines start coroutines, which run at once.
        co.frame().coroutines->start(co.frame(), [&](CoroutineContext& inner) -> Coroutine {
            ++resumed;
            co_await inner.nextFrame();
            ++resumed;
        });
    });
    // One that never waits is over at once.
    const CoroutineHandle instant = game.start([&](CoroutineContext&) -> Coroutine {
        ++resumed;
        co_return;
    });
    CHECK_FALSE(game.coroutines.isRunning(instant));
    CHECK(resumed == 1);
    game.frame(0.1);
    CHECK(resumed == 3);
    CHECK(game.coroutines.count() == 1);
    game.frame(0.1);
    CHECK(resumed == 4);
    CHECK(game.coroutines.count() == 0);
}
