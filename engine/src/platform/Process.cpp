#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_properties.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <utility>

namespace devex::platform {

std::string environmentVariable(std::string_view name)
{
    const std::string variable(name);
    const char* value = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), variable.c_str());
    return value != nullptr ? value : "";
}

void setEnvironmentVariable(std::string_view name, std::string_view value)
{
    // The environment SDL keeps is the one the programs started below inherit, so it is the one
    // to change rather than the environment of the C runtime.
    const std::string variable(name);
    const std::string contents(value);
    if (contents.empty())
    {
        static_cast<void>(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), variable.c_str()));
        return;
    }
    static_cast<void>(
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), variable.c_str(), contents.c_str(), true));
}
namespace {

#ifdef _WIN32
[[nodiscard]] bool isValidUtf8(std::string_view text) noexcept
{
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto lead = static_cast<unsigned char>(text[index]);
        const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || index + length > text.size())
        {
            return false;
        }
        for (std::size_t continuation = 1; continuation < length; ++continuation)
        {
            if ((static_cast<unsigned char>(text[index + continuation]) >> 6) != 0x2)
            {
                return false;
            }
        }
        index += length;
    }
    return true;
}
#endif

// Console programs such as compilers write in the console code page, not always UTF-8.
[[nodiscard]] std::string toUtf8Line(std::string line)
{
#ifdef _WIN32
    if (!isValidUtf8(line))
    {
        const int wideLength = MultiByteToWideChar(CP_OEMCP, 0, line.data(), static_cast<int>(line.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
        MultiByteToWideChar(CP_OEMCP, 0, line.data(), static_cast<int>(line.size()), wide.data(), wideLength);
        const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, utf8.data(), length, nullptr, nullptr);
        return utf8;
    }
#endif
    return line;
}

[[nodiscard]] core::Result<SDL_Process*>
createProcess(std::span<const std::string> arguments, const std::filesystem::path& workingDirectory,
              bool captureOutput, bool interactive = false)
{
    if (arguments.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a process needs a program to run");
    }
    std::vector<const char*> argv;
    for (const std::string& argument : arguments)
    {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          captureOutput ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    if (interactive)
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                              SDL_PROCESS_STDIO_APP);
    const std::string directory = core::toUtf8(workingDirectory);
    if (!directory.empty())
    {
        SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, directory.c_str());
    }
    SDL_Process* const process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (process == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot run '{}': {}", arguments.front(), SDL_GetError());
    }
    return process;
}

} // namespace

core::Result<Process> Process::start(std::span<const std::string> arguments, const std::filesystem::path& workingDirectory)
{
    core::Result<SDL_Process*> process = createProcess(arguments, workingDirectory, true);
    if (!process)
    {
        return std::unexpected(process.error());
    }
    return Process(*process);
}

core::Result<void> Process::launch(std::span<const std::string> arguments, const std::filesystem::path& workingDirectory)
{
    core::Result<SDL_Process*> process = createProcess(arguments, workingDirectory, false);
    if (!process)
    {
        return std::unexpected(process.error());
    }
    // Destroying the object that tracks the process leaves the process running.
    SDL_DestroyProcess(*process);
    return {};
}

core::Result<Process> Process::startInteractive(std::span<const std::string> arguments,
                                                const std::filesystem::path& workingDirectory)
{
    const auto process = createProcess(arguments, workingDirectory, true, true);
    if (!process)
        return std::unexpected(process.error());
    return Process(*process);
}

std::uint32_t Process::id() const noexcept
{
    return m_process != nullptr
               ? static_cast<std::uint32_t>(SDL_GetNumberProperty(
                     SDL_GetProcessProperties(static_cast<SDL_Process*>(m_process)),
                     SDL_PROP_PROCESS_PID_NUMBER, 0))
               : 0;
}

core::Result<void> Process::sendLine(std::string_view line)
{
    if (line.size() > 1024 || line.find_first_of("\r\n") != std::string_view::npos)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "a process command must fit on one short line");
    SDL_IOStream* const input =
        m_process != nullptr ? SDL_GetProcessInput(static_cast<SDL_Process*>(m_process)) : nullptr;
    const std::string command = std::string(line) + '\n';
    if (input == nullptr)
        return core::makeError(core::ErrorCode::Platform, "the process has no command pipe");
#ifndef _WIN32
    // A child may crash between checking its state and writing. A broken pipe must not kill its
    // supervisor with SIGPIPE. Mask it on this thread only, preserving any already pending signal.
    sigset_t signals{}, previous{}, pending{};
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &signals, &previous) != 0)
        return core::makeError(core::ErrorCode::Platform,
                               "cannot protect the process command pipe");
    sigpending(&pending);
    const bool alreadyPending = sigismember(&pending, SIGPIPE) == 1;
