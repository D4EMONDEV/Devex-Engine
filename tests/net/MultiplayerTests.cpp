#include <devex/net/Multiplayer.hpp>

#include <catch2/catch_test_macros.hpp>

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

namespace {

// Each test on its own port, so that the sockets of the one before cannot answer.
std::uint16_t nextPort()
{
    static std::uint16_t port = 47310;
    return port++;
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
