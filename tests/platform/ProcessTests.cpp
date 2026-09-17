#include <devex/platform/Process.hpp>
#include <devex/platform/SharedLibrary.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

// Reads the output of a process until it ends.
[[nodiscard]] std::vector<std::string> readUntilExit(devex::platform::Process& process, int& exitCode)
{
    std::vector<std::string> lines;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const std::optional<int> code = process.exitCode();
        for (std::string& line : process.readLines())
        {
            lines.push_back(std::move(line));
        }
        if (code)
        {
            for (std::string& line : process.readLines())
            {
                lines.push_back(std::move(line));
            }
            exitCode = *code;
            return lines;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL("the process did not end");
    return lines;
}

} // namespace

#ifdef _WIN32
TEST_CASE("Processes run in the background with their output captured by line", "[platform][process]")
{
    const std::array<std::string, 4> arguments{"cmd.exe", "/d", "/c", "echo first& echo second 1>&2& exit /b 3"};
    auto process = devex::platform::Process::start(arguments);
    REQUIRE(process.has_value());
    int exitCode = 0;
    const std::vector<std::string> lines = readUntilExit(*process, exitCode);
    CHECK(exitCode == 3);
    // Standard error is merged into standard output.
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "first");
    CHECK(lines[1].starts_with("second"));
}

TEST_CASE("Processes run in a working directory", "[platform][process]")
{
    const std::array<std::string, 4> arguments{"cmd.exe", "/d", "/c", "cd"};
    auto process = devex::platform::Process::start(arguments, "C:\\Windows");
    REQUIRE(process.has_value());
    int exitCode = -1;
    const std::vector<std::string> lines = readUntilExit(*process, exitCode);
    CHECK(exitCode == 0);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.front() == "C:\\Windows");
}

TEST_CASE("Shared libraries export functions and know their addresses", "[platform][library]")
{
    CHECK_FALSE(devex::platform::SharedLibrary::load("devex-missing-library.dll").has_value());
    auto library = devex::platform::SharedLibrary::load("kernel32.dll");
    REQUIRE(library.has_value());
    const void* const function = library->function("GetTickCount64");
    CHECK(function != nullptr);
    CHECK(library->function("NoSuchFunction") == nullptr);
    CHECK(library->contains(function));
    // An address of this test program is not inside the library.
    static const int local = 0;
    CHECK_FALSE(library->contains(&local));
}
#endif

TEST_CASE("Processes need a program", "[platform][process]")
{
    CHECK_FALSE(devex::platform::Process::start({}).has_value());
}