#endif
    const bool written = SDL_WriteIO(input, command.data(), command.size()) == command.size();
#ifndef _WIN32
    if (!written && !alreadyPending)
    {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE) == 1)
        {
            int consumed = 0;
            static_cast<void>(sigwait(&signals, &consumed));
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
#endif
    if (!written)
        return core::makeError(core::ErrorCode::Platform, "cannot send the process command: {}",
                               SDL_GetError());
    return {};
}

std::vector<std::string> ProcessInput::readLines()
{
    // A bounded read leaves time for the game even when its controller sends many commands.
    char buffer[4096];
    for (int batch = 0; !m_closed && batch < 16; ++batch)
    {
        std::size_t count = 0;
#ifdef _WIN32
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD available = 0, read = 0;
        if (input == nullptr || input == INVALID_HANDLE_VALUE ||
            GetFileType(input) != FILE_TYPE_PIPE ||
            !PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
        {
            m_closed = true;
            break;
        }
        if (available == 0)
            break;
        if (!ReadFile(input, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr))
        {
            m_closed = true;
            break;
        }
        count = read;
#else
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input, 1, 0);
        if (ready < 0 && errno == EINTR)
            continue;
        if (isatty(STDIN_FILENO) || ready < 0 || (input.revents & (POLLERR | POLLNVAL)))
        {
            m_closed = true;
            break;
        }
        if (!(input.revents & (POLLIN | POLLHUP)))
            break;
        const auto read = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (read < 0 && errno == EINTR)
            continue;
        if (read <= 0)
        {
            m_closed = true;
            break;
        }
        count = static_cast<std::size_t>(read);
#endif
        m_pending.append(buffer, count);
        if (m_pending.size() > 65536)
        {
            m_closed = true;
            break;
        }
    }
    std::vector<std::string> lines;
    for (auto newline = m_pending.find('\n'); newline != std::string::npos;
         newline = m_pending.find('\n'))
    {
        std::string line = m_pending.substr(0, newline);
        m_pending.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

Process::Process(void* process) noexcept
    : m_process(process)
{
}

Process::Process(Process&& other) noexcept
    : m_process(std::exchange(other.m_process, nullptr))
    , m_pending(std::move(other.m_pending))
    , m_exitCode(other.m_exitCode)
{
}

Process& Process::operator=(Process&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_process = std::exchange(other.m_process, nullptr);
        m_pending = std::move(other.m_pending);
        m_exitCode = other.m_exitCode;
    }
    return *this;
}

Process::~Process()
{
    destroy();
}

void Process::destroy() noexcept
{
    if (m_process != nullptr)
    {
        if (!m_exitCode)
        {
            kill();
        }
        SDL_DestroyProcess(static_cast<SDL_Process*>(std::exchange(m_process, nullptr)));
    }
}

std::vector<std::string> Process::readLines()
{
    std::vector<std::string> lines;
    if (m_process == nullptr)
    {
        return lines;
    }
    // The output stream does not block: it returns what the pipe holds.
    SDL_IOStream* const output = SDL_GetProcessOutput(static_cast<SDL_Process*>(m_process));
    char buffer[4096];
    for (int batch = 0; output != nullptr && batch < 16; ++batch)
    {
        const std::size_t read = SDL_ReadIO(output, buffer, sizeof(buffer));
        if (read == 0)
        {
            break;
        }
        m_pending.append(buffer, read);
    }
    std::size_t start = 0;
    for (std::size_t newline = m_pending.find('\n'); newline != std::string::npos;
         newline = m_pending.find('\n', start))
    {
        std::string line = m_pending.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(toUtf8Line(std::move(line)));
        start = newline + 1;
    }
    m_pending.erase(0, start);
    if (m_pending.size() > 256 * 1024)
        lines.push_back(toUtf8Line(std::exchange(m_pending, {})) + " [long process output split]");
    if (m_exitCode && !m_pending.empty())
    {
        lines.push_back(toUtf8Line(std::exchange(m_pending, {})));
    }
    return lines;
}

std::optional<int> Process::exitCode()
{
    if (!m_exitCode && m_process != nullptr)
    {
        int code = 0;
        if (SDL_WaitProcess(static_cast<SDL_Process*>(m_process), false, &code))
        {
            m_exitCode = code;
        }
    }
    return m_exitCode;
}

void Process::kill() noexcept
{
    if (m_process != nullptr && !m_exitCode)
    {
        SDL_KillProcess(static_cast<SDL_Process*>(m_process), true);
    }
}

std::uint32_t currentProcessId() noexcept
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

} // namespace devex::platform
