#pragma once

#include <devex/core/Export.hpp>

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

[[nodiscard]] DEVEX_API std::string_view toString(LogLevel level) noexcept;

struct DEVEX_API LogRecord
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
DEVEX_API void setLogLevel(LogLevel level) noexcept;
[[nodiscard]] DEVEX_API LogLevel logLevel() noexcept;
[[nodiscard]] DEVEX_API bool isLogLevelEnabled(LogLevel level) noexcept;

[[nodiscard]] DEVEX_API LogSinkId addLogSink(LogSink sink);
// When the logger started: the lines of the console and of a log file count their seconds from it.
[[nodiscard]] DEVEX_API std::chrono::steady_clock::time_point logStartTime() noexcept;
DEVEX_API void removeLogSink(LogSinkId id);

DEVEX_API void logMessage(LogLevel level, std::string_view message,
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
