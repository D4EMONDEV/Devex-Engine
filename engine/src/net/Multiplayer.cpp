#include <devex/net/Multiplayer.hpp>

#include <devex/core/Log.hpp>

#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace devex::net {

namespace {

// Refuses players of another version of this protocol, given when they connect.
constexpr std::uint32_t protocolVersion = 1;
constexpr std::size_t reliableChannel = 0;
constexpr std::size_t unreliableChannel = 1;
constexpr std::size_t channelCount = 2;
// How long a silent player is waited for, in milliseconds.
constexpr enet_uint32 timeoutMinimum = 5000;
constexpr enet_uint32 timeoutMaximum = 10000;

// The first byte of every packet.
enum class Packet : std::uint8_t
{
    // Host to a new client: its number, then those of the others.
    Welcome,
    PeerJoined,
    PeerLeft,
    // The number of the sender, which the host fills in, of the receiver, then the message.
    Data,
};

constexpr std::size_t dataHeader = 1 + 2 * sizeof(PeerId);

void writeNumber(std::vector<std::byte>& bytes, std::uint32_t value)
{
    const std::array<std::byte, 4> little{static_cast<std::byte>(value), static_cast<std::byte>(value >> 8), static_cast<std::byte>(value >> 16),
                                          static_cast<std::byte>(value >> 24)};
    bytes.insert(bytes.end(), little.begin(), little.end());
}

[[nodiscard]] std::uint32_t readNumber(const std::byte* bytes)
{
    return std::to_integer<std::uint32_t>(bytes[0]) | (std::to_integer<std::uint32_t>(bytes[1]) << 8) | (std::to_integer<std::uint32_t>(bytes[2]) << 16) |
           (std::to_integer<std::uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] core::Result<void> initializeEnet()
{
    static std::once_flag once;
    static int result = 0;
    std::call_once(once, [] {
        result = enet_initialize();
        if (result == 0)
        {
            std::atexit(enet_deinitialize);
        }
    });
    if (result != 0)
    {
        return core::makeError(core::ErrorCode::Platform, "The network could not be started");
    }
    return {};
}

} // namespace

struct Multiplayer::Impl
{
    struct Client
    {
        ENetPeer* connection = nullptr;
        PeerId id = 0;
    };

    ENetHost* enet = nullptr;
    MultiplayerState state = MultiplayerState::Offline;
    bool hosting = false;
    PeerId local = 0;
    PeerId nextPeer = hostPeer + 1;
    // The host's connection, on a client.
    ENetPeer* server = nullptr;
    // The clients and their connections, on the host.
    std::vector<Client> clients;
    std::vector<PeerId> peers;
    std::uint32_t maxClients = 0;
    std::vector<NetworkEvent> events;

    void close()
    {
        if (enet != nullptr)
        {
            enet_host_destroy(enet);
        }
        enet = nullptr;
        server = nullptr;
        clients.clear();
        peers.clear();
        state = MultiplayerState::Offline;
        hosting = false;
        local = 0;
    }

