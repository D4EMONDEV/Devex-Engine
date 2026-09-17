#pragma once

#include <devex/core/Error.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace devex::platform {

// A program run in the background with its output captured and read without blocking, such as a
// compiler. Standard error is merged into standard output.
class Process
{
public:
    // The first argument is the program, looked up in PATH. An empty working directory keeps the
    // current one.
    [[nodiscard]] static core::Result<Process> start(std::span<const std::string> arguments,
                                                     const std::filesystem::path& workingDirectory = {});
    // Starts a program that runs on its own, such as a game: its output is discarded and it keeps
    // running when the caller exits.
    [[nodiscard]] static core::Result<void> launch(std::span<const std::string> arguments,
                                                   const std::filesystem::path& workingDirectory = {});

    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    // Kills the program if it is still running.
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // The lines written since the last call, without their line ends. Once the program has ended,
    // an unfinished last line is returned too.
    [[nodiscard]] std::vector<std::string> readLines();

    // The exit code, once the program has ended.
    [[nodiscard]] std::optional<int> exitCode();

    void kill() noexcept;

private:
    explicit Process(void* process) noexcept;
    void destroy() noexcept;

    void* m_process = nullptr;
    std::string m_pending;
    std::optional<int> m_exitCode;
};

} // namespace devex::platform
