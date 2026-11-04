#include <devex/net/Multiplayer.hpp>

#include <devex/core/Log.hpp>
#include <devex/net/Bytes.hpp>

#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace devex::net {

namespace {

// Refuses players of another version of this protocol, given when they connect.
constexpr std::uint32_t protocolVersion = 3;
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
    // Likewise, with a remote call for a message.
    Rpc,
    // Host to the clients: an entity, then the player that now decides for it.
    Authority,
    // Likewise as Data, for the replication of the runtime.
    Replication,
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

[[nodiscard]] constexpr NetworkEvent::Kind eventOf(Packet kind) noexcept
{
    return kind == Packet::Rpc           ? NetworkEvent::Kind::Rpc
           : kind == Packet::Replication ? NetworkEvent::Kind::Replication
                                         : NetworkEvent::Kind::Message;
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
    std::map<std::string, RpcPolicy, std::less<>> rpcs;
    std::unordered_map<core::Uuid, PeerId> authorities;
    std::vector<std::vector<std::byte>> loopback;
    PeerId remoteSender = 0;
    std::uint32_t testInstance = 0;
    bool keepTestSession = false;
    // The events keepAlive kept for the next poll.
    bool keptEvents = false;

    [[nodiscard]] const RpcPolicy* policyOf(std::string_view name) const noexcept
    {
        const auto found = rpcs.find(name);
        return found != rpcs.end() ? &found->second : nullptr;
    }

    [[nodiscard]] PeerId authorityOf(const core::Uuid& entity) const noexcept
    {
        const auto found = authorities.find(entity);
        return found != authorities.end() ? found->second : hostPeer;
    }

    // The host is who decides unless said otherwise, so that it need not be kept.
    void remember(const core::Uuid& entity, PeerId peer)
    {
        if (peer == hostPeer)
        {
            authorities.erase(entity);
        }
        else
        {
            authorities.insert_or_assign(entity, peer);
        }
    }

    void forgetAuthorityOf(PeerId peer)
    {
        std::erase_if(authorities, [peer](const auto& entry) { return entry.second == peer; });
    }

    [[nodiscard]] static std::vector<std::byte> authorityPacket(const core::Uuid& entity, PeerId peer)
    {
        std::vector<std::byte> packet{static_cast<std::byte>(Packet::Authority)};
        ByteWriter writer(packet);
        writer.uuid(entity);
        writer.number(peer, sizeof(PeerId));
        return packet;
    }

    // Whether a call that arrived may run, or be passed on. The sender has already refused what it knows
    // it may not call, so that one arriving here is another version of the game, or a cheat.
    [[nodiscard]] bool accepts(std::span<const std::byte> call, PeerId from) const
    {
        const std::optional<RpcHeader> header = decodeRpcHeader(call);
        if (!header)
        {
            DEVEX_LOG_WARNING("Refused a malformed remote call from player {}", from);
            return false;
        }
        const RpcPolicy* const policy = policyOf(header->name);
        if (policy == nullptr)
        {
            DEVEX_LOG_WARNING("Refused the remote call {} from player {}: this game has no such method", header->name, from);
            return false;
        }
        if (!policy->anyPeer && from != authorityOf(header->target))
        {
            DEVEX_LOG_WARNING("Refused the remote call {} from player {}: only the authority of the entity, player {}, may call it", header->name, from,
                           authorityOf(header->target));
            return false;
        }
        return true;
    }

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
        authorities.clear();
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
        for (const auto& [entity, peer] : authorities)
        {
            sendTo(event.peer, authorityPacket(entity, peer), Delivery::Reliable);
        }
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
        forgetAuthorityOf(id);
        tellClients(Packet::PeerLeft, id, id);
        events.push_back({.kind = NetworkEvent::Kind::PeerLeft, .peer = id});
    }