    void sendTo(ENetPeer* connection, std::span<const std::byte> bytes, Delivery delivery)
    {
        const bool reliable = delivery == Delivery::Reliable;
        ENetPacket* const packet = enet_packet_create(bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        if (packet != nullptr && enet_peer_send(connection, static_cast<enet_uint8>(reliable ? reliableChannel : unreliableChannel), packet) < 0)
        {
            enet_packet_destroy(packet);
        }
    }

    void tellClients(Packet kind, PeerId peer, PeerId except)
    {
        std::vector<std::byte> bytes{static_cast<std::byte>(kind)};
        writeNumber(bytes, peer);
        for (const Client& client : clients)
        {
            if (client.id != except)
            {
                sendTo(client.connection, bytes, Delivery::Reliable);
            }
        }
    }

    void clientConnected(ENetEvent& event)
    {
        if (event.data != protocolVersion || clients.size() >= maxClients)
        {
            enet_peer_disconnect_now(event.peer, 0);
            return;
        }
        const PeerId id = nextPeer++;
        std::vector<std::byte> welcome{static_cast<std::byte>(Packet::Welcome)};
        writeNumber(welcome, id);
        for (const PeerId other : peers)
        {
            writeNumber(welcome, other);
        }
        enet_peer_timeout(event.peer, 0, timeoutMinimum, timeoutMaximum);
        sendTo(event.peer, welcome, Delivery::Reliable);
        tellClients(Packet::PeerJoined, id, id);
        clients.push_back({.connection = event.peer, .id = id});
        peers.push_back(id);
        events.push_back({.kind = NetworkEvent::Kind::PeerJoined, .peer = id});
    }

    void clientLeft(const ENetEvent& event)
    {
        const auto found = std::ranges::find(clients, event.peer, &Client::connection);
        if (found == clients.end())
        {
            return;
        }
        const PeerId id = found->id;
        clients.erase(found);
        std::erase(peers, id);
        tellClients(Packet::PeerLeft, id, id);
        events.push_back({.kind = NetworkEvent::Kind::PeerLeft, .peer = id});
    }

    // A message from a client: for the host, for another client, or for everyone. The host says who sent
    // it, whatever the packet claims.
    void relay(const ENetEvent& event, std::span<const std::byte> bytes, Delivery delivery)
    {
        const auto sender = std::ranges::find(clients, event.peer, &Client::connection);
        if (sender == clients.end() || bytes.size() < dataHeader)
        {
            return;
        }
        const PeerId from = sender->id;
        const PeerId to = readNumber(bytes.data() + 1 + sizeof(PeerId));
        const std::span<const std::byte> message = bytes.subspan(dataHeader);
        if (to == hostPeer || to == everyone)
        {
            events.push_back({.kind = NetworkEvent::Kind::Message, .peer = from, .bytes = {message.begin(), message.end()}});
        }
        if (to == hostPeer)
        {
            return;
        }
        std::vector<std::byte> passed{static_cast<std::byte>(Packet::Data)};
        writeNumber(passed, from);
        writeNumber(passed, to);
        passed.insert(passed.end(), message.begin(), message.end());
        for (const Client& client : clients)
        {
            if (client.id != from && (to == everyone || client.id == to))
            {
                sendTo(client.connection, passed, delivery);
            }
        }
    }

    void received(std::span<const std::byte> bytes)
    {
        if (bytes.empty())
        {
            return;
        }
        const auto kind = static_cast<Packet>(std::to_integer<std::uint8_t>(bytes[0]));
        if (kind == Packet::Welcome && bytes.size() >= 1 + sizeof(PeerId))
        {
            local = readNumber(bytes.data() + 1);
            peers.assign(1, hostPeer);
            for (std::size_t at = 1 + sizeof(PeerId); at + sizeof(PeerId) <= bytes.size(); at += sizeof(PeerId))
            {
                peers.push_back(readNumber(bytes.data() + at));
            }
            state = MultiplayerState::Online;
            events.push_back({.kind = NetworkEvent::Kind::Connected, .peer = local});
        }
        else if ((kind == Packet::PeerJoined || kind == Packet::PeerLeft) && bytes.size() >= 1 + sizeof(PeerId))
        {
            const PeerId peer = readNumber(bytes.data() + 1);
            if (kind == Packet::PeerJoined)
            {
                peers.push_back(peer);
            }
            else
            {
                std::erase(peers, peer);
            }
            events.push_back({.kind = kind == Packet::PeerJoined ? NetworkEvent::Kind::PeerJoined : NetworkEvent::Kind::PeerLeft, .peer = peer});
        }
        else if (kind == Packet::Data && bytes.size() >= dataHeader)
        {
            const std::span<const std::byte> message = bytes.subspan(dataHeader);
            events.push_back({.kind = NetworkEvent::Kind::Message, .peer = readNumber(bytes.data() + 1), .bytes = {message.begin(), message.end()}});
        }
    }
};

Multiplayer::Multiplayer()
    : m_impl(std::make_unique<Impl>())
{
}

Multiplayer::~Multiplayer()
{
    leave();
}

core::Result<void> Multiplayer::host(std::uint16_t port, std::uint32_t maxClients)
{
    if (m_impl->state != MultiplayerState::Offline)
    {
        return core::makeError(core::ErrorCode::InvalidState, "This game is already in a session");
    }
    if (core::Result<void> started = initializeEnet(); !started)
    {
        return started;
    }
    const ENetAddress address{.host = ENET_HOST_ANY, .port = port};
    // One connection more than there are places: a client too many is told, rather than left waiting.
    m_impl->enet = enet_host_create(&address, maxClients + 1, channelCount, 0, 0);
    if (m_impl->enet == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "Cannot host on port {}: it may be taken", port);
    }
    m_impl->hosting = true;
    m_impl->state = MultiplayerState::Online;
    m_impl->local = hostPeer;
    m_impl->nextPeer = hostPeer + 1;
    m_impl->maxClients = maxClients;
    return {};
}

core::Result<void> Multiplayer::join(std::string_view address, std::uint16_t port)
{
    if (m_impl->state != MultiplayerState::Offline)
    {
        return core::makeError(core::ErrorCode::InvalidState, "This game is already in a session");
    }
    if (core::Result<void> started = initializeEnet(); !started)
    {
        return started;
    }
    ENetAddress target{.host = ENET_HOST_ANY, .port = port};
    if (enet_address_set_host(&target, std::string(address).c_str()) != 0)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "Cannot find the address {}", address);
    }
    m_impl->enet = enet_host_create(nullptr, 1, channelCount, 0, 0);
    if (m_impl->enet == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "Cannot open a connection");
    }
    m_impl->server = enet_host_connect(m_impl->enet, &target, channelCount, protocolVersion);
    if (m_impl->server == nullptr)
    {
        m_impl->close();
        return core::makeError(core::ErrorCode::Platform, "Cannot start connecting to {}", address);
    }
    enet_peer_timeout(m_impl->server, 0, timeoutMinimum, timeoutMaximum);
    m_impl->state = MultiplayerState::Connecting;
    return {};
}

