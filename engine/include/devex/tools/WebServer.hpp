#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace devex::tools {

// Serves the files of a folder over HTTP to this computer only, as Run in Browser needs for a game exported for the
// web: a browser does not run the WebAssembly of a page opened as a file. Requests are answered one at a time, each
// on a connection of its own, and the folder is never left.
class DEVEX_API WebServer
{
public:
    // Serves `folder` on 127.0.0.1, on the first free port from `port`.
    [[nodiscard]] static core::Result<std::unique_ptr<WebServer>> start(std::filesystem::path folder, std::uint16_t port = 8060);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    [[nodiscard]] const std::filesystem::path& folder() const noexcept
    {
        return m_folder;
    }
    [[nodiscard]] std::uint16_t port() const noexcept
    {
        return m_port;
    }
    // The page of the folder, as a browser opens it.
    [[nodiscard]] std::string url() const;

    // The type an answer gives a file, by its extension: a browser compiles WebAssembly only as application/wasm.
    [[nodiscard]] static std::string_view contentType(const std::filesystem::path& file) noexcept;

private:
    WebServer() = default;
    void serve();

    std::filesystem::path m_folder;
    std::uint16_t m_port = 0;
    // The listening socket, as the system numbers it.
    std::intptr_t m_socket = -1;
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};

} // namespace devex::tools
