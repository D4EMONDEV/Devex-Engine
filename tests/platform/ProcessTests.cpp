#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>
#include <devex/platform/SharedLibrary.hpp>

#include <catch2/catch_test_macros.hpp>

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

// A command that writes a line to each output and exits with 3, one that writes its working
// directory, and a library of the system with one of its functions.
#ifdef _WIN32
const std::vector<std::string> twoLines{"cmd.exe", "/d", "/c", "echo first& echo second 1>&2& exit /b 3"};
const std::vector<std::string> workingDirectory{"cmd.exe", "/d", "/c", "cd"};
constexpr const char* systemDirectory = "C:\\Windows";
constexpr const char* systemLibrary = "kernel32.dll";
constexpr const char* systemFunction = "GetTickCount64";
constexpr const char* missingLibrary = "devex-missing-library.dll";
#else
const std::vector<std::string> twoLines{"/bin/sh", "-c", "echo first; echo second 1>&2; exit 3"};
const std::vector<std::string> workingDirectory{"/bin/sh", "-c", "pwd"};
constexpr const char* systemDirectory = "/usr";
constexpr const char* systemLibrary = "libc.so.6";
constexpr const char* systemFunction = "getpid";
constexpr const char* missingLibrary = "libdevex-missing-library.so";
#endif

TEST_CASE("Processes run in the background with their output captured by line", "[platform][process]")
{
    auto process = devex::platform::Process::start(twoLines);
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
    auto process = devex::platform::Process::start(workingDirectory, systemDirectory);
    REQUIRE(process.has_value());
    int exitCode = -1;
    const std::vector<std::string> lines = readUntilExit(*process, exitCode);
    CHECK(exitCode == 0);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.front() == systemDirectory);
}

TEST_CASE("Shared libraries export functions and know their addresses", "[platform][library]")
{
    CHECK_FALSE(devex::platform::SharedLibrary::load(missingLibrary).has_value());
    auto library = devex::platform::SharedLibrary::load(systemLibrary);
    REQUIRE(library.has_value());
    const void* const function = library->function(systemFunction);
    CHECK(function != nullptr);
    CHECK(library->function("NoSuchFunction") == nullptr);
    CHECK(library->contains(function));
    // An address of this test program is not inside the library.
    static const int local = 0;
    CHECK_FALSE(library->contains(&local));
}

TEST_CASE("Processes need a program", "[platform][process]")
{
    CHECK_FALSE(devex::platform::Process::start({}).has_value());
}

TEST_CASE("The folder of the player is found without being made", "[platform][user]")
{
    const std::string game = "Devex test " + devex::core::Uuid::generate().toString();
    const devex::core::Result<std::filesystem::path> folder = devex::platform::userDataLocation("", game + " <:?>");
    REQUIRE(folder.has_value());
    // The characters a file name cannot hold are left out.
    CHECK(folder->filename() == std::filesystem::path(game));
    CHECK_FALSE(std::filesystem::exists(*folder));
    const devex::core::Result<std::filesystem::path> company = devex::platform::userDataLocation("Studio", game);
    REQUIRE(company.has_value());
    CHECK(company->parent_path().filename() == "Studio");
}
