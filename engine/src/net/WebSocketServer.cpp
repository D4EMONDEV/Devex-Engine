#include "WebSocketServer.hpp"

#include <devex/core/Base64.hpp>
#include <devex/core/Log.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <format>
#include <utility>

namespace devex::net::detail {

namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalidSocket = INVALID_SOCKET;
constexpr int sendFlags = 0;

void closeSocket(Socket socket)
{
    closesocket(socket);
}

[[nodiscard]] bool makeNonBlocking(Socket socket)
{
    u_long on = 1;
    return ioctlsocket(socket, FIONBIO, &on) == 0;
}

[[nodiscard]] bool wouldBlock()
{
    return WSAGetLastError() == WSAEWOULDBLOCK;
}
#else
using Socket = int;
constexpr Socket invalidSocket = -1;
// A client gone while being written to is an error to handle, not a signal that ends the program.
constexpr int sendFlags = MSG_NOSIGNAL;

void closeSocket(Socket socket)
{
    ::close(socket);
}

[[nodiscard]] bool makeNonBlocking(Socket socket)
{
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

[[nodiscard]] bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}
#endif

using Clock = std::chrono::steady_clock;

// What a handshake may take, and how often a client is pinged.
constexpr std::size_t handshakeLimit = 8 * 1024;
constexpr auto handshakeTime = std::chrono::seconds(10);
constexpr auto pingInterval = std::chrono::seconds(5);
// A message larger than this closes its client, as one whose writes pile up does.
constexpr std::size_t messageLimit = 16 * 1024 * 1024;
constexpr std::size_t outputLimit = 64 * 1024 * 1024;

enum Opcode : std::uint8_t
{
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

[[nodiscard]] std::array<std::uint8_t, 20> sha1(std::string_view text)
{
    std::array<std::uint32_t, 5> state{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<std::uint8_t> message(text.begin(), text.end());
    const std::uint64_t bits = static_cast<std::uint64_t>(message.size()) * 8u;
    message.push_back(0x80);
    while (message.size() % 64 != 56)
    {
        message.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64)
    {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16; ++index)
        {
            const std::size_t at = chunk + index * 4;
            words[index] = (static_cast<std::uint32_t>(message[at]) << 24) | (static_cast<std::uint32_t>(message[at + 1]) << 16) |
                           (static_cast<std::uint32_t>(message[at + 2]) << 8) | static_cast<std::uint32_t>(message[at + 3]);
        }
        for (std::size_t index = 16; index < 80; ++index)
        {
            words[index] = std::rotl(words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);
        }
        auto [a, b, c, d, e] = state;
        for (std::size_t index = 0; index < 80; ++index)
        {
            std::uint32_t mixed = 0;
            std::uint32_t constant = 0;
            if (index < 20)
            {
                mixed = (b & c) | (~b & d);
                constant = 0x5A827999u;
            }
            else if (index < 40)
            {
                mixed = b ^ c ^ d;
                constant = 0x6ED9EBA1u;
            }
            else if (index < 60)
            {
                mixed = (b & c) | (b & d) | (c & d);
                constant = 0x8F1BBCDCu;
            }
            else
            {
                mixed = b ^ c ^ d;
                constant = 0xCA62C1D6u;
            }
            const std::uint32_t next = std::rotl(a, 5) + mixed + e + constant + words[index];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = next;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
    std::array<std::uint8_t, 20> digest{};
    for (std::size_t index = 0; index < 20; ++index)
    {
        digest[index] = static_cast<std::uint8_t>(state[index / 4] >> (24 - 8 * (index % 4)));
    }
    return digest;
}

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return result;
}

[[nodiscard]] std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    {
        text.remove_suffix(1);
    }
    return text;
}

// The value of a header of a request, whose name is matched whatever its case.
[[nodiscard]] std::string_view headerOf(std::string_view request, std::string_view name)
{
    std::size_t start = request.find("\r\n");
    while (start != std::string_view::npos && start + 2 < request.size())
    {
        start += 2;
        const std::size_t end = request.find("\r\n", start);
        const std::string_view line = request.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos && lowered(trimmed(line.substr(0, colon))) == name)
        {
            return trimmed(line.substr(colon + 1));
        }
        start = end;
    }
    return {};
}

// Whether a list of a header, separated by commas, holds a token, whatever its case.
[[nodiscard]] bool listHolds(std::string_view list, std::string_view token)
{
    while (!list.empty())
    {
        const std::size_t comma = list.find(',');
        if (lowered(trimmed(list.substr(0, comma))) == lowered(token))
        {
            return true;
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        list.remove_prefix(comma + 1);
    }
    return false;
}

// A frame from the server, which is not masked.
void appendFrame(std::vector<std::byte>& output, Opcode opcode, std::span<const std::byte> payload)
{
    output.push_back(static_cast<std::byte>(0x80u | opcode));
    const std::size_t size = payload.size();
    if (size < 126)
    {
        output.push_back(static_cast<std::byte>(size));
    }
    else if (size <= 0xFFFF)
    {
        output.push_back(std::byte{126});
        output.push_back(static_cast<std::byte>(size >> 8));
        output.push_back(static_cast<std::byte>(size));
    }
    else
    {
        output.push_back(std::byte{127});
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            output.push_back(static_cast<std::byte>(static_cast<std::uint64_t>(size) >> shift));
        }
    }
    output.insert(output.end(), payload.begin(), payload.end());
}

} // namespace

