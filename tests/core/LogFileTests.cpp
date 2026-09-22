#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/LogFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

TEST_CASE("A log file keeps the run before it, and every line reaches the disk at once",
          "[core][log]")
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "devex-log-file";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    const std::filesystem::path path = folder / "logs" / "run.log";

    {
        auto log = devex::core::LogFile::open(path);
        REQUIRE(log.has_value());
        DEVEX_LOG_INFO("the first run");
    }
    {
        auto log = devex::core::LogFile::open(path);
        REQUIRE(log.has_value());
        CHECK((*log)->path() == path);
        // A line is on the disk while the file is still open.
        DEVEX_LOG_WARNING("the second run");
        const devex::core::Result<std::string> open = devex::core::readTextFile(path);
        REQUIRE(open.has_value());
        CHECK(open->starts_with("Devex Engine"));
        CHECK(open->find("warning the second run") != std::string::npos);
        (*log)->writeRaw("a report written as it is\n");
    }

    const devex::core::Result<std::string> current = devex::core::readTextFile(path);
    const devex::core::Result<std::string> previous =
        devex::core::readTextFile(folder / "logs" / "run.previous.log");
    REQUIRE(current.has_value());
    REQUIRE(previous.has_value());
    CHECK(current->find("the first run") == std::string::npos);
    CHECK(current->find("a report written as it is") != std::string::npos);
    CHECK(previous->find("info    the first run") != std::string::npos);

    // Once closed, the file no longer receives the log.
    DEVEX_LOG_WARNING("after the file");
    CHECK(devex::core::readTextFile(path)->find("after the file") == std::string::npos);

    std::filesystem::remove_all(folder, error);
}
