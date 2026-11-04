#include <devex/net/Multiplayer.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/Game.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

using devex::net::Multiplayer;
using devex::runtime::GameRegistry;
using devex::runtime::SystemContext;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

// What the remote functions of these tests saw.
struct Seen
{
    int hits = 0;
    int damage = 0;
    std::string text;
    Entity target;
    Entity other;
    devex::net::PeerId sender = 0;
    float decimal = 0.0f;
};
Seen seen;

void hit(SystemContext& context, Entity entity, int damage)
{
    ++seen.hits;
    seen.damage = damage;
    seen.target = entity;
    seen.sender = context.multiplayer->remoteSender();
}

void ping(SystemContext&, Entity)
{
    ++seen.hits;
}

void say(SystemContext& context, Entity, const std::string& text, Entity other, float decimal)
{
    seen.text = text;
    seen.other = other;
    seen.decimal = decimal;
    seen.sender = context.multiplayer->remoteSender();
}

// What a frame needs and the games of a test share, a process having one platform.
struct Services
{
    Services()
    {
        auto created = devex::platform::Platform::create();
        REQUIRE(created.has_value());
        platform = std::make_unique<devex::platform::Platform>(std::move(*created));
        auto opened = platform->createWindow({.hidden = true});
        REQUIRE(opened.has_value());
        window = std::make_unique<devex::platform::Window>(std::move(*opened));
    }

    std::unique_ptr<devex::platform::Platform> platform;
    std::unique_ptr<devex::platform::Window> window;
    devex::runtime::AssetManager assets{nullptr, nullptr};
};

// The game of one player: a scene with the same entity as the others.
struct Game
{
    Game(Services& shared, devex::core::Uuid uuid)
        : services(shared)
    {
        auto made = scene.createEntity(uuid, "Player");
        REQUIRE(made.has_value());
        entity = *made;
        game.rpc("Player.Hit", &hit);
        game.rpc("Player.Say", &say, {.anyPeer = true});
        game.rpc("Player.Here", &hit, {.callLocal = true});
        game.rpc("Player.Ping", &ping, {.callLocal = true});
        for (const devex::runtime::RpcInfo& rpc : game.rpcs())
        {
            session.declareRpc(rpc.name, rpc.policy);
        }
    }

    [[nodiscard]] SystemContext context()
    {
        return {.scene = scene, .input = services.platform->input(), .window = *services.window, .assets = services.assets, .multiplayer = &session, .game = &game};
    }

    void frame()
    {
        session.poll();
        SystemContext now = context();
        game.dispatchRpcs(now);
    }

    Services& services;
    Scene scene;
    GameRegistry game;
    Multiplayer session;
    Entity entity;
};

template <typename Done>
bool until(Game& first, Game& second, const Done& done)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < end)
    {
        first.frame();
        second.frame();
        if (done())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

TEST_CASE("A remote function outside a session runs only when it says so", "[runtime][net]")
{
    seen = {};
    Services services;
    Game game(services, devex::core::Uuid::fromParts(1, 2));
    SystemContext context = game.context();
    devex::runtime::rpc(context, game.entity, "Player.Hit", 25);
    CHECK(seen.hits == 0);
    devex::runtime::rpc(context, game.entity, "Player.Here", 25);
    CHECK(seen.hits == 1);
    CHECK(seen.damage == 25);
    CHECK(seen.target == game.entity);
    // On its own, this game is the host: the one a call comes from.
    CHECK(seen.sender == devex::net::hostPeer);
    CHECK(game.session.remoteSender() == 0);
    devex::runtime::rpc(context, game.entity, "Player.Ping");
    CHECK(seen.hits == 2);
}

TEST_CASE("Remote functions run on the same entity of the other game", "[runtime][net]")
{
    seen = {};
    const devex::core::Uuid uuid = devex::core::Uuid::fromParts(3, 4);
    Services services;
    Game host(services, uuid);
    Game client(services, uuid);
    REQUIRE(host.session.host(47390));
    REQUIRE(client.session.join("127.0.0.1", 47390));
    REQUIRE(until(host, client, [&] { return client.session.state() == devex::net::MultiplayerState::Online && !host.session.peers().empty(); }));
    const devex::net::PeerId clientPeer = client.session.localPeer();

    // Only the authority of an entity, the host for now, calls it; the client's call does not leave.
    SystemContext onClient = client.context();
    devex::runtime::rpc(onClient, client.entity, "Player.Hit", 1);
    SystemContext onHost = host.context();
    devex::runtime::rpc(onHost, host.entity, "Player.Hit", 25);
    REQUIRE(until(host, client, [] { return seen.hits != 0; }));
    CHECK(seen.hits == 1);
    CHECK(seen.damage == 25);
    CHECK(seen.target == client.entity);
    CHECK(seen.sender == devex::net::hostPeer);

    // Any player for what says so, with an entity for an argument and a whole number for a decimal.
    devex::runtime::rpc(onClient, client.entity, "Player.Say", "bonjour", client.entity, 3);
    REQUIRE(until(host, client, [] { return !seen.text.empty(); }));
    CHECK(seen.text == "bonjour");
    CHECK(seen.other == host.entity);
    CHECK(seen.decimal == 3.0f);
    CHECK(seen.sender == clientPeer);

    // What is not what the function takes does not run it, and neither does what no one declared.
    seen = {};
    devex::runtime::rpc(onClient, client.entity, "Player.Say", 12);
    devex::runtime::rpc(onClient, client.entity, "Player.Nothing", 12);
    devex::runtime::rpc(onClient, client.entity, "Player.Say", "suite", client.entity, 1.5);
    REQUIRE(until(host, client, [] { return !seen.text.empty(); }));
    CHECK(seen.text == "suite");

    // Authority moves to the client on both games.
    seen = {};
    host.session.setAuthority(uuid, clientPeer);
    client.session.setAuthority(uuid, clientPeer);
    devex::runtime::rpc(onHost, host.entity, "Player.Hit", 2);
    devex::runtime::rpc(onClient, client.entity, "Player.Hit", 3);
    REQUIRE(until(host, client, [] { return seen.hits != 0; }));
    CHECK(seen.damage == 3);
    CHECK(seen.sender == clientPeer);
}