std::string webSocketAccept(std::string_view key)
{
    const std::array<std::uint8_t, 20> digest = sha1(std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return core::encodeBase64(std::as_bytes(std::span(digest)));
}

struct WebSocketServer::Impl
{
    struct Connection
    {
        Socket socket = invalidSocket;
        std::uint32_t id = 0;
        // After the handshake.
        bool open = false;
        // Ends once what it has to write is written; quietly, when the session closed it.
        bool closing = false;
        bool quiet = false;
        std::vector<std::byte> input;
        std::vector<std::byte> output;
        // A message in several frames, until its last.
        std::vector<std::byte> message;
        Clock::time_point heard;
        Clock::time_point pinged;
    };

    Socket listener = invalidSocket;
    std::string protocol;
    std::vector<Connection> connections;
    std::uint32_t nextConnection = 1;

    ~Impl()
    {
        for (const Connection& connection : connections)
        {
            closeSocket(connection.socket);
        }
        if (listener != invalidSocket)
        {
            closeSocket(listener);
        }
    }

    void accept(Clock::time_point now)
    {
        for (;;)
        {
            const Socket socket = ::accept(listener, nullptr, nullptr);
            if (socket == invalidSocket)
            {
                return;
            }
            // Small messages go at once rather than waiting to be gathered.
            const int noDelay = 1;
            setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
            if (!makeNonBlocking(socket))
            {
                closeSocket(socket);
                continue;
            }
            connections.push_back({.socket = socket, .id = nextConnection++, .heard = now, .pinged = now});
        }
    }

    // Reads what arrived; false once the client is gone.
    [[nodiscard]] static bool read(Connection& connection)
    {
        std::array<char, 16384> buffer{};
        for (;;)
        {
            const auto received = recv(connection.socket, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received > 0)
            {
                const auto* const bytes = reinterpret_cast<const std::byte*>(buffer.data());
                connection.input.insert(connection.input.end(), bytes, bytes + received);
                continue;
            }
            return received < 0 && wouldBlock();
        }
    }

    // Writes what waits; false once the client is gone.
    [[nodiscard]] static bool write(Connection& connection)
    {
        std::size_t written = 0;
        while (written < connection.output.size())
        {
            const std::size_t left = connection.output.size() - written;
            const auto sent = ::send(connection.socket, reinterpret_cast<const char*>(connection.output.data() + written),
                                     static_cast<int>(std::min<std::size_t>(left, 1 << 20)), sendFlags);
            if (sent <= 0)
            {
                if (sent < 0 && wouldBlock())
                {
                    break;
                }
                return false;
            }
            written += static_cast<std::size_t>(sent);
        }
        connection.output.erase(connection.output.begin(), connection.output.begin() + static_cast<std::ptrdiff_t>(written));
        return connection.output.size() <= outputLimit;
    }

    void refuse(Connection& connection)
    {
        constexpr std::string_view answer = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        const auto* const bytes = reinterpret_cast<const std::byte*>(answer.data());
        connection.output.assign(bytes, bytes + answer.size());
        connection.closing = true;
        connection.quiet = true;
    }

    // The request of a client that has not finished its handshake: answered once it is whole.
    void handshake(Connection& connection, std::vector<Event>& events)
    {
        const std::string_view input(reinterpret_cast<const char*>(connection.input.data()), connection.input.size());
        const std::size_t end = input.find("\r\n\r\n");
        if (end == std::string_view::npos)
        {
            if (connection.input.size() > handshakeLimit)
            {
                refuse(connection);
            }
            return;
        }
        const std::string_view request = input.substr(0, end + 2);
        const std::string_view key = headerOf(request, "sec-websocket-key");
        if (!request.starts_with("GET ") || !listHolds(headerOf(request, "upgrade"), "websocket") || key.empty() ||
            !listHolds(headerOf(request, "sec-websocket-protocol"), protocol))
        {
            DEVEX_LOG_WARNING("Refused a WebSocket connection: it is not one of this game, or of this version of it");
            refuse(connection);
            return;
        }
        const std::string answer = std::format("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                               "Sec-WebSocket-Accept: {}\r\nSec-WebSocket-Protocol: {}\r\n\r\n",
                                               webSocketAccept(key), protocol);
        const auto* const bytes = reinterpret_cast<const std::byte*>(answer.data());
        connection.output.insert(connection.output.end(), bytes, bytes + answer.size());
        connection.input.erase(connection.input.begin(), connection.input.begin() + static_cast<std::ptrdiff_t>(end + 4));
        connection.open = true;
        events.push_back({.kind = Event::Kind::Opened, .connection = connection.id});
    }

    // The frames that arrived whole.
    void frames(Connection& connection, std::vector<Event>& events)
    {
        std::size_t at = 0;
        const std::vector<std::byte>& input = connection.input;
        while (!connection.closing && input.size() - at >= 2)
        {
            const auto first = std::to_integer<std::uint8_t>(input[at]);
            const auto second = std::to_integer<std::uint8_t>(input[at + 1]);
            const bool last = (first & 0x80u) != 0;
            const auto opcode = static_cast<Opcode>(first & 0x0Fu);
            std::uint64_t size = second & 0x7Fu;
            std::size_t header = 2;
            if (size == 126 || size == 127)
            {
                const std::size_t count = size == 126 ? 2 : 8;
                if (input.size() - at < header + count)
                {
                    break;
                }
                size = 0;
                for (std::size_t index = 0; index < count; ++index)
                {
                    size = (size << 8) | std::to_integer<std::uint64_t>(input[at + header + index]);
                }
                header += count;
            }
            // Clients mask what they send, extensions are not spoken, and a control frame is short and whole.
            const bool control = (opcode & 0x8u) != 0;
            if ((first & 0x70u) != 0 || (second & 0x80u) == 0 || size > messageLimit || (control && (!last || size > 125)))
            {
                closeWith(connection, 1002);
                break;
            }
            if (input.size() - at < header + 4 + size)
            {
                break;
            }
            const std::byte* const mask = input.data() + at + header;
            std::vector<std::byte> payload(input.begin() + static_cast<std::ptrdiff_t>(at + header + 4),
                                           input.begin() + static_cast<std::ptrdiff_t>(at + header + 4 + size));
            for (std::size_t index = 0; index < payload.size(); ++index)
            {
                payload[index] ^= mask[index % 4];
            }
            at += header + 4 + static_cast<std::size_t>(size);
            switch (opcode)
            {
            case Text:
            case Binary:
            case Continuation:
                connection.message.insert(connection.message.end(), payload.begin(), payload.end());
                if (connection.message.size() > messageLimit)
                {
                    closeWith(connection, 1009);
                }
                else if (last)
                {
                    events.push_back({.kind = Event::Kind::Message, .connection = connection.id, .bytes = std::exchange(connection.message, {})});
                }
                break;
            case Close:
                appendFrame(connection.output, Close, std::span(payload).first(std::min<std::size_t>(payload.size(), 2)));
                connection.closing = true;
                break;
            case Ping:
                appendFrame(connection.output, Pong, payload);
                break;
            case Pong:
                break;
            default:
                closeWith(connection, 1002);
                break;
            }
        }
        connection.input.erase(connection.input.begin(), connection.input.begin() + static_cast<std::ptrdiff_t>(at));
    }

    static void closeWith(Connection& connection, std::uint16_t status)
    {
        const std::array<std::byte, 2> code{static_cast<std::byte>(status >> 8), static_cast<std::byte>(status)};
        appendFrame(connection.output, Close, code);
        connection.closing = true;
    }
};

WebSocketServer::WebSocketServer(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

WebSocketServer::~WebSocketServer() = default;

core::Result<std::unique_ptr<WebSocketServer>> WebSocketServer::listen(std::uint16_t port, std::string protocol)
{
    auto impl = std::make_unique<Impl>();
    impl->protocol = std::move(protocol);
    impl->listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl->listener == invalidSocket)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot open a socket");
    }
#if !defined(_WIN32)
    // A host started again at once takes its port back, while the connections of the last one wind down.
    const int reuse = 1;
    setsockopt(impl->listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(impl->listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(impl->listener, 16) != 0 ||
        !makeNonBlocking(impl->listener))
    {
        return core::makeError(core::ErrorCode::Platform, "port {} is taken over TCP", port);
    }
    return std::unique_ptr<WebSocketServer>(new WebSocketServer(std::move(impl)));
}

void WebSocketServer::service(std::vector<Event>& events, std::chrono::milliseconds silenceLimit)
{
    Impl& impl = *m_impl;
    const Clock::time_point now = Clock::now();
    impl.accept(now);
    for (Impl::Connection& connection : impl.connections)
    {
        bool alive = Impl::read(connection);
        if (!connection.input.empty())
        {
            connection.heard = now;
            if (!connection.open && !connection.closing)
            {
                impl.handshake(connection, events);
            }
            if (connection.open && !connection.closing)
            {
                impl.frames(connection, events);
            }
        }
        if (!connection.open && !connection.closing && now - connection.heard > handshakeTime)
        {
            alive = false;
        }
        if (connection.open && !connection.closing)
        {
            if (now - connection.heard > silenceLimit)
            {
                DEVEX_LOG_WARNING("A player in a browser said nothing for {} s: closing its connection",
                                  std::chrono::duration_cast<std::chrono::seconds>(silenceLimit).count());
                alive = false;
            }
            else if (now - connection.pinged > pingInterval)
            {
                appendFrame(connection.output, Ping, {});
                connection.pinged = now;
            }
        }
        alive = Impl::write(connection) && alive;
        if (!alive || (connection.closing && connection.output.empty()))
        {
            if (connection.open && !connection.quiet)
            {
                events.push_back({.kind = Event::Kind::Closed, .connection = connection.id});
            }
            closeSocket(connection.socket);
            connection.socket = invalidSocket;
        }
    }
    std::erase_if(impl.connections, [](const Impl::Connection& connection) { return connection.socket == invalidSocket; });
}

void WebSocketServer::send(std::uint32_t connection, std::span<const std::byte> bytes)
{
    const auto found = std::ranges::find(m_impl->connections, connection, &Impl::Connection::id);
    if (found == m_impl->connections.end() || !found->open || found->closing)
    {
        return;
    }
    appendFrame(found->output, Binary, bytes);
    // At once, as ENet does when flushed: the next service writes what is left.
    if (!Impl::write(*found))
    {
        found->closing = true;
        found->output.clear();
    }
}

void WebSocketServer::close(std::uint32_t connection)
{
    const auto found = std::ranges::find(m_impl->connections, connection, &Impl::Connection::id);
    if (found != m_impl->connections.end() && !found->closing)
    {
        Impl::closeWith(*found, 1000);
        found->quiet = true;
    }
}

} // namespace devex::net::detail
