#include <devex/core/Log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using devex::core::LogLevel;
using devex::core::LogRecord;

namespace {

struct CapturedRecord
{
    LogLevel level;
    std::string message;
    std::uint_least32_t line;
};

// Records every message reaching the logger and restores the log level afterwards.
class LogCapture
{
public:
    explicit LogCapture(LogLevel level)
        : m_previousLevel(devex::core::logLevel())
    {
        devex::core::setLogLevel(level);
        m_sink = devex::core::addLogSink([this](const LogRecord& record) {
            m_records.push_back({record.level, std::string(record.message), record.location.line()});
        });
    }

    ~LogCapture()
    {
        devex::core::removeLogSink(m_sink);
        devex::core::setLogLevel(m_previousLevel);
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] const std::vector<CapturedRecord>& records() const noexcept
    {
        return m_records;
    }

private:
    LogLevel m_previousLevel;
    devex::core::LogSinkId m_sink{};
    std::vector<CapturedRecord> m_records;
};

} // namespace

TEST_CASE("Log macros format the message and record the call site", "[core][log]")
{
    const LogCapture capture(LogLevel::Trace);

    const auto expectedLine = static_cast<std::uint_least32_t>(__LINE__ + 1);
    DEVEX_LOG_INFO("{} + {} = {}", 1, 2, 3);

    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].level == LogLevel::Info);
    CHECK(capture.records()[0].message == "1 + 2 = 3");
    CHECK(capture.records()[0].line == expectedLine);
}

TEST_CASE("Messages below the log level are not evaluated", "[core][log]")
{
    const LogCapture capture(LogLevel::Warning);
    int evaluations = 0;

    DEVEX_LOG_DEBUG("{}", ++evaluations);
    DEVEX_LOG_WARNING("{}", ++evaluations);

    CHECK(evaluations == 1);
    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].level == LogLevel::Warning);
    CHECK(capture.records()[0].message == "1");
}

TEST_CASE("Off disables every level", "[core][log]")
{
    const LogCapture capture(LogLevel::Off);

    DEVEX_LOG_FATAL("hidden");

    CHECK(capture.records().empty());
    CHECK_FALSE(devex::core::isLogLevelEnabled(LogLevel::Off));
}

TEST_CASE("Removed sinks stop receiving messages", "[core][log]")
{
    const LogCapture capture(LogLevel::Info);
    int received = 0;
    const devex::core::LogSinkId sink =
        devex::core::addLogSink([&received](const LogRecord&) { ++received; });

    DEVEX_LOG_INFO("first");
    devex::core::removeLogSink(sink);
    DEVEX_LOG_INFO("second");

    CHECK(received == 1);
    CHECK(capture.records().size() == 2);
}
