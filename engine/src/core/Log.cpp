#include <devex/core/Log.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <print>
#include <shared_mutex>
#include <span>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace devex::core {
namespace {

#ifdef NDEBUG
constexpr LogLevel defaultLogLevel = LogLevel::Info;
#else
constexpr LogLevel defaultLogLevel = LogLevel::Debug;
#endif

// The width the categories are aligned to on the console, as the levels are.
constexpr std::size_t categoryWidth = 9;

// The same message again within this time of the last one is counted rather than written.
constexpr std::chrono::milliseconds repeatWindow{1000};

std::string_view fileName(std::string_view path) noexcept
{
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

[[nodiscard]] bool isSeparator(char character) noexcept
{
    return character == '/' || character == '\\';
}

// Where a path goes on after a run of folders, separated by / or \ (the first run found): the
// position after the separator of the last one; npos when the path has no such run.
[[nodiscard]] std::size_t after(std::string_view path, std::span<const std::string_view> folders) noexcept
{
    for (std::size_t start = 0; start < path.size(); ++start)
    {
        if (start > 0 && !isSeparator(path[start - 1]))
        {
            continue;
        }
        std::size_t at = start;
        bool matched = true;
        for (const std::string_view folder : folders)
        {
            if (path.substr(at, folder.size()) != folder || at + folder.size() >= path.size() ||
                !isSeparator(path[at + folder.size()]))
            {
                matched = false;
                break;
            }
            at += folder.size() + 1;
        }
        if (matched)
        {
            return at;
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.remove_suffix(1);
    }
    return text;
}

// Colors on a terminal that shows them, as https://no-color.org lets the user refuse; never into a
// file or a pipe.
[[nodiscard]] bool consoleShowsColors() noexcept
{
#ifdef _WIN32
    return false;
#else
    const char* const noColor = std::getenv("NO_COLOR");
    const char* const terminal = std::getenv("TERM");
    return (noColor == nullptr || *noColor == '\0') && (terminal == nullptr || std::string_view(terminal) != "dumb") &&
           isatty(fileno(stdout)) != 0 && isatty(fileno(stderr)) != 0;
#endif
}

// The escape sequences of the console: the level, then its message.
[[nodiscard]] std::string_view levelColor(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
    case LogLevel::Debug:
        return "\x1b[90m";
    case LogLevel::Info:
        return "\x1b[36m";
    case LogLevel::Warning:
        return "\x1b[33m";
    case LogLevel::Error:
    case LogLevel::Fatal:
        return "\x1b[1;31m";
    case LogLevel::Off:
        break;
    }
    return "";
}

[[nodiscard]] std::string_view messageColor(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
    case LogLevel::Debug:
        return "\x1b[90m";
    case LogLevel::Warning:
        return "\x1b[33m";
    case LogLevel::Error:
    case LogLevel::Fatal:
        return "\x1b[31m";
    case LogLevel::Info:
    case LogLevel::Off:
        break;
    }
    return "";
}

class Logger
{
public:
    Logger()
        : m_startTime(std::chrono::steady_clock::now())
        , m_colors(consoleShowsColors())
    {
        m_sinks.push_back({consoleLogSinkId, [this](const LogRecord& record) { writeToConsole(record); }});
    }

