#include <devex/core/Log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
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
    std::string category;
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
            m_records.push_back({record.level, std::string(record.message), record.location.line(), std::string(record.category)});
        });
    }

    ~LogCapture()
    {
        devex::core::removeLogSink(m_sink);
        devex::core::clearLogCategoryLevels();
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

TEST_CASE("Messages are filed under the part of the engine they come from, or the one they name", "[core][log]")
{
    using devex::core::logCategoryOf;
    CHECK(logCategoryOf("/home/me/Devex-Engine/engine/src/render/vulkan/VulkanRenderer.cpp") == "render");
    CHECK(logCategoryOf("D:\\a\\Devex-Engine\\engine\\src\\asset\\import\\Importer.cpp") == "asset");
    CHECK(logCategoryOf("/src/Devex-Engine/engine/include/devex/scene/Scene.hpp") == "scene");
    CHECK(logCategoryOf("/src/Devex-Engine/engine/src/EngineLibrary.cpp") == "engine");
    CHECK(logCategoryOf("/src/Devex-Engine/apps/editor/src/main.cpp") == "editor");
    // The code of a project, and messages without a place such as those of C#.
    CHECK(logCategoryOf("/home/me/MyGame/code/Player.cpp") == "game");
    CHECK(logCategoryOf("") == "game");
    CHECK(logCategoryOf("/home/me/my-engine/src/Thing.cpp") == "game");

    const LogCapture capture(LogLevel::Trace);
    DEVEX_LOG_INFO("filed by its folder");
    DEVEX_LOG_INFO_IN("build", "filed by the call");
    REQUIRE(capture.records().size() == 2);
    CHECK(capture.records()[0].category == "core");
    CHECK(capture.records()[1].category == "build");
}

TEST_CASE("A category can keep more or less than the others", "[core][log]")
{
    const LogCapture capture(LogLevel::Info);
    devex::core::setLogLevel("build", LogLevel::Warning);
    devex::core::setLogLevel("core", LogLevel::Trace);
    int evaluations = 0;

    DEVEX_LOG_DEBUG("{}", ++evaluations);
    DEVEX_LOG_INFO_IN("build", "{}", ++evaluations);
    DEVEX_LOG_WARNING_IN("build", "{}", ++evaluations);
    DEVEX_LOG_DEBUG_IN("render", "{}", ++evaluations);

    // Kept: the debug message of the core, the warning of the build; the others are not even formatted.
    CHECK(evaluations == 2);
    REQUIRE(capture.records().size() == 2);
    CHECK(capture.records()[0].message == "1");
    CHECK(capture.records()[1].message == "2");
    CHECK(devex::core::logLevel("render") == LogLevel::Info);
    CHECK(devex::core::logLevel("build") == LogLevel::Warning);
}

TEST_CASE("The levels are read from a text such as DEVEX_LOG gives", "[core][log]")
{
    const LogCapture capture(LogLevel::Info);

    CHECK_FALSE(devex::core::configureLog(" warning , render = trace,build=debug").has_value());
    CHECK(devex::core::logLevel() == LogLevel::Warning);
    CHECK(devex::core::logLevel("render") == LogLevel::Trace);
    CHECK(devex::core::logLevel("build") == LogLevel::Debug);
    CHECK(devex::core::logLevel("scene") == LogLevel::Warning);
    CHECK(devex::core::isLogLevelEnabled(LogLevel::Trace));
    CHECK_FALSE(devex::core::isLogEnabled(LogLevel::Info, "scene"));

    // A new text replaces the levels of the categories; a mistake changes nothing.
    CHECK_FALSE(devex::core::configureLog("scene=error").has_value());
    CHECK(devex::core::logLevel() == LogLevel::Warning);
    CHECK(devex::core::logLevel("render") == LogLevel::Warning);
    CHECK(devex::core::logLevel("scene") == LogLevel::Error);
    const std::optional<std::string> mistake = devex::core::configureLog("info,render=loud");
    REQUIRE(mistake.has_value());
    CHECK(mistake->find("loud") != std::string::npos);
    CHECK(devex::core::configureLog("=debug").has_value());
    CHECK(devex::core::logLevel() == LogLevel::Warning);
    CHECK(devex::core::logLevel("scene") == LogLevel::Error);
    CHECK(devex::core::parseLogLevel("warn") == LogLevel::Warning);
}
