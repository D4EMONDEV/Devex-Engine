#include <devex/tools/WebServer.hpp>

#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

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
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <format>
#include <string_view>
#include <vector>

namespace devex::tools {
namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalidSocket = INVALID_SOCKET;
void closeSocket(Socket socket)
{
    closesocket(socket);
}
#else
using Socket = int;
constexpr Socket invalidSocket = -1;
void closeSocket(Socket socket)
{
    close(socket);
}
#endif

[[nodiscard]] Socket toSocket(std::intptr_t value) noexcept
{
    return static_cast<Socket>(value);
}

void sendAll(Socket socket, std::string_view bytes)
{
    while (!bytes.empty())
    {
        const auto sent = send(socket, bytes.data(), static_cast<int>(std::min<std::size_t>(bytes.size(), 1 << 20)), 0);
        if (sent <= 0)
        {
            return;
        }
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
}

[[nodiscard]] int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// The path of a request, its %XX decoded, without its query.
[[nodiscard]] std::string decodePath(std::string_view target)
{
    target = target.substr(0, target.find_first_of("?#"));
    std::string decoded;
    for (std::size_t at = 0; at < target.size(); ++at)
    {
        if (target[at] == '%' && at + 2 < target.size() && hexValue(target[at + 1]) >= 0 && hexValue(target[at + 2]) >= 0)
        {
            decoded.push_back(static_cast<char>(hexValue(target[at + 1]) * 16 + hexValue(target[at + 2])));
            at += 2;
        }
        else
        {
            decoded.push_back(target[at]);
        }
    }
    return decoded;
}

// Answers one request: the file it names inside the folder, or an error.
void answer(Socket client, const std::filesystem::path& folder)
{
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384)
    {
        const auto received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0)
        {
            return;
        }
        request.append(buffer, static_cast<std::size_t>(received));
    }
    // The headers that isolate the page, which a game of Thread Support needs to run its player with threads.
    const auto reply = [client](std::string_view status, std::string_view type, std::string_view body, bool head) {
        sendAll(client, std::format("HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-cache\r\n"
                                    "Cross-Origin-Opener-Policy: same-origin\r\nCross-Origin-Embedder-Policy: require-corp\r\n"
                                    "Connection: close\r\n\r\n",
                                    status, type, body.size()));
        if (!head)
        {
            sendAll(client, body);
        }
    };
    const std::size_t methodEnd = request.find(' ');
    const std::size_t targetEnd = methodEnd == std::string::npos ? std::string::npos : request.find(' ', methodEnd + 1);
    if (targetEnd == std::string::npos)
    {
        reply("400 Bad Request", "text/plain", "Bad request\n", false);
        return;
    }
    const std::string_view method = std::string_view(request).substr(0, methodEnd);
    const bool head = method == "HEAD";
    if (method != "GET" && !head)
    {
        reply("405 Method Not Allowed", "text/plain", "Only GET and HEAD\n", false);
        return;
    }
    std::string path = decodePath(std::string_view(request).substr(methodEnd + 1, targetEnd - methodEnd - 1));
    if (path.empty() || path.back() == '/')
    {
        path += "index.html";
    }
    // Whatever the request says, it stays inside the folder.
    const std::filesystem::path relative = core::pathFromUtf8(path.substr(path.find_first_not_of('/'))).lexically_normal();
    const bool inside = !relative.empty() && !relative.is_absolute() && !relative.has_root_name() && *relative.begin() != "..";
    std::error_code error;
    if (!inside || !std::filesystem::is_regular_file(folder / relative, error))
    {
        reply("404 Not Found", "text/plain", "Not found\n", head);
        return;
    }
    const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(folder / relative);
    if (!bytes)
    {
        reply("500 Internal Server Error", "text/plain", "Cannot read the file\n", head);
        return;
    }
    reply("200 OK", WebServer::contentType(relative),
          std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), head);
}

} // namespace

std::string_view WebServer::contentType(const std::filesystem::path& file) noexcept
{
    std::string extension = core::toUtf8(file.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".html" || extension == ".htm")
    {
        return "text/html; charset=utf-8";
    }
    if (extension == ".js")
    {
        return "text/javascript";
    }
    if (extension == ".wasm")
    {
        return "application/wasm";
    }
    if (extension == ".json")
    {
        return "application/json";
    }
    if (extension == ".png")
    {
        return "image/png";
    }
    if (extension == ".css")
    {
        return "text/css";
    }
    if (extension == ".txt" || extension == ".md")
    {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

core::Result<std::unique_ptr<WebServer>> WebServer::start(std::filesystem::path folder, std::uint16_t port)
{
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot start the sockets of Windows");
    }
#endif
    std::unique_ptr<WebServer> server(new WebServer());
    server->m_folder = std::move(folder);
    // The first free port of a few after the one asked for.
    for (std::uint16_t tried = port; tried < port + 40; ++tried)
    {
        const Socket listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listening == invalidSocket)
        {
            break;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(tried);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listening, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 && listen(listening, 16) == 0)
        {
            server->m_socket = static_cast<std::intptr_t>(listening);
            server->m_port = tried;
            break;
        }
        closeSocket(listening);
    }
    if (server->m_port == 0)
    {
        return core::makeError(core::ErrorCode::Platform, "no port from {} is free to serve the game", port);
    }
    server->m_thread = std::thread([raw = server.get()] { raw->serve(); });
    DEVEX_LOG_INFO("Serving {} at {}", core::toUtf8(server->m_folder), server->url());
    return server;
}

WebServer::~WebServer()
{
    m_stopping = true;
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    if (m_socket != -1)
    {
        closeSocket(toSocket(m_socket));
    }
}

std::string WebServer::url() const
{
    return std::format("http://localhost:{}/", m_port);
}

void WebServer::serve()
{
    const Socket listening = toSocket(m_socket);
    while (!m_stopping)
    {
        // A tenth of a second at most between looks at whether to stop.
        fd_set waiting;
        FD_ZERO(&waiting);
        FD_SET(listening, &waiting);
        timeval timeout{.tv_sec = 0, .tv_usec = 100000};
        if (select(static_cast<int>(listening + 1), &waiting, nullptr, nullptr, &timeout) <= 0)
        {
            continue;
        }
        const Socket client = accept(listening, nullptr, nullptr);
        if (client == invalidSocket)
        {
            continue;
        }
        answer(client, m_folder);
        closeSocket(client);
    }
}

} // namespace devex::tools
