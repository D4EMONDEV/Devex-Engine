#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <source_location>
#include <string_view>
#include <utility>

namespace devex::core {

enum class LogLevel : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    Off,
};

[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

struct LogRecord
{
    LogLevel level = LogLevel::Info;
    std::string_view message;
    std::source_location location;
    std::chrono::steady_clock::time_point time;
};

// Sinks run under the logger lock, in registration order: a sink must never log itself.
using LogSink = std::function<void(const LogRecord& record)>;

enum class LogSinkId : std::uint32_t
{
};

// The console sink is registered at startup and can be removed like any other sink.
inline constexpr LogSinkId consoleLogSinkId{0};

// Messages below this level are discarded without evaluating their arguments.
void setLogLevel(LogLevel level) noexcept;
[[nodiscard]] LogLevel logLevel() noexcept;
[[nodiscard]] bool isLogLevelEnabled(LogLevel level) noexcept;

[[nodiscard]] LogSinkId addLogSink(LogSink sink);
void removeLogSink(LogSinkId id);

void logMessage(LogLevel level, std::string_view message,
                std::source_location location = std::source_location::current());

namespace detail {

template <typename... Args>
void logFormatted(LogLevel level, std::source_location location,
                  std::format_string<Args...> format, Args&&... args)
{
    logMessage(level, std::format(format, std::forward<Args>(args)...), location);
}

} // namespace detail

} // namespace devex::core

#define DEVEX_LOG_AT(level, ...)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (::devex::core::isLogLevelEnabled(level))                                               \
        {                                                                                          \
            ::devex::core::detail::logFormatted(level, std::source_location::current(),           \
                                                __VA_ARGS__);                                      \
        }                                                                                          \
    } while (false)

#define DEVEX_LOG_TRACE(...) DEVEX_LOG_AT(::devex::core::LogLevel::Trace, __VA_ARGS__)
#define DEVEX_LOG_DEBUG(...) DEVEX_LOG_AT(::devex::core::LogLevel::Debug, __VA_ARGS__)
#define DEVEX_LOG_INFO(...) DEVEX_LOG_AT(::devex::core::LogLevel::Info, __VA_ARGS__)
#define DEVEX_LOG_WARNING(...) DEVEX_LOG_AT(::devex::core::LogLevel::Warning, __VA_ARGS__)
#define DEVEX_LOG_ERROR(...) DEVEX_LOG_AT(::devex::core::LogLevel::Error, __VA_ARGS__)
#define DEVEX_LOG_FATAL(...) DEVEX_LOG_AT(::devex::core::LogLevel::Fatal, __VA_ARGS__)
