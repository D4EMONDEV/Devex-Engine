#pragma once

#include <devex/core/Error.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::net::detail {

// The WebSocket side of a host: players in a browser connect to it over TCP, on the port number that ENet uses over UDP.
// It speaks what browsers send (RFC 6455): messages, pings and closes, without extensions, and accepts only clients that
// ask for its protocol, which carries the version of the session. Served from the thread of the game, like ENet,
// without blocking.
class WebSocketServer
{
public:
    struct Event
    {
        enum class Kind : std::uint8_t
        {
            // A client finished its handshake.
            Opened,
            Message,
            // A client closed, broke off, or was silent too long.
            Closed,
        };
        Kind kind = Kind::Opened;
        std::uint32_t connection = 0;
        std::vector<std::byte> bytes;
    };

    // Listens on every address of this computer.
    [[nodiscard]] static core::Result<std::unique_ptr<WebSocketServer>> listen(std::uint16_t port, std::string protocol);
    ~WebSocketServer();
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // Accepts, reads and writes what is waiting, pings the clients now and then, and appends what happened. A client
    // that has said nothing, not even to a ping, for silenceLimit is closed.
    void service(std::vector<Event>& events, std::chrono::milliseconds silenceLimit);
    // One binary message.
    void send(std::uint32_t connection, std::span<const std::byte> bytes);
    // Closes a client, which then makes no Closed event.
    void close(std::uint32_t connection);

private:
    struct Impl;
    explicit WebSocketServer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
};

// The answer to the key of a handshake: the base64 of the SHA-1 of the key and the GUID of the protocol.
[[nodiscard]] std::string webSocketAccept(std::string_view key);

} // namespace devex::net::detail
