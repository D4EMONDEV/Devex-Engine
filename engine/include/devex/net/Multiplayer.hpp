#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace devex::net {

// A player of a session, as Godot numbers them: the host is 1, and it gives the others their numbers.
using PeerId = std::uint32_t;
inline constexpr PeerId hostPeer = 1;
// As the destination of a message: every other player of the session.
inline constexpr PeerId everyone = 0;

enum class Delivery : std::uint8_t
{
    // Arrives, in order. For what must not be lost.
    Reliable,
    // May be lost, and is overtaken by nothing: what comes late is dropped. For what the next message
    // replaces, such as a position.
    Unreliable,
};

enum class MultiplayerState : std::uint8_t
{
    Offline,
    // Waiting for the host to answer.
    Connecting,
    Online,
};

struct DEVEX_API NetworkEvent
{
    enum class Kind : std::uint8_t
    {
        // This player joined the session; localPeer and peers are known from now on.
        Connected,
        // The host could not be reached, or refused this player.
        ConnectionFailed,
        // The session ended for this player: the host left, or stopped answering.
        Disconnected,
        PeerJoined,
        PeerLeft,
        Message,
    };

    Kind kind = Kind::Message;
    // Who joined, left, or sent the message: the host vouches for it.
    PeerId peer = 0;
    std::vector<std::byte> bytes;
};

// The network of a game, as a client-server session: a player hosts, and the others join it by its
// address. They only reach each other through the host, which passes their messages on. Nothing here
// runs by itself: the application polls it once a frame, on the thread of the game, and the events it
// finds are what game code reads until the next poll.
class DEVEX_API Multiplayer
{
public:
    Multiplayer();
    ~Multiplayer();

    Multiplayer(const Multiplayer&) = delete;
    Multiplayer& operator=(const Multiplayer&) = delete;

    // Starts a session on a port of this machine, as its host. Fails when a session is under way or
    // when the port is taken.
    [[nodiscard]] core::Result<void> host(std::uint16_t port, std::uint32_t maxClients = 16);
    // Starts joining a session: Connected or ConnectionFailed tells how it went.
    [[nodiscard]] core::Result<void> join(std::string_view address, std::uint16_t port);
    // Ends the session for this player, telling the others. A host leaving ends it for everyone.
    void leave();

    // Reads what arrived, passing on the messages between clients when hosting, and replaces the events.
    void poll();
    // Sends what waits, which otherwise leaves with the next poll.
    void flush();
    [[nodiscard]] std::span<const NetworkEvent> events() const noexcept;

    // To one player, to everyone, or to the host. A player does not receive its own messages.
    void send(PeerId to, std::span<const std::byte> bytes, Delivery delivery);

    [[nodiscard]] MultiplayerState state() const noexcept;
    [[nodiscard]] bool isHost() const noexcept;
    // This player's number, 0 until it is online.
    [[nodiscard]] PeerId localPeer() const noexcept;
    // The other players of the session.
    [[nodiscard]] std::span<const PeerId> peers() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace devex::net