void Multiplayer::leave()
{
    if (m_impl->enet == nullptr)
    {
        return;
    }
    // At once rather than waiting for an answer: the others find out from the packet this sends.
    if (m_impl->server != nullptr)
    {
        enet_peer_disconnect_now(m_impl->server, 0);
    }
    for (const Impl::Client& client : m_impl->clients)
    {
        enet_peer_disconnect_now(client.connection, 0);
    }
    enet_host_flush(m_impl->enet);
    m_impl->close();
}

void Multiplayer::poll()
{
    Impl& impl = *m_impl;
    impl.events.clear();
    if (impl.enet == nullptr)
    {
        return;
    }
    ENetEvent event;
    while (impl.enet != nullptr && enet_host_service(impl.enet, &event, 0) > 0)
    {
        switch (event.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
            if (impl.hosting)
            {
                impl.clientConnected(event);
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(event.packet->data), event.packet->dataLength);
            if (impl.hosting)
            {
                if (!bytes.empty() && static_cast<Packet>(std::to_integer<std::uint8_t>(bytes[0])) == Packet::Data)
                {
                    impl.relay(event, bytes, event.channelID == unreliableChannel ? Delivery::Unreliable : Delivery::Reliable);
                }
            }
            else
            {
                impl.received(bytes);
            }
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            if (impl.hosting)
            {
                impl.clientLeft(event);
            }
            else
            {
                const bool online = impl.state == MultiplayerState::Online;
                impl.close();
                impl.events.push_back({.kind = online ? NetworkEvent::Kind::Disconnected : NetworkEvent::Kind::ConnectionFailed});
            }
            break;
        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
    flush();
}

void Multiplayer::flush()
{
    if (m_impl->enet != nullptr)
    {
        enet_host_flush(m_impl->enet);
    }
}

std::span<const NetworkEvent> Multiplayer::events() const noexcept
{
    return m_impl->events;
}

void Multiplayer::send(PeerId to, std::span<const std::byte> bytes, Delivery delivery)
{
    Impl& impl = *m_impl;
    if (impl.state != MultiplayerState::Online || to == impl.local)
    {
        return;
    }
    std::vector<std::byte> packet{static_cast<std::byte>(Packet::Data)};
    packet.reserve(dataHeader + bytes.size());
    writeNumber(packet, impl.local);
    writeNumber(packet, to);
    packet.insert(packet.end(), bytes.begin(), bytes.end());
    if (!impl.hosting)
    {
        impl.sendTo(impl.server, packet, delivery);
        return;
    }
    for (const Impl::Client& client : impl.clients)
    {
        if (to == everyone || client.id == to)
        {
            impl.sendTo(client.connection, packet, delivery);
        }
    }
}

MultiplayerState Multiplayer::state() const noexcept
{
    return m_impl->state;
}

bool Multiplayer::isHost() const noexcept
{
    return m_impl->hosting;
}

PeerId Multiplayer::localPeer() const noexcept
{
    return m_impl->local;
}

std::span<const PeerId> Multiplayer::peers() const noexcept
{
    return m_impl->peers;
}

} // namespace devex::net
