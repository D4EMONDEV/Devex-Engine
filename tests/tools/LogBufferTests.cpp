#include <devex/core/Log.hpp>
#include <devex/tools/LogBuffer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("The log buffer keeps the most recent messages", "[tools][log]")
{
    const devex::core::LogLevel previousLevel = devex::core::logLevel();
    devex::core::setLogLevel(devex::core::LogLevel::Info);
    {
        devex::tools::LogBuffer buffer(3);
        for (int index = 1; index <= 5; ++index)
        {
            DEVEX_LOG_INFO("message {}", index);
        }
        DEVEX_LOG_DEBUG("filtered out by the log level");

        std::vector<std::string> messages;
        buffer.forEach([&messages](const devex::tools::LogEntry& entry) {
            messages.push_back(entry.message);
        });
        CHECK(messages == std::vector<std::string>{"message 3", "message 4", "message 5"});

        buffer.clear();
        CHECK(buffer.size() == 0);
    }
    // The buffer no longer listens once destroyed.
    DEVEX_LOG_INFO("after destruction");
    devex::core::setLogLevel(previousLevel);
}