    // A message or a call from a client: for the host, for another client, or for everyone. The host says
    // who sent it, whatever the packet claims, and passes on no call it refuses.
    void relay(const ENetEvent& event, Packet kind, std::span<const std::byte> bytes, Delivery delivery)
    {
        const auto sender = std::ranges::find(clients, event.peer, &Client::connection);
        if (sender == clients.end() || bytes.size() < dataHeader)
        {
            return;
        }
        const PeerId from = sender->id;
        const PeerId to = readNumber(bytes.data() + 1 + sizeof(PeerId));
        const std::span<const std::byte> message = bytes.subspan(dataHeader);
        if (kind == Packet::Rpc && !accepts(message, from))
        {
            return;
        }
        if (kind == Packet::Replication)
        {
            events.push_back({.kind = eventOf(kind), .peer = from, .bytes = {message.begin(), message.end()}, .delivery = delivery});
            return;
        }
        if (to == hostPeer || to == everyone)
        {
            events.push_back({.kind = eventOf(kind), .peer = from, .bytes = {message.begin(), message.end()}, .delivery = delivery});
        }
        if (to == hostPeer)
        {
            return;
        }
        std::vector<std::byte> passed{static_cast<std::byte>(kind)};
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

    // Sends a packet of the session to one player, to everyone, or to the host.
    void sendPacket(Packet kind, PeerId to, std::span<const std::byte> bytes, Delivery delivery)
    {
        std::vector<std::byte> packet{static_cast<std::byte>(kind)};
        packet.reserve(dataHeader + bytes.size());
        writeNumber(packet, local);
        writeNumber(packet, to);
        packet.insert(packet.end(), bytes.begin(), bytes.end());
        if (!hosting)
        {
            sendTo(server, packet, delivery);
            return;
        }
        for (const Client& client : clients)
        {
            if (to == everyone || client.id == to)
            {
                sendTo(client.connection, packet, delivery);
            }
        }
    }

    void received(std::span<const std::byte> bytes, Delivery delivery)
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
            // The host decides from now on: it tells what it has decided so far next.
            authorities.clear();
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
                forgetAuthorityOf(peer);
            }
            events.push_back({.kind = kind == Packet::PeerJoined ? NetworkEvent::Kind::PeerJoined : NetworkEvent::Kind::PeerLeft, .peer = peer});
        }
        else if ((kind == Packet::Data || kind == Packet::Rpc || kind == Packet::Replication) && bytes.size() >= dataHeader)
        {
            const std::span<const std::byte> message = bytes.subspan(dataHeader);
            const PeerId from = readNumber(bytes.data() + 1);
            if (kind != Packet::Rpc || accepts(message, from))
            {
                events.push_back({.kind = eventOf(kind), .peer = from, .bytes = {message.begin(), message.end()}, .delivery = delivery});
            }
        }
        else if (kind == Packet::Authority)
        {
            ByteReader reader(bytes.subspan(1));
            const core::Uuid entity = reader.uuid();
            const auto peer = static_cast<PeerId>(reader.number(sizeof(PeerId)));
            if (!reader.failed())
            {
                remember(entity, peer);
            }
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
    if (m_impl->state != MultiplayerState::Offline && m_impl->keepTestSession)
    {
        return {};
    }
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
    if (m_impl->state != MultiplayerState::Offline && m_impl->keepTestSession)
    {
        return {};
    }
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
    if (!std::exchange(impl.keptEvents, false))
    {
        impl.events.clear();
    }
    for (std::vector<std::byte>& call : impl.loopback)
    {
        impl.events.push_back({.kind = NetworkEvent::Kind::Rpc, .peer = caller(), .bytes = std::move(call)});
    }
    impl.loopback.clear();
    service();
}

void Multiplayer::keepAlive()
{
    m_impl->keptEvents = true;
    service();
}

