#include <devex/net/Multiplayer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using devex::net::Delivery;
using devex::net::Multiplayer;
using devex::net::MultiplayerState;
using devex::net::NetworkEvent;
using devex::net::PeerId;
using devex::net::RpcCall;
using devex::net::RpcPolicy;
using devex::net::RpcValue;

namespace {

// Each test on its own port, so that no other can answer: ctest runs each in its own process, possibly at
// the same time, which leaves only their names to tell them apart.
std::uint16_t nextPort()
{
    static std::uint16_t next = 0;
    const std::size_t name = std::hash<std::string>{}(Catch::getResultCapture().getCurrentTestName());
    return static_cast<std::uint16_t>(46000 + (name + next++) % 1000);
}

struct Player
{
    Multiplayer session;
    std::vector<NetworkEvent> events;

    [[nodiscard]] bool saw(NetworkEvent::Kind kind, PeerId peer = 0) const
    {
        return std::ranges::any_of(events, [&](const NetworkEvent& event) { return event.kind == kind && (peer == 0 || event.peer == peer); });
    }
    [[nodiscard]] std::string lastMessage(PeerId from) const
    {
        std::string text;
        for (const NetworkEvent& event : events)
        {
            if (event.kind == NetworkEvent::Kind::Message && event.peer == from)
            {
                text.assign(reinterpret_cast<const char*>(event.bytes.data()), event.bytes.size());
            }
        }
        return text;
    }
};

// Polls everyone, keeping what they saw, until it comes true or it is too late.
bool until(std::vector<Player*> players, const std::function<bool()>& done, std::chrono::seconds patience = std::chrono::seconds(3))
{
    const auto end = std::chrono::steady_clock::now() + patience;
    while (std::chrono::steady_clock::now() < end)
    {
        for (Player* player : players)
        {
            player->session.poll();
            const auto events = player->session.events();
            player->events.insert(player->events.end(), events.begin(), events.end());
        }
        if (done())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void say(Player& player, PeerId to, std::string_view text, Delivery delivery = Delivery::Reliable)
{
    player.session.send(to, std::as_bytes(std::span(text.data(), text.size())), delivery);
    player.session.flush();
}

} // namespace

TEST_CASE("Players join a host, learn about each other, and leave", "[net]")
{
    const std::uint16_t port = nextPort();
    Player host;
    Player first;
    Player second;
    CHECK(host.session.state() == MultiplayerState::Offline);
    REQUIRE(host.session.host(port));
    CHECK(host.session.isHost());
    CHECK(host.session.localPeer() == devex::net::hostPeer);
    CHECK_FALSE(host.session.host(port));

    REQUIRE(first.session.join("127.0.0.1", port));
    CHECK(first.session.state() == MultiplayerState::Connecting);
    CHECK(first.session.localPeer() == 0);
    REQUIRE(until({&host, &first}, [&] { return first.session.state() == MultiplayerState::Online; }));
    const PeerId firstId = first.session.localPeer();
    CHECK(firstId > devex::net::hostPeer);
    CHECK_FALSE(first.session.isHost());
    CHECK(first.saw(NetworkEvent::Kind::Connected, firstId));
    CHECK(host.saw(NetworkEvent::Kind::PeerJoined, firstId));

    // The second is told about the first, and the first about the second.
    REQUIRE(second.session.join("localhost", port));
    REQUIRE(until({&host, &first, &second}, [&] { return second.session.state() == MultiplayerState::Online && first.session.peers().size() == 2; }));
    const PeerId secondId = second.session.localPeer();
    CHECK(secondId != firstId);
    CHECK(std::ranges::contains(second.session.peers(), devex::net::hostPeer));
    CHECK(std::ranges::contains(second.session.peers(), firstId));
    CHECK(std::ranges::contains(first.session.peers(), secondId));
    CHECK(first.saw(NetworkEvent::Kind::PeerJoined, secondId));
    CHECK(host.session.peers().size() == 2);

    // Leaving is seen by those who stay.
    second.session.leave();
    CHECK(second.session.state() == MultiplayerState::Offline);
    CHECK(second.session.localPeer() == 0);
    REQUIRE(until({&host, &first}, [&] { return first.session.peers().size() == 1 && host.session.peers().size() == 1; }));
    CHECK(host.saw(NetworkEvent::Kind::PeerLeft, secondId));
    CHECK(first.saw(NetworkEvent::Kind::PeerLeft, secondId));

    // A host leaving ends the session for the rest.
    host.session.leave();
    REQUIRE(until({&first}, [&] { return first.session.state() == MultiplayerState::Offline; }));
    CHECK(first.saw(NetworkEvent::Kind::Disconnected));
}

TEST_CASE("Messages reach their players, through the host between clients, with the right sender", "[net]")
{
    const std::uint16_t port = nextPort();
    Player host;
    Player first;
    Player second;
    REQUIRE(host.session.host(port));
    REQUIRE(first.session.join("127.0.0.1", port));
    REQUIRE(second.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &first, &second}, [&] {
        return first.session.peers().size() == 2 && second.session.peers().size() == 2;
    }));
    const PeerId firstId = first.session.localPeer();
    const PeerId secondId = second.session.localPeer();

