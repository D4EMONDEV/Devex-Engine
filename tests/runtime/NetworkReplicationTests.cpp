#include <devex/net/Bytes.hpp>
#include <devex/net/Multiplayer.hpp>
#include <devex/runtime/NetworkReplication.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/EntityRef.hpp>
#include <devex/scene/NetworkComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using devex::asset::AssetId;
using devex::core::Uuid;
using devex::net::Multiplayer;
using devex::net::MultiplayerState;
using devex::runtime::NetworkReplication;
using devex::scene::Entity;
using devex::scene::NetworkSpawner;
using devex::scene::NetworkSync;
using devex::scene::Scene;
using devex::scene::Transform;

namespace devex::scene {

// Every kind of field a NetworkSync carries.
struct ReplicatedProbe
{
    bool flag = false;
    std::int32_t whole = 0;
    std::uint32_t count = 0;
    float decimal = 0.0f;
    std::string text;
    math::Vec2 flat{0.0f};
    math::Vec3 aim{0.0f};
    math::Vec4 tint{0.0f};
    math::Quat turn{1.0f, 0.0f, 0.0f, 0.0f};
    core::Uuid id;
    asset::AssetId asset;
    EntityRef target;
    BodyType body = BodyType::Static;
};

DEVEX_REFLECT(ReplicatedProbe)
{
    type.field("flag", &ReplicatedProbe::flag)
        .field("whole", &ReplicatedProbe::whole)
        .field("count", &ReplicatedProbe::count)
        .field("decimal", &ReplicatedProbe::decimal)
        .field("text", &ReplicatedProbe::text)
        .field("flat", &ReplicatedProbe::flat)
        .field("aim", &ReplicatedProbe::aim)
        .field("tint", &ReplicatedProbe::tint)
        .field("turn", &ReplicatedProbe::turn)
        .field("id", &ReplicatedProbe::id)
        .field("asset", &ReplicatedProbe::asset, {.assetType = "texture"})
        .field("target", &ReplicatedProbe::target)
        .field("body", &ReplicatedProbe::body);
}

} // namespace devex::scene

namespace {

using devex::scene::ReplicatedProbe;

const AssetId shotPrefab{Uuid::fromParts(0x51, 0x52)};
const Uuid spawnerId = Uuid::fromParts(0x61, 0x62);
const Uuid syncedId = Uuid::fromParts(0x71, 0x72);
constexpr double frameSeconds = 1.0 / 60.0;

// The game of a player: its scene, its session and what keeps them alike, with the same scene as the
// others to start with: a spawner, and an entity that follows.
struct Player
{
    Player()
    {
        spawner = *scene.createEntity(spawnerId, "Spawner");
        scene.add<NetworkSpawner>(spawner);
        synced = *scene.createEntity(syncedId, "Synced");
        scene.add<Transform>(synced);
        NetworkSync& sync = scene.add<NetworkSync>(synced);
        sync.rate = 0.0f;
        sync.smoothing = 0.0f;
    }

    void frame(double seconds = frameSeconds)
    {
        session.poll();
        replication.receive(scene, devex::core::Duration(seconds));
        replication.send(scene, devex::core::Duration(seconds));
        session.flush();
    }

    Scene scene;
    Multiplayer session;
    NetworkReplication replication{session};
    Entity spawner;
    Entity synced;
};

template <typename Done>
bool until(std::vector<Player*> players, const Done& done)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < end)
    {
        for (Player* const player : players)
        {
            player->frame();
        }
        if (done())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// A prefab: a shot with a part, whose position its Transform says.
struct Prefabs
{
    Prefabs()
    {
        Scene source;
        const Entity shot = *source.createEntity(Uuid::fromParts(0x81, 0x82), "Shot");
        source.add<Transform>(shot);
        const Entity part = *source.createEntity(Uuid::fromParts(0x83, 0x84), "Part");
        source.add<Transform>(part);
        REQUIRE(source.setParent(part, shot));
        std::string text = devex::scene::saveScene(source);
        devex::scene::clearPrefabCache();
        devex::scene::setPrefabSourceLoader([text](AssetId) -> devex::core::Result<std::string> { return text; });
    }

    ~Prefabs()
    {
        devex::scene::setPrefabSourceLoader({});
        devex::scene::clearPrefabCache();
    }

    Prefabs(const Prefabs&) = delete;
    Prefabs& operator=(const Prefabs&) = delete;
};

void connect(Player& host, std::uint16_t port, std::vector<Player*> clients)
{
    REQUIRE(host.session.host(port));
    std::vector<Player*> everyone{&host};
    for (Player* const client : clients)
    {
        REQUIRE(client->session.join("127.0.0.1", port));
        everyone.push_back(client);
    }
    REQUIRE(until(everyone, [&] {
        for (Player* const client : clients)
        {
            if (client->session.state() != MultiplayerState::Online || client->session.peers().size() != clients.size())
            {
                return false;
            }
        }
        return host.session.peers().size() == clients.size();
    }));
}

[[nodiscard]] Entity childOf(const Scene& scene, Entity parent)
{
    return scene.firstChild(parent);
}

} // namespace