void Multiplayer::service()
{
    Impl& impl = *m_impl;
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
                const auto kind = bytes.empty() ? Packet::Welcome : static_cast<Packet>(std::to_integer<std::uint8_t>(bytes[0]));
                if (kind == Packet::Data || kind == Packet::Rpc || kind == Packet::Replication)
                {
                    impl.relay(event, kind, bytes, event.channelID == unreliableChannel ? Delivery::Unreliable : Delivery::Reliable);
                }
            }
            else
            {
                impl.received(bytes, event.channelID == unreliableChannel ? Delivery::Unreliable : Delivery::Reliable);
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
    impl.sendPacket(Packet::Data, to, bytes, delivery);
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

void Multiplayer::declareRpc(std::string name, const RpcPolicy& policy)
{
    m_impl->rpcs.insert_or_assign(std::move(name), policy);
}

bool Multiplayer::sendRpc(PeerId to, std::span<const std::byte> call)
{
    Impl& impl = *m_impl;
    const std::optional<RpcHeader> header = decodeRpcHeader(call);
    if (!header)
    {
        DEVEX_LOG_ERROR("Cannot send a remote call that says nothing of what it is for");
        return false;
    }
    const RpcPolicy* const policy = impl.policyOf(header->name);
    if (policy == nullptr)
    {
        DEVEX_LOG_ERROR("{} cannot be called over the network: it needs [Rpc] in C#, or GameRegistry::rpc in C++", header->name);
        return false;
    }
    if (!policy->anyPeer && !isAuthority(header->target))
    {
        DEVEX_LOG_ERROR("Player {} cannot call {}: only the authority of the entity, player {}, may. Say AnyPeer if every player may", impl.local, header->name,
                        impl.authorityOf(header->target));
        return false;
    }
    const PeerId self = caller();
    if (impl.state == MultiplayerState::Online && to != self)
    {
        impl.sendPacket(Packet::Rpc, to, call, policy->reliable ? Delivery::Reliable : Delivery::Unreliable);
    }
    if (to == self && !policy->callLocal)
    {
        DEVEX_LOG_WARNING("{} was called for this very player, and does not say CallLocal: it does not run", header->name);
    }
    return policy->callLocal && (to == everyone || to == self);
}

void Multiplayer::loopbackRpc(std::span<const std::byte> call)
{
    m_impl->loopback.emplace_back(call.begin(), call.end());
}

void Multiplayer::sendReplication(PeerId to, std::span<const std::byte> bytes, Delivery delivery)
{
    Impl& impl = *m_impl;
    if (impl.state == MultiplayerState::Online)
    {
        impl.sendPacket(Packet::Replication, impl.hosting ? to : hostPeer, bytes, delivery);
    }
}

void Multiplayer::setAuthority(const core::Uuid& entity, PeerId peer)
{
    Impl& impl = *m_impl;
    if (impl.state == MultiplayerState::Online && !impl.hosting)
    {
        DEVEX_LOG_ERROR("Player {} cannot give the authority of an entity: the host decides", impl.local);
        return;
    }
    impl.remember(entity, peer);
    for (const Impl::Client& client : impl.clients)
    {
        impl.sendTo(client.connection, Impl::authorityPacket(entity, peer), Delivery::Reliable);
    }
}

PeerId Multiplayer::authority(const core::Uuid& entity) const noexcept
{
    return m_impl->authorityOf(entity);
}

bool Multiplayer::isAuthority(const core::Uuid& entity) const noexcept
{
    return m_impl->state != MultiplayerState::Online || m_impl->authorityOf(entity) == m_impl->local;
}

PeerId Multiplayer::remoteSender() const noexcept
{
    return m_impl->remoteSender;
}

void Multiplayer::setRemoteSender(PeerId peer) noexcept
{
    m_impl->remoteSender = peer;
}

PeerId Multiplayer::caller() const noexcept
{
    return m_impl->local != 0 ? m_impl->local : hostPeer;
}

void Multiplayer::setTestInstance(std::uint32_t instance) noexcept
{
    m_impl->testInstance = instance;
}

std::uint32_t Multiplayer::testInstance() const noexcept
{
    return m_impl->testInstance;
}

void Multiplayer::keepTestSession() noexcept
{
    m_impl->keepTestSession = true;
}

std::uint16_t findFreePort()
{
    if (!initializeEnet())
    {
        return 0;
    }
    ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL)
    {
        return 0;
    }
    ENetAddress address{.host = ENET_HOST_ANY, .port = 0};
    const bool found = enet_socket_bind(socket, &address) == 0 && enet_socket_get_address(socket, &address) == 0;
    enet_socket_destroy(socket);
    return found ? address.port : std::uint16_t{0};
}

} // namespace devex::net
