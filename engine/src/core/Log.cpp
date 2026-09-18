#include <devex/core/Log.hpp>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <print>
#include <vector>

namespace devex::core {
namespace {

#ifdef NDEBUG
constexpr LogLevel defaultLogLevel = LogLevel::Info;
#else
constexpr LogLevel defaultLogLevel = LogLevel::Debug;
#endif

std::string_view fileName(std::string_view path) noexcept
{
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

class Logger
{
public:
    Logger()
        : m_startTime(std::chrono::steady_clock::now())
    {
        m_sinks.push_back({consoleLogSinkId, [this](const LogRecord& record) { writeToConsole(record); }});
    }

    void setLevel(LogLevel level) noexcept
    {
        m_level.store(level, std::memory_order_relaxed);
    }

    [[nodiscard]] LogLevel level() const noexcept
    {
        return m_level.load(std::memory_order_relaxed);
    }

    [[nodiscard]] LogSinkId addSink(LogSink sink)
    {
        const std::scoped_lock lock(m_mutex);
        const LogSinkId id{++m_lastSinkId};
        m_sinks.push_back({id, std::move(sink)});
        return id;
    }

    void removeSink(LogSinkId id)
    {
        const std::scoped_lock lock(m_mutex);
        std::erase_if(m_sinks, [id](const SinkEntry& entry) { return entry.id == id; });
    }

    void write(const LogRecord& record)
    {
        const std::scoped_lock lock(m_mutex);
        for (const SinkEntry& entry : m_sinks)
        {
            entry.sink(record);
        }
    }

private:
    struct SinkEntry
    {
        LogSinkId id;
        LogSink sink;
    };

    void writeToConsole(const LogRecord& record) const
    {
        const bool isProblem = record.level >= LogLevel::Warning;
        std::FILE* const stream = isProblem ? stderr : stdout;
        const std::chrono::duration<double> elapsed = record.time - m_startTime;

        // stdout is buffered when redirected: flush it so both streams keep the message order.
        if (isProblem)
        {
            std::fflush(stdout);
        }

        // Messages from other languages, such as C#, come without a C++ location.
        if (record.level >= LogLevel::Error && record.location.line() != 0)
        {
            std::println(stream, "[{:9.3f}] {:<7} {} ({}:{})", elapsed.count(),
                         toString(record.level), record.message,
                         fileName(record.location.file_name()), record.location.line());
        }
        else
        {
            std::println(stream, "[{:9.3f}] {:<7} {}", elapsed.count(), toString(record.level),
                         record.message);
        }

        if (isProblem)
        {
            std::fflush(stream);
        }
    }

    std::chrono::steady_clock::time_point m_startTime;
    std::atomic<LogLevel> m_level{defaultLogLevel};
    std::mutex m_mutex;
    std::vector<SinkEntry> m_sinks;
    std::uint32_t m_lastSinkId = 0;
};

Logger& logger()
{
    static Logger instance;
    return instance;
}

} // namespace

std::string_view toString(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Fatal:
        return "fatal";
    case LogLevel::Off:
        return "off";
    }
    return "unknown";
}

void setLogLevel(LogLevel level) noexcept
{
    logger().setLevel(level);
}

LogLevel logLevel() noexcept
{
    return logger().level();
}

bool isLogLevelEnabled(LogLevel level) noexcept
{
    return level != LogLevel::Off && level >= logger().level();
}

LogSinkId addLogSink(LogSink sink)
{
    return logger().addSink(std::move(sink));
}

void removeLogSink(LogSinkId id)
{
    logger().removeSink(id);
}

void logMessage(LogLevel level, std::string_view message, std::source_location location)
{
    if (!isLogLevelEnabled(level))
    {
        return;
    }
    logger().write({level, message, location, std::chrono::steady_clock::now()});
}

} // namespace devex::core