    say(host, firstId, "to the first");
    REQUIRE(until({&host, &first, &second}, [&] { return first.lastMessage(devex::net::hostPeer) == "to the first"; }));
    CHECK(second.lastMessage(devex::net::hostPeer).empty());

    say(first, devex::net::hostPeer, "to the host");
    REQUIRE(until({&host, &first, &second}, [&] { return host.lastMessage(firstId) == "to the host"; }));

    // Between clients, the sender is the one the host knows.
    say(first, secondId, "between clients");
    REQUIRE(until({&host, &first, &second}, [&] { return second.lastMessage(firstId) == "between clients"; }));
    CHECK(host.lastMessage(firstId) == "to the host");

    // To everyone but the sender, reliable or not.
    say(second, devex::net::everyone, "from the second");
    REQUIRE(until({&host, &first, &second}, [&] { return host.lastMessage(secondId) == "from the second" && first.lastMessage(secondId) == "from the second"; }));
    CHECK(second.lastMessage(secondId).empty());
    say(host, devex::net::everyone, "unreliable", Delivery::Unreliable);
    REQUIRE(until({&host, &first, &second}, [&] { return first.lastMessage(devex::net::hostPeer) == "unreliable" && second.lastMessage(devex::net::hostPeer) == "unreliable"; }));

    // What a player sends itself goes nowhere.
    say(first, firstId, "to myself");
    REQUIRE(until({&host, &first, &second}, [&] { return !first.events.empty(); }));
    CHECK(first.lastMessage(firstId).empty());
}

TEST_CASE("A session refuses what it cannot do, and says when no one answers", "[net]")
{
    const std::uint16_t port = nextPort();
    Player host;
    REQUIRE(host.session.host(port, 1));
    Player other;
    CHECK_FALSE(other.session.host(port));
    CHECK(other.session.state() == MultiplayerState::Offline);
    CHECK_FALSE(other.session.join("no such host.invalid", port));
    CHECK(other.session.state() == MultiplayerState::Offline);

    // A session that is full refuses a second client, who is told.
    Player first;
    Player second;
    REQUIRE(first.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &first}, [&] { return first.session.state() == MultiplayerState::Online; }));
    REQUIRE(second.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &first, &second}, [&] { return second.session.state() == MultiplayerState::Offline; }, std::chrono::seconds(15)));
    CHECK(second.saw(NetworkEvent::Kind::ConnectionFailed));
    CHECK(host.session.peers().size() == 1);

    // Nobody is listening there.
    Player lonely;
    REQUIRE(lonely.session.join("127.0.0.1", nextPort()));
    REQUIRE(until({&lonely}, [&] { return lonely.session.state() == MultiplayerState::Offline; }, std::chrono::seconds(20)));
    CHECK(lonely.saw(NetworkEvent::Kind::ConnectionFailed));

    // Sending without a session does nothing, and leaving twice neither.
    say(lonely, devex::net::everyone, "into the void");
    lonely.session.leave();
    lonely.session.leave();
}

namespace {

const devex::core::Uuid rpcTarget = devex::core::Uuid::fromParts(0x0102030405060708u, 0x090a0b0c0d0e0f10u);

[[nodiscard]] std::vector<std::byte> encoded(std::string name, std::vector<RpcValue> arguments = {})
{
    auto bytes = devex::net::encodeRpc({.target = rpcTarget, .name = std::move(name), .arguments = std::move(arguments)});
    REQUIRE(bytes.has_value());
    return *std::move(bytes);
}

// The calls of a player so far, by name.
[[nodiscard]] int callsOf(const Player& player, std::string_view name)
{
    return static_cast<int>(std::ranges::count_if(player.events, [&](const NetworkEvent& event) {
        const auto header = devex::net::decodeRpcHeader(event.bytes);
        return event.kind == NetworkEvent::Kind::Rpc && header && header->name == name;
    }));
}

} // namespace