TEST_CASE("What is placed under a spawner appears on every game, and goes from them", "[runtime][net]")
{
    const Prefabs prefabs;
    Player host;
    Player client;
    connect(host, 47391, {&client});

    const Entity shot = *devex::scene::instantiatePrefab(host.scene, shotPrefab, host.spawner);
    host.scene.get<Transform>(shot).position = {1.0f, 2.0f, 3.0f};
    REQUIRE(until({&host, &client}, [&] { return childOf(client.scene, client.spawner).isValid(); }));

    // The same entity, with the same parts and where it was placed, on the other game.
    const Entity copy = childOf(client.scene, client.spawner);
    CHECK(client.scene.uuid(copy) == host.scene.uuid(shot));
    CHECK(client.scene.get<Transform>(copy).position == devex::math::Vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(client.scene.firstChild(copy).isValid());
    CHECK(client.scene.uuid(client.scene.firstChild(copy)) == host.scene.uuid(host.scene.firstChild(shot)));

    // Who joins later gets what is there at the time, and not what has gone.
    const Entity gone = *devex::scene::instantiatePrefab(host.scene, shotPrefab, host.spawner);
    const Uuid goneId = host.scene.uuid(gone);
    host.frame();
    host.scene.destroyEntity(gone);
    REQUIRE(until({&host, &client}, [&] { return !client.scene.isAlive(client.scene.findEntity(goneId)); }));

    Player late;
    REQUIRE(late.session.join("127.0.0.1", 47391));
    REQUIRE(until({&host, &client, &late}, [&] { return childOf(late.scene, late.spawner).isValid(); }));
    CHECK(late.scene.uuid(childOf(late.scene, late.spawner)) == host.scene.uuid(shot));
    CHECK(late.scene.get<Transform>(childOf(late.scene, late.spawner)).position == devex::math::Vec3(1.0f, 2.0f, 3.0f));
    CHECK_FALSE(late.scene.isAlive(late.scene.findEntity(goneId)));

    // For one that leaves, it goes with the session; joining again finds it.
    late.session.leave();
    late.frame();
    CHECK_FALSE(childOf(late.scene, late.spawner).isValid());
    REQUIRE(late.session.join("127.0.0.1", 47391));
    REQUIRE(until({&host, &client, &late}, [&] { return childOf(late.scene, late.spawner).isValid(); }));

    // It goes from all of them.
    host.scene.destroyEntity(shot);
    REQUIRE(until({&host, &client, &late}, [&] { return !childOf(client.scene, client.spawner).isValid() && !childOf(late.scene, late.spawner).isValid(); }));
}

TEST_CASE("Only the authority of a spawner places objects, and what it sends is checked", "[runtime][net]")
{
    const Prefabs prefabs;
    Player host;
    Player client;
    Player other;
    connect(host, 47392, {&client, &other});

    // A client placing an object where it has no authority: it stays on its own game.
    REQUIRE(devex::scene::instantiatePrefab(client.scene, shotPrefab, client.spawner));
    for (int frame = 0; frame < 20; ++frame)
    {
        client.frame();
    }

    // A client that sends it anyway, as a cheat would: the host neither takes it nor passes it on.
    const Uuid forged = Uuid::fromParts(0x91, 0x92);
    std::vector<std::byte> message{std::byte{0}};
    devex::net::ByteWriter writer(message);
    writer.uuid(spawnerId);
    writer.uuid(shotPrefab.uuid);
    writer.uuid(forged);
    for (int value = 0; value < 10; ++value)
    {
        writer.decimal(0.0f);
    }
    client.session.sendReplication(devex::net::hostPeer, message, devex::net::Delivery::Reliable);
    // Nor does anything it cannot read stop it: a message cut anywhere, or of another kind.
    for (std::size_t size = 1; size < message.size(); ++size)
    {
        client.session.sendReplication(devex::net::hostPeer, std::span(message).first(size), devex::net::Delivery::Reliable);
    }
    client.session.sendReplication(devex::net::hostPeer, std::vector<std::byte>{std::byte{200}}, devex::net::Delivery::Reliable);
    for (int frame = 0; frame < 30; ++frame)
    {
        client.frame();
        host.frame();
        other.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_FALSE(host.scene.isAlive(host.scene.findEntity(forged)));
    CHECK_FALSE(other.scene.isAlive(other.scene.findEntity(forged)));
    CHECK_FALSE(childOf(host.scene, host.spawner).isValid());
    CHECK_FALSE(childOf(other.scene, other.spawner).isValid());

    // Given the spawner, the client places objects, which the host passes on.
    host.session.setAuthority(spawnerId, client.session.localPeer());
    REQUIRE(until({&host, &client, &other}, [&] { return childOf(other.scene, other.spawner).isValid(); }));
    CHECK(other.scene.uuid(childOf(other.scene, other.spawner)) == client.scene.uuid(childOf(client.scene, client.spawner)));
    CHECK(childOf(host.scene, host.spawner).isValid());
}

TEST_CASE("The fields a NetworkSync lists follow the authority of the entity", "[runtime][net]")
{
    devex::scene::registerComponent<ReplicatedProbe>();
    Player host;
    Player client;
    for (Player* const player : {&host, &client})
    {
        player->scene.add<ReplicatedProbe>(player->synced);
        player->scene.get<NetworkSync>(player->synced).fields = {
            "Transform.position", "ReplicatedProbe.flag",  "ReplicatedProbe.whole", "ReplicatedProbe.count", "ReplicatedProbe.decimal",
            "ReplicatedProbe.text", "ReplicatedProbe.flat", "ReplicatedProbe.aim",   "ReplicatedProbe.tint",  "ReplicatedProbe.turn",
            "ReplicatedProbe.id",   "ReplicatedProbe.asset", "ReplicatedProbe.target", "ReplicatedProbe.body",
        };
    }
    connect(host, 47393, {&client});

    ReplicatedProbe& sent = host.scene.get<ReplicatedProbe>(host.synced);
    sent.flag = true;
    sent.whole = -12;
    sent.count = 4000000000u;
    sent.decimal = 1.5f;
    sent.text = "été";
    sent.flat = {1.0f, 2.0f};
    sent.aim = {3.0f, 4.0f, 5.0f};
    sent.tint = {6.0f, 7.0f, 8.0f, 9.0f};
    sent.turn = devex::math::Quat(0.0f, 1.0f, 0.0f, 0.0f);
    sent.id = Uuid::fromParts(1, 2);
    sent.asset = AssetId{Uuid::fromParts(3, 4)};
    sent.target = devex::scene::EntityRef{syncedId};
    sent.body = devex::scene::BodyType::Dynamic;
    host.scene.get<Transform>(host.synced).position = {10.0f, 0.0f, 0.0f};

    const ReplicatedProbe& seen = client.scene.get<ReplicatedProbe>(client.synced);
    REQUIRE(until({&host, &client}, [&] { return seen.text == "été"; }));
    CHECK(seen.flag);
    CHECK(seen.whole == -12);
    CHECK(seen.count == 4000000000u);
    CHECK(seen.decimal == 1.5f);
    CHECK(seen.flat == devex::math::Vec2(1.0f, 2.0f));
    CHECK(seen.aim == devex::math::Vec3(3.0f, 4.0f, 5.0f));
    CHECK(seen.tint == devex::math::Vec4(6.0f, 7.0f, 8.0f, 9.0f));
    CHECK(seen.turn.w == 0.0f);
    CHECK(seen.turn.x == 1.0f);
    CHECK(seen.id == Uuid::fromParts(1, 2));
    CHECK(seen.asset.uuid == Uuid::fromParts(3, 4));
    CHECK(seen.target.uuid == syncedId);
    CHECK(seen.body == devex::scene::BodyType::Dynamic);
    CHECK(client.scene.get<Transform>(client.synced).position.x == 10.0f);

    // What a player that is not the authority changes does not go, and is taken back by the next values.
    client.scene.get<ReplicatedProbe>(client.synced).whole = 99;
    for (int frame = 0; frame < 20; ++frame)
    {
        client.frame();
        host.frame();
    }
    CHECK(host.scene.get<ReplicatedProbe>(host.synced).whole == -12);
    host.scene.get<ReplicatedProbe>(host.synced).whole = -13;
    REQUIRE(until({&host, &client}, [&] { return seen.whole == -13; }));

    // Who joins later has them at once, even if they stopped changing long ago.
    Player late;
    late.scene.add<ReplicatedProbe>(late.synced);
    late.scene.get<NetworkSync>(late.synced).fields = host.scene.get<NetworkSync>(host.synced).fields;
    REQUIRE(late.session.join("127.0.0.1", 47393));
    REQUIRE(until({&host, &client, &late}, [&] { return late.scene.get<ReplicatedProbe>(late.synced).whole == -13; }));
    CHECK(late.scene.get<ReplicatedProbe>(late.synced).text == "été");

    // Given to a client, it decides, for the host as for the other: through the host, which passes it on.
    host.session.setAuthority(syncedId, client.session.localPeer());
    REQUIRE(until({&host, &client, &late}, [&] { return client.session.isAuthority(syncedId) && late.session.authority(syncedId) == client.session.localPeer(); }));
    client.scene.get<ReplicatedProbe>(client.synced).text = "du client";
    REQUIRE(until({&host, &client, &late}, [&] { return late.scene.get<ReplicatedProbe>(late.synced).text == "du client"; }));
    CHECK(host.scene.get<ReplicatedProbe>(host.synced).text == "du client");

    // A list of fields that is not the same everywhere does not follow, rather than read wrong values.
    late.scene.get<NetworkSync>(late.synced).fields = {"Transform.position"};
    client.scene.get<ReplicatedProbe>(client.synced).text = "encore";
    REQUIRE(until({&host, &client, &late}, [&] { return host.scene.get<ReplicatedProbe>(host.synced).text == "encore"; }));
    CHECK(late.scene.get<ReplicatedProbe>(late.synced).text == "du client");

    // Back to the host when its player leaves.
    client.session.leave();
    REQUIRE(until({&host, &late}, [&] { return late.session.authority(syncedId) == devex::net::hostPeer; }));
    CHECK(host.session.isAuthority(syncedId));
}

TEST_CASE("The others see a moving object slightly late and smoothly, and where it stops", "[runtime][net]")
{
    Player host;
    Player client;
    for (Player* const player : {&host, &client})
    {
        NetworkSync& sync = player->scene.get<NetworkSync>(player->synced);
        sync.rate = 20.0f;
        sync.smoothing = 0.1f;
    }
    connect(host, 47394, {&client});

    // At 10 units per second, in frames of a 60th: only one in three leaves, but every frame moves it.
    const float speed = 10.0f;
    float where = 0.0f;
    float previous = 0.0f;
    float biggestStep = 0.0f;
    bool started = false;
    for (int frame = 0; frame < 180; ++frame)
    {
        where += speed * static_cast<float>(frameSeconds);
        host.scene.get<Transform>(host.synced).position.x = where;
        host.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        client.frame();
        const float seen = client.scene.get<Transform>(client.synced).position.x;
        CHECK(seen >= previous);
        CHECK(seen <= where);
        if (started)
        {
            biggestStep = std::max(biggestStep, seen - previous);
        }
        started = started || seen > 0.0f;
        previous = seen;
    }
    REQUIRE(started);
    // Late, by about what it was told; moving a little at each frame rather than jumping to each value,
    // which would be half a unit at a time.
    CHECK(where - previous >= 0.5f);
    CHECK(where - previous <= 2.5f);
    CHECK(biggestStep < 0.4f);

    // Once it stops, it ends up exactly there.
    REQUIRE(until({&host, &client}, [&] { return client.scene.get<Transform>(client.synced).position.x == where; }));
}

TEST_CASE("A body another player has authority over is not simulated here", "[runtime][net]")
{
    Player host;
    Player client;
    for (Player* const player : {&host, &client})
    {
        player->scene.add<devex::scene::RigidBody>(player->synced);
        REQUIRE(player->scene.get<devex::scene::RigidBody>(player->synced).type == devex::scene::BodyType::Dynamic);
    }
    connect(host, 47397, {&client});
    REQUIRE(until({&host, &client}, [&] { return client.scene.get<devex::scene::RigidBody>(client.synced).type == devex::scene::BodyType::Kinematic; }));
    CHECK(host.scene.get<devex::scene::RigidBody>(host.synced).type == devex::scene::BodyType::Dynamic);

    // The other way round when authority changes hands.
    host.session.setAuthority(syncedId, client.session.localPeer());
    REQUIRE(until({&host, &client}, [&] {
        return host.scene.get<devex::scene::RigidBody>(host.synced).type == devex::scene::BodyType::Kinematic &&
               client.scene.get<devex::scene::RigidBody>(client.synced).type == devex::scene::BodyType::Dynamic;
    }));

    // And its own again on its own: the host takes it back when the client leaves.
    client.session.leave();
    REQUIRE(until({&host}, [&] { return host.scene.get<devex::scene::RigidBody>(host.synced).type == devex::scene::BodyType::Dynamic; }));
}

TEST_CASE("Authority given to an entity covers what is under it", "[runtime][net]")
{
    Player host;
    const Entity child = *host.scene.createEntity(Uuid::fromParts(0xa1, 0xa2), "Child");
    REQUIRE(host.scene.setParent(child, host.synced));
    devex::runtime::setAuthority(host.scene, host.session, host.synced, 7);
    CHECK(host.session.authority(syncedId) == 7);
    CHECK(host.session.authority(Uuid::fromParts(0xa1, 0xa2)) == 7);
    CHECK(host.session.authority(spawnerId) == devex::net::hostPeer);
}
