#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/runtime/SystemReport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <string>

TEST_CASE("A system report says what a bug report needs", "[runtime]")
{
    const std::filesystem::path logs = std::filesystem::temp_directory_path() / ("devex-report-" + devex::core::Uuid::generate().toString());
    const auto report = [&logs] { return devex::runtime::systemReport("System: a machine\n", "A GPU\n", logs); };

    SECTION("Without a log")
    {
        const std::string text = report();
        CHECK(text.find(devex::core::version()) != std::string::npos);
        CHECK(text.find("System: a machine") != std::string::npos);
        CHECK(text.find("A GPU") != std::string::npos);
        CHECK(text.find("dotnet: ") != std::string::npos);
        CHECK(text.find("No log yet") != std::string::npos);
    }
    SECTION("It keeps the last problems of the log, and names the crashes")
    {
        std::string log = "[    0.100] info    runtime   all is well\n"
                          "[    0.200] warning render    the first warning\n";
        for (int index = 0; index < 40; ++index)
        {
            log += std::format("[    1.000] error   render    problem number {}\n", index);
        }
        REQUIRE(devex::core::writeTextFile(logs / "editor.log", log));
        REQUIRE(devex::core::writeTextFile(logs / "crash-2026-11-06-120000.txt", "stack"));
        const std::string text = report();
        CHECK(text.find("all is well") == std::string::npos);
        CHECK(text.find("the first warning") == std::string::npos);
        CHECK(text.find("problem number 39") != std::string::npos);
        CHECK(text.find("problem number 9\n") == std::string::npos);
        CHECK(text.find("crash-2026-11-06-120000.txt") != std::string::npos);
    }
    std::error_code ignored;
    std::filesystem::remove_all(logs, ignored);
}