TEST_CASE("A remote call reads back as it was written", "[net]")
{
    const std::vector<RpcValue> arguments{
        RpcValue(true),
        RpcValue(std::int64_t{-40000000000}),
        RpcValue(1.5),
        RpcValue(std::string("été")),
        RpcValue(devex::math::Vec2(1.0f, 2.0f)),
        RpcValue(devex::math::Vec3(3.0f, 4.0f, 5.0f)),
        RpcValue(devex::math::Vec4(6.0f, 7.0f, 8.0f, 9.0f)),
        RpcValue(devex::math::Quat(1.0f, 0.5f, 0.25f, 0.125f)),
        RpcValue(rpcTarget),
        RpcValue(std::vector<std::byte>{std::byte{1}, std::byte{255}}),
    };
    const std::vector<std::byte> bytes = encoded("Player.Hit", arguments);
    const auto call = devex::net::decodeRpc(bytes);
    REQUIRE(call.has_value());
    CHECK(call->target == rpcTarget);
    CHECK(call->name == "Player.Hit");
    CHECK(call->arguments == arguments);
    const auto header = devex::net::decodeRpcHeader(bytes);
    REQUIRE(header.has_value());
    CHECK(header->target == rpcTarget);
    CHECK(header->name == "Player.Hit");
}

TEST_CASE("A malformed remote call is refused, whatever is wrong with it", "[net]")
{
    const std::vector<std::byte> bytes = encoded("Player.Hit", {RpcValue(std::string("texte")), RpcValue(std::int64_t{3})});
    // Cut anywhere.
    for (std::size_t size = 0; size < bytes.size(); ++size)
    {
        CHECK_FALSE(devex::net::decodeRpc(std::span(bytes).first(size)).has_value());
    }
    // A length that goes past the end, and a type that does not exist.
    std::vector<std::byte> tooLong = bytes;
    const std::size_t arguments = 16 + 2 + std::string_view("Player.Hit").size() + 1;
    tooLong[arguments + 4] = std::byte{0xff};
    CHECK_FALSE(devex::net::decodeRpc(tooLong).has_value());
    std::vector<std::byte> strange = bytes;
    strange[arguments] = std::byte{200};
    CHECK_FALSE(devex::net::decodeRpc(strange).has_value());
    // A name of no length, and too many arguments, which do not encode either.
    CHECK_FALSE(devex::net::encodeRpc({.target = rpcTarget, .name = "", .arguments = {}}).has_value());
    CHECK_FALSE(devex::net::encodeRpc({.target = rpcTarget, .name = std::string(devex::net::maxRpcNameLength + 1, 'x'), .arguments = {}}).has_value());
    CHECK_FALSE(devex::net::encodeRpc({.target = rpcTarget, .name = "a", .arguments = std::vector<RpcValue>(devex::net::maxRpcArguments + 1, RpcValue(true))}).has_value());
    std::vector<std::byte> many = encoded("Player.Hit");
    many.back() = std::byte{255};
    CHECK_FALSE(devex::net::decodeRpc(many).has_value());
}

TEST_CASE("A value of a game becomes what a call carries, and back when it fits", "[net]")
{
    enum class Kind : std::uint8_t
    {
        First,
        Second,
    };
    using devex::net::fromRpcValue;
    using devex::net::toRpcValue;
    CHECK(std::get<std::int64_t>(toRpcValue(12)) == 12);
    CHECK(std::get<std::int64_t>(toRpcValue(Kind::Second)) == 1);
    CHECK(std::get<double>(toRpcValue(1.5f)) == 1.5);
    CHECK(std::get<std::string>(toRpcValue("texte")) == "texte");
    CHECK(std::get<std::string>(toRpcValue(std::string_view("vue"))) == "vue");
    CHECK(std::get<bool>(toRpcValue(true)));

    CHECK(fromRpcValue<int>(RpcValue(std::int64_t{12})) == 12);
    CHECK(fromRpcValue<Kind>(RpcValue(std::int64_t{1})) == Kind::Second);
    CHECK(fromRpcValue<float>(RpcValue(std::int64_t{3})) == 3.0f);
    CHECK(fromRpcValue<float>(RpcValue(1.5)) == 1.5f);
    // It does not fit, or is another kind of value.
    CHECK_FALSE(fromRpcValue<int>(RpcValue(std::int64_t{1} << 40)).has_value());
    CHECK_FALSE(fromRpcValue<std::uint8_t>(RpcValue(std::int64_t{-1})).has_value());
    CHECK_FALSE(fromRpcValue<int>(RpcValue(1.5)).has_value());
    CHECK_FALSE(fromRpcValue<std::string>(RpcValue(std::int64_t{1})).has_value());
    CHECK_FALSE(fromRpcValue<bool>(RpcValue(std::int64_t{1})).has_value());
}

