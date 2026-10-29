#pragma once

#include <devex/core/Export.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <source_location>
#include <string>
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
// A level by its name, as toString writes it ("warn" too); nothing for another word.
[[nodiscard]] DEVEX_API std::optional<LogLevel> parseLogLevel(std::string_view name) noexcept;

struct DEVEX_API LogRecord
{
    LogLevel level = LogLevel::Info;
    std::string_view message;
    std::source_location location;
    std::chrono::steady_clock::time_point time;
    // The part of the engine the message comes from, such as "render" or "build": given by the
    // call, or else the folder of its source file (see logCategoryOf).
    std::string_view category;
};

// The category of the messages of a source file: the folder of the engine it is in (render, asset,
// scene...), the program for those of apps/ (editor, player), and "game" for the code of a project
// and for messages without a place, such as those of C#.
[[nodiscard]] DEVEX_API std::string_view logCategoryOf(std::string_view file) noexcept;

// Sinks run under the logger lock, in registration order: a sink must never log itself.
using LogSink = std::function<void(const LogRecord& record)>;

enum class LogSinkId : std::uint32_t
{
};

// The console sink is registered at startup and can be removed like any other sink.
inline constexpr LogSinkId consoleLogSinkId{0};

// Messages below this level are discarded without evaluating their arguments, unless their category
// has a level of its own.
DEVEX_API void setLogLevel(LogLevel level) noexcept;
[[nodiscard]] DEVEX_API LogLevel logLevel() noexcept;
// The level of the messages of one category, over the level of all; clearLogCategoryLevels drops
// every such level.
DEVEX_API void setLogLevel(std::string_view category, LogLevel level);
[[nodiscard]] DEVEX_API LogLevel logLevel(std::string_view category) noexcept;
DEVEX_API void clearLogCategoryLevels();
// Whether a message of this level may be kept, in some category: the cheap test the macros make
// before they format anything.
[[nodiscard]] DEVEX_API bool isLogLevelEnabled(LogLevel level) noexcept;
// Whether a message of this level and category is kept.
[[nodiscard]] DEVEX_API bool isLogEnabled(LogLevel level, std::string_view category) noexcept;

// Sets the levels from a text such as "debug" or "info,render=trace,build=warning": a level for all,
// then levels of categories, as the DEVEX_LOG variable and --log-level give them. The levels of
// categories it does not name are dropped. Returns what it could not read, or nothing.
[[nodiscard]] DEVEX_API std::optional<std::string> configureLog(std::string_view settings);

[[nodiscard]] DEVEX_API LogSinkId addLogSink(LogSink sink);
// When the logger started: the lines of the console and of a log file count their seconds from it.
[[nodiscard]] DEVEX_API std::chrono::steady_clock::time_point logStartTime() noexcept;
DEVEX_API void removeLogSink(LogSinkId id);

// Without a category, the message takes the one of the file it comes from.
DEVEX_API void logMessage(LogLevel level, std::string_view message,
                          std::source_location location = std::source_location::current(),
                          std::string_view category = {});

namespace detail {

template <typename... Args>
void logFormatted(LogLevel level, std::source_location location, std::string_view category,
                  std::format_string<Args...> format, Args&&... args)
{
    logMessage(level, std::format(format, std::forward<Args>(args)...), location, category);
}

} // namespace detail

} // namespace devex::core

#define DEVEX_LOG_AT(level, ...)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (::devex::core::isLogLevelEnabled(level))                                               \
        {                                                                                          \
            ::devex::core::detail::logFormatted(level, std::source_location::current(), {},       \
                                                __VA_ARGS__);                                      \
        }                                                                                          \
    } while (false)

// The same in a category named by the call, such as "build" for the output of compilers.
#define DEVEX_LOG_IN_AT(category, level, ...)                                                      \
    do                                                                                             \
    {                                                                                              \
        if (::devex::core::isLogEnabled(level, category))                                          \
        {                                                                                          \
            ::devex::core::detail::logFormatted(level, std::source_location::current(), category, \
                                                __VA_ARGS__);                                      \
        }                                                                                          \
    } while (false)

#define DEVEX_LOG_TRACE(...) DEVEX_LOG_AT(::devex::core::LogLevel::Trace, __VA_ARGS__)
#define DEVEX_LOG_DEBUG(...) DEVEX_LOG_AT(::devex::core::LogLevel::Debug, __VA_ARGS__)
#define DEVEX_LOG_INFO(...) DEVEX_LOG_AT(::devex::core::LogLevel::Info, __VA_ARGS__)
#define DEVEX_LOG_WARNING(...) DEVEX_LOG_AT(::devex::core::LogLevel::Warning, __VA_ARGS__)
#define DEVEX_LOG_ERROR(...) DEVEX_LOG_AT(::devex::core::LogLevel::Error, __VA_ARGS__)
#define DEVEX_LOG_FATAL(...) DEVEX_LOG_AT(::devex::core::LogLevel::Fatal, __VA_ARGS__)

#define DEVEX_LOG_TRACE_IN(category, ...) DEVEX_LOG_IN_AT(category, ::devex::core::LogLevel::Trace, __VA_ARGS__)
#define DEVEX_LOG_DEBUG_IN(category, ...) DEVEX_LOG_IN_AT(category, ::devex::core::LogLevel::Debug, __VA_ARGS__)
#define DEVEX_LOG_INFO_IN(category, ...) DEVEX_LOG_IN_AT(category, ::devex::core::LogLevel::Info, __VA_ARGS__)
#define DEVEX_LOG_WARNING_IN(category, ...) DEVEX_LOG_IN_AT(category, ::devex::core::LogLevel::Warning, __VA_ARGS__)
#define DEVEX_LOG_ERROR_IN(category, ...) DEVEX_LOG_IN_AT(category, ::devex::core::LogLevel::Error, __VA_ARGS__)
