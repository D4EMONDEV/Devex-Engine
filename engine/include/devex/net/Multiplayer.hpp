#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/net/Rpc.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
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
        // A remote call this game accepted: its bytes are what decodeRpc reads.
        Rpc,
        // For the replication of the runtime, which says what its bytes mean.
        Replication,
    };

    Kind kind = Kind::Message;
    // Who joined, left, or sent the message: the host vouches for it.
    PeerId peer = 0;
    std::vector<std::byte> bytes;
    // How a message or a call came.
    Delivery delivery = Delivery::Reliable;
};

// The network of a game, as a client-server session: a player hosts, and the others join it by its
// address. They only reach each other through the host, which passes their messages on. Nothing here
// runs by itself: the application polls it once a frame, on the thread of the game, and the events it
// finds are what game code reads until the next poll.
// A port of this machine that nothing listens on at the moment, 0 when there is none.
[[nodiscard]] DEVEX_API std::uint16_t findFreePort();

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
    // While the game is paused: answers the others so that they do not take it for gone, and passes on
    // what is for them, keeping the events for the next poll.
    void keepAlive();
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

    // Which methods can be called over the network, by either language of the game. A call for any
    // other is refused when it arrives, as is one its policy does not allow.
    void declareRpc(std::string name, const RpcPolicy& policy);

    // Sends what encodeRpc made to one player or to everyone, the policy of the method allowing it.
    // True when the method also runs on this game: the caller runs it, then, or gives it to loopbackRpc
    // when its language does not have it. False when refused, with the reason in the log.
    [[nodiscard]] bool sendRpc(PeerId to, std::span<const std::byte> call);
    // Gives a call to this game with the next poll.
    void loopbackRpc(std::span<const std::byte> call);

    // To one player, or to everyone, for the replication of the runtime. A client only reaches the host,
    // which decides what to pass on: it knows what its bytes mean.
    void sendReplication(PeerId to, std::span<const std::byte> bytes, Delivery delivery);

    // The player that decides for an entity, and alone may call its methods unless they say anyPeer: the
    // host, unless said otherwise. The host gives it, and every player follows, those who come later
    // included; a client cannot give it, which is refused with the reason in the log. An entity goes back
    // to the host when its player leaves, and the session forgets them when it ends.
    void setAuthority(const core::Uuid& entity, PeerId peer);
    [[nodiscard]] PeerId authority(const core::Uuid& entity) const noexcept;
    // Whether this game is that player; always, outside a session, where it is on its own.
    [[nodiscard]] bool isAuthority(const core::Uuid& entity) const noexcept;

    // Who called the method that is running, set by what runs it; 0 outside of one.
    [[nodiscard]] PeerId remoteSender() const noexcept;
    void setRemoteSender(PeerId peer) noexcept;
    // The number this game calls with: its own, or the host's when it plays alone.
    [[nodiscard]] PeerId caller() const noexcept;

    // Among the instances Play starts together in the editor, which one this is, from 1; 0 otherwise.
    void setTestInstance(std::uint32_t instance) noexcept;
    [[nodiscard]] std::uint32_t testInstance() const noexcept;
    // From now on, a game that hosts or joins while it is in a session is told it did, without anything
    // changing: Play connected these instances, and the code of the game can stay as it is.
    void keepTestSession() noexcept;

private:
    void service();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace devex::net