TEST_CASE("Only the authority of an entity calls what is not open to all", "[net]")
{
    const std::uint16_t port = nextPort();
    Player host;
    Player client;
    Player other;
    for (Player* player : {&host, &client, &other})
    {
        player->session.declareRpc("Door.Open", {});
        player->session.declareRpc("Door.Knock", {.anyPeer = true});
    }
    REQUIRE(host.session.host(port));
    REQUIRE(client.session.join("127.0.0.1", port));
    REQUIRE(other.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &client, &other}, [&] {
        return host.session.peers().size() == 2 && client.session.peers().size() == 2 && other.session.peers().size() == 2;
    }));
    const PeerId clientPeer = client.session.localPeer();

    // The host is the authority by default, and the client's own game refuses to send.
    CHECK(host.session.isAuthority(rpcTarget));
    CHECK_FALSE(client.session.isAuthority(rpcTarget));
    CHECK(client.session.authority(rpcTarget) == devex::net::hostPeer);
    CHECK_FALSE(client.session.sendRpc(devex::net::everyone, encoded("Door.Open")));
    CHECK_FALSE(host.session.sendRpc(devex::net::everyone, encoded("Door.Nothing")));

    CHECK_FALSE(host.session.sendRpc(devex::net::everyone, encoded("Door.Open")));
    REQUIRE(until({&host, &client, &other}, [&] { return callsOf(client, "Door.Open") == 1 && callsOf(other, "Door.Open") == 1; }));
    CHECK(callsOf(host, "Door.Open") == 0);

    // Any player: the others hear it through the host, which vouches for who called.
    CHECK_FALSE(client.session.sendRpc(devex::net::everyone, encoded("Door.Knock")));
    REQUIRE(until({&host, &client, &other}, [&] { return callsOf(host, "Door.Knock") == 1 && callsOf(other, "Door.Knock") == 1; }));
    CHECK(callsOf(client, "Door.Knock") == 0);
    const auto fromClient = [&](const NetworkEvent& event) { return event.kind == NetworkEvent::Kind::Rpc && event.peer == clientPeer; };
    CHECK(std::ranges::count_if(other.events, fromClient) == 1);

    // A game that says otherwise is a cheat, or another version: the host passes on nothing. What comes
    // after it shows that the first was refused rather than late.
    client.session.declareRpc("Door.Open", {.anyPeer = true});
    host.events.clear();
    other.events.clear();
    CHECK_FALSE(client.session.sendRpc(devex::net::everyone, encoded("Door.Open")));
    CHECK_FALSE(client.session.sendRpc(devex::net::everyone, encoded("Door.Knock")));
    REQUIRE(until({&host, &client, &other}, [&] { return callsOf(other, "Door.Knock") == 1; }));
    CHECK(callsOf(host, "Door.Open") == 0);
    CHECK(callsOf(other, "Door.Open") == 0);

    // A client does not give it, even to itself.
    client.session.setAuthority(rpcTarget, clientPeer);
    CHECK_FALSE(client.session.isAuthority(rpcTarget));

    // Given by the host, every game follows: the client calls, and the host no longer does.
    host.session.setAuthority(rpcTarget, clientPeer);
    REQUIRE(until({&host, &client, &other}, [&] { return client.session.isAuthority(rpcTarget) && other.session.authority(rpcTarget) == clientPeer; }));
    CHECK_FALSE(host.session.isAuthority(rpcTarget));
    CHECK_FALSE(client.session.sendRpc(devex::net::everyone, encoded("Door.Open")));
    REQUIRE(until({&host, &client, &other}, [&] { return callsOf(other, "Door.Open") == 1; }));
    CHECK_FALSE(host.session.sendRpc(devex::net::everyone, encoded("Door.Open")));

    // Who joins later knows as well.
    Player late;
    late.session.declareRpc("Door.Open", {});
    REQUIRE(late.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &client, &other, &late}, [&] { return late.session.authority(rpcTarget) == clientPeer; }));

    // A session forgets it when it ends, for the one that left.
    client.session.leave();
    CHECK(client.session.isAuthority(rpcTarget));
    CHECK(client.session.authority(rpcTarget) == devex::net::hostPeer);

    // And for the others, it goes back to the host with the player that left.
    REQUIRE(until({&host, &other, &late}, [&] { return other.session.authority(rpcTarget) == devex::net::hostPeer && late.session.authority(rpcTarget) == devex::net::hostPeer; }));
    CHECK(host.session.isAuthority(rpcTarget));
}

