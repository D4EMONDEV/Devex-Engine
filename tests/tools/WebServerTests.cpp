// The server of Run in Browser: it serves the files of its folder with the types a browser wants, and nothing outside it.
#include <devex/core/File.hpp>
#include <devex/tools/WebServer.hpp>

#include <catch2/catch_test_macros.hpp>

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
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() /
               std::format("devex-web-server-{}", std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        std::filesystem::create_directories(path / "site" / "data");
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};

// What the server answers to one request, whole.
[[nodiscard]] std::string fetch(std::uint16_t port, const std::string& request)
{
#if defined(_WIN32)
    using Socket = SOCKET;
    const auto closeSocket = [](Socket socket) { closesocket(socket); };
    WSADATA data;
    static_cast<void>(WSAStartup(MAKEWORD(2, 2), &data));
#else
    using Socket = int;
    const auto closeSocket = [](Socket socket) { close(socket); };
#endif
    const Socket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::string answer;
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
    {
        send(client, request.data(), static_cast<int>(request.size()), 0);
        char buffer[4096];
        for (;;)
        {
            const auto received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
            if (received <= 0)
            {
                break;
            }
            answer.append(buffer, static_cast<std::size_t>(received));
        }
    }
    closeSocket(client);
    return answer;
}

} // namespace

TEST_CASE("The server of Run in Browser serves its folder, and nothing outside it", "[tools][web][noweb]")
{
    TemporaryDirectory directory;
    REQUIRE(devex::core::writeTextFile(directory.path / "site" / "index.html", "<!doctype html><title>Game</title>"));
    REQUIRE(devex::core::writeTextFile(directory.path / "site" / "data" / "game.wasm", "\0asm"));
    REQUIRE(devex::core::writeTextFile(directory.path / "secret.txt", "outside"));
    devex::core::Result<std::unique_ptr<devex::tools::WebServer>> server =
        devex::tools::WebServer::start(directory.path / "site", 18060);
    REQUIRE(server.has_value());
    const std::uint16_t port = (*server)->port();
    CHECK((*server)->url() == std::format("http://localhost:{}/", port));

    // The page of the folder, for its root.
    const std::string page = fetch(port, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    CHECK(page.starts_with("HTTP/1.1 200 OK"));
    CHECK(page.find("Content-Type: text/html") != std::string::npos);
    CHECK(page.ends_with("<!doctype html><title>Game</title>"));
    // WebAssembly with the type a browser compiles it by, from a folder, a name written with %XX.
    const std::string wasm = fetch(port, "GET /data/game%2Ewasm?v=1 HTTP/1.1\r\n\r\n");
    CHECK(wasm.starts_with("HTTP/1.1 200 OK"));
    CHECK(wasm.find("Content-Type: application/wasm") != std::string::npos);
    // Nothing outside the folder, nor what is not there.
    CHECK(fetch(port, "GET /../secret.txt HTTP/1.1\r\n\r\n").starts_with("HTTP/1.1 404"));
    CHECK(fetch(port, "GET /data/../../secret.txt HTTP/1.1\r\n\r\n").starts_with("HTTP/1.1 404"));
    CHECK(fetch(port, "GET /missing.js HTTP/1.1\r\n\r\n").starts_with("HTTP/1.1 404"));
    CHECK(fetch(port, "POST / HTTP/1.1\r\n\r\n").starts_with("HTTP/1.1 405"));

    // A second server of the same port takes the next free one.
    devex::core::Result<std::unique_ptr<devex::tools::WebServer>> second =
        devex::tools::WebServer::start(directory.path / "site", port);
    REQUIRE(second.has_value());
    CHECK((*second)->port() != port);
}