    ~Logger()
    {
        const std::scoped_lock lock(m_mutex);
        writeRepeats();
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    [[nodiscard]] std::chrono::steady_clock::time_point startTime() const noexcept
    {
        return m_startTime;
    }

    void setLevel(LogLevel level)
    {
        const std::unique_lock lock(m_levelsMutex);
        m_level.store(level, std::memory_order_relaxed);
        updateMinimum();
    }

    [[nodiscard]] LogLevel level() const noexcept
    {
        return m_level.load(std::memory_order_relaxed);
    }

    void setCategoryLevel(std::string_view category, LogLevel level)
    {
        const std::unique_lock lock(m_levelsMutex);
        const auto found = std::ranges::find(m_categoryLevels, category, &CategoryLevel::category);
        if (found != m_categoryLevels.end())
        {
            found->level = level;
        }
        else
        {
            m_categoryLevels.push_back({std::string(category), level});
        }
        updateMinimum();
    }

    void clearCategoryLevels()
    {
        const std::unique_lock lock(m_levelsMutex);
        m_categoryLevels.clear();
        updateMinimum();
    }

    [[nodiscard]] LogLevel levelOf(std::string_view category) const noexcept
    {
        if (!m_hasCategoryLevels.load(std::memory_order_relaxed))
        {
            return level();
        }
        const std::shared_lock lock(m_levelsMutex);
        const auto found = std::ranges::find(m_categoryLevels, category, &CategoryLevel::category);
        return found != m_categoryLevels.end() ? found->level : level();
    }

    // The lowest level kept in any category.
    [[nodiscard]] LogLevel minimum() const noexcept
    {
        return m_minimum.load(std::memory_order_relaxed);
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

    struct CategoryLevel
    {
        std::string category;
        LogLevel level = LogLevel::Info;
    };

    // Under the lock of the levels.
    void updateMinimum() noexcept
    {
        LogLevel lowest = m_level.load(std::memory_order_relaxed);
        for (const CategoryLevel& category : m_categoryLevels)
        {
            lowest = std::min(lowest, category.level);
        }
        m_minimum.store(lowest, std::memory_order_relaxed);
        m_hasCategoryLevels.store(!m_categoryLevels.empty(), std::memory_order_relaxed);
    }

    // A line of the console: when, how grave, from where, then what.
    void writeLine(LogLevel level, std::chrono::steady_clock::time_point time, std::string_view category, std::string_view message,
                   std::string_view place) const
    {
        const bool isProblem = level >= LogLevel::Warning;
        std::FILE* const stream = isProblem ? stderr : stdout;
        const std::chrono::duration<double> elapsed = time - m_startTime;
        // stdout is buffered when redirected: flush it so both streams keep the message order.
        if (isProblem)
        {
            std::fflush(stdout);
        }
        if (m_colors)
        {
            std::println(stream, "\x1b[2m[{:9.3f}]\x1b[0m {}{:<7}\x1b[0m \x1b[34m{:<{}}\x1b[0m {}{}{}\x1b[0m", elapsed.count(),
                         levelColor(level), toString(level), category, categoryWidth, messageColor(level), message, place);
        }
        else
        {
            std::println(stream, "[{:9.3f}] {:<7} {:<{}} {}{}", elapsed.count(), toString(level), category, categoryWidth, message,
                         place);
        }
        if (isProblem)
        {
            std::fflush(stream);
        }
    }

    void writeToConsole(const LogRecord& record)
    {
        // The same message again, soon after: counted, and told once something else comes.
        if (m_last && m_last->level == record.level && m_last->category == record.category && m_last->message == record.message &&
            record.time - m_last->time <= repeatWindow)
        {
            ++m_last->repeats;
            m_last->time = record.time;
            return;
        }
        writeRepeats();
        m_last = Last{.level = record.level, .category = std::string(record.category), .message = std::string(record.message),
                      .time = record.time};
        // Messages from other languages, such as C#, come without a C++ location.
        const std::string place = record.level >= LogLevel::Error && record.location.line() != 0
                                      ? std::format(" ({}:{})", fileName(record.location.file_name()), record.location.line())
                                      : std::string();
        writeLine(record.level, record.time, record.category, record.message, place);
    }

    void writeRepeats()
    {
        if (m_last && m_last->repeats > 0)
        {
            writeLine(m_last->level, m_last->time, m_last->category,
                      std::format("(the same, {} more time{})", m_last->repeats, m_last->repeats == 1 ? "" : "s"), {});
            m_last->repeats = 0;
        }
    }

    // The last message of the console, and how many times it came again since.
    struct Last
    {
        LogLevel level = LogLevel::Info;
        std::string category;
        std::string message;
        std::chrono::steady_clock::time_point time;
        std::uint32_t repeats = 0;
    };

    std::chrono::steady_clock::time_point m_startTime;
    bool m_colors = false;
    std::atomic<LogLevel> m_level{defaultLogLevel};
    std::atomic<LogLevel> m_minimum{defaultLogLevel};
    std::atomic<bool> m_hasCategoryLevels{false};
    mutable std::shared_mutex m_levelsMutex;
    std::vector<CategoryLevel> m_categoryLevels;
    std::mutex m_mutex;
    std::vector<SinkEntry> m_sinks;
    std::uint32_t m_lastSinkId = 0;
    std::optional<Last> m_last;
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

std::optional<LogLevel> parseLogLevel(std::string_view name) noexcept
{
    for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error,
                                 LogLevel::Fatal, LogLevel::Off})
    {
        if (name == toString(level))
        {
            return level;
        }
    }
    if (name == "warn")
    {
        return LogLevel::Warning;
    }
    return std::nullopt;
}

std::string_view logCategoryOf(std::string_view file) noexcept
{
    // engine/src/<folder>/..., the headers in engine/include/devex/<folder>/..., the tests in
    // tests/<folder>/..., the programs in apps/<program>/...
    static constexpr std::string_view sources[] = {"engine", "src"};
    static constexpr std::string_view headers[] = {"engine", "include", "devex"};
    static constexpr std::string_view programs[] = {"apps"};
    static constexpr std::string_view tests[] = {"tests"};
    for (const std::span<const std::string_view> root : {std::span<const std::string_view>(sources), std::span<const std::string_view>(headers),
                                                         std::span<const std::string_view>(programs), std::span<const std::string_view>(tests)})
    {
        const std::size_t start = after(file, root);
        if (start == std::string_view::npos)
        {
            continue;
        }
        const std::string_view rest = file.substr(start);
        const std::size_t end = rest.find_first_of("/\\");
        // A file right in the folder belongs to the engine as a whole.
        return end == std::string_view::npos ? std::string_view("engine") : rest.substr(0, end);
    }
    return "game";
}

void setLogLevel(LogLevel level) noexcept
{
    logger().setLevel(level);
}

LogLevel logLevel() noexcept
{
    return logger().level();
}

void setLogLevel(std::string_view category, LogLevel level)
{
    logger().setCategoryLevel(category, level);
}

LogLevel logLevel(std::string_view category) noexcept
{
    return logger().levelOf(category);
}

void clearLogCategoryLevels()
{
    logger().clearCategoryLevels();
}

bool isLogLevelEnabled(LogLevel level) noexcept
{
    return level != LogLevel::Off && level >= logger().minimum();
}

bool isLogEnabled(LogLevel level, std::string_view category) noexcept
{
    return isLogLevelEnabled(level) && level >= logger().levelOf(category);
}

std::optional<std::string> configureLog(std::string_view settings)
{
    // Everything is read before anything changes: a mistake leaves the levels as they were.
    std::optional<LogLevel> all;
    std::vector<std::pair<std::string_view, LogLevel>> categories;
    while (!settings.empty())
    {
        const std::size_t comma = settings.find(',');
        const std::string_view item = trimmed(settings.substr(0, comma));
        settings = comma == std::string_view::npos ? std::string_view() : settings.substr(comma + 1);
        if (item.empty())
        {
            continue;
        }
        const std::size_t equals = item.find('=');
        const std::string_view name = trimmed(item.substr(equals == std::string_view::npos ? 0 : equals + 1));
        const std::optional<LogLevel> level = parseLogLevel(name);
        if (!level)
        {
            return std::format("'{}' is not a log level: trace, debug, info, warning, error, fatal or off", name);
        }
        if (equals == std::string_view::npos)
        {
            all = level;
            continue;
        }
        const std::string_view category = trimmed(item.substr(0, equals));
        if (category.empty())
        {
            return std::format("'{}' names no category before its level", item);
        }
        categories.emplace_back(category, *level);
    }
    clearLogCategoryLevels();
    if (all)
    {
        setLogLevel(*all);
    }
    for (const auto& [category, level] : categories)
    {
        setLogLevel(category, level);
    }
    return std::nullopt;
}

std::chrono::steady_clock::time_point logStartTime() noexcept
{
    return logger().startTime();
}

LogSinkId addLogSink(LogSink sink)
{
    return logger().addSink(std::move(sink));
}

void removeLogSink(LogSinkId id)
{
    logger().removeSink(id);
}

void logMessage(LogLevel level, std::string_view message, std::source_location location, std::string_view category)
{
    if (category.empty())
    {
        category = logCategoryOf(location.file_name());
    }
    if (!isLogEnabled(level, category))
    {
        return;
    }
    logger().write({level, message, location, std::chrono::steady_clock::now(), category});
}

} // namespace devex::core