TEST_CASE("A remote call says where it runs, to one player or on this game", "[net]")
{
    const std::uint16_t port = nextPort();
    Player host;
    Player client;
    Player other;
    for (Player* player : {&host, &client, &other})
    {
        player->session.declareRpc("Door.Open", {.anyPeer = true, .callLocal = true});
        player->session.declareRpc("Door.Knock", {.anyPeer = true, .reliable = false});
    }

    // Alone, a game is its own host, and only what says so runs.
    CHECK(host.session.isAuthority(rpcTarget));
    CHECK(host.session.sendRpc(devex::net::everyone, encoded("Door.Open")));
    CHECK_FALSE(host.session.sendRpc(devex::net::everyone, encoded("Door.Knock")));
    host.session.loopbackRpc(encoded("Door.Open"));
    host.session.poll();
    REQUIRE(host.session.events().size() == 1);
    CHECK(host.session.events()[0].peer == devex::net::hostPeer);
    host.session.poll();
    CHECK(host.session.events().empty());

    REQUIRE(host.session.host(port));
    REQUIRE(client.session.join("127.0.0.1", port));
    REQUIRE(other.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &client, &other}, [&] { return client.session.peers().size() == 2 && other.session.peers().size() == 2; }));

    // To one player: the other does not hear it. To itself: it only runs here.
    CHECK_FALSE(client.session.sendRpc(other.session.localPeer(), encoded("Door.Knock")));
    CHECK(client.session.sendRpc(devex::net::everyone, encoded("Door.Open")));
    CHECK(host.session.sendRpc(host.session.localPeer(), encoded("Door.Open")));
    CHECK_FALSE(client.session.sendRpc(client.session.localPeer(), encoded("Door.Knock")));
    REQUIRE(until({&host, &client, &other}, [&] {
        return callsOf(other, "Door.Knock") == 1 && callsOf(other, "Door.Open") == 1 && callsOf(host, "Door.Open") == 1;
    }));
    CHECK(callsOf(host, "Door.Knock") == 0);
    CHECK(callsOf(client, "Door.Open") == 0);

    // Who called the method that is running is for what runs it to say.
    CHECK(host.session.remoteSender() == 0);
    host.session.setRemoteSender(7);
    CHECK(host.session.remoteSender() == 7);
    CHECK(client.session.caller() == client.session.localPeer());
    CHECK(host.session.caller() == devex::net::hostPeer);
}

TEST_CASE("A paused game keeps its place in the session, with what arrived meanwhile", "[net]")
{
    const std::uint16_t port = nextPort();
    Player host;
    Player client;
    REQUIRE(host.session.host(port));
    REQUIRE(client.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &client}, [&] { return client.session.state() == MultiplayerState::Online && host.session.peers().size() == 1; }));

    // Longer than a silent player is waited for, the host only keeping alive.
    say(client, devex::net::hostPeer, "pendant la pause");
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(11);
    while (std::chrono::steady_clock::now() < end)
    {
        host.session.keepAlive();
        client.session.poll();
        client.session.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(client.session.state() == MultiplayerState::Online);
    CHECK(host.session.peers().size() == 1);

    // Back to playing, it finds what came.
    host.session.poll();
    const auto events = host.session.events();
    host.events.assign(events.begin(), events.end());
    CHECK(host.lastMessage(client.session.localPeer()) == "pendant la pause");
    // And not twice.
    host.session.poll();
    CHECK(host.session.events().empty());
}

TEST_CASE("The instances of Play stay as they are when their game connects", "[net]")
{
    const std::uint16_t port = devex::net::findFreePort();
    REQUIRE(port != 0);
    Player host;
    Player client;
    // A game that is not one of them does not get this: it is told it cannot.
    REQUIRE(host.session.host(port));
    CHECK_FALSE(host.session.host(port));
    CHECK_FALSE(host.session.join("127.0.0.1", port));

    host.session.keepTestSession();
    host.session.setTestInstance(1);
    client.session.keepTestSession();
    client.session.setTestInstance(2);
    CHECK(host.session.testInstance() == 1);
    REQUIRE(client.session.join("127.0.0.1", port));
    REQUIRE(until({&host, &client}, [&] { return client.session.state() == MultiplayerState::Online; }));
    CHECK(host.session.host(port + 1).has_value());
    CHECK(host.session.join("127.0.0.1", port + 1).has_value());
    CHECK(client.session.host(port + 1).has_value());
    CHECK(client.session.join("127.0.0.1", port + 1).has_value());
    CHECK(host.session.isHost());
    CHECK_FALSE(client.session.isHost());

    // After leaving, it connects as any game.
    client.session.leave();
    CHECK(client.session.host(devex::net::findFreePort()).has_value());
    CHECK(client.session.isHost());
}
