#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Executable.hpp>
#include <devex/platform/Process.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#ifdef _WIN32
namespace {

// Runs the probe until it dies, and answers its exit code.
[[nodiscard]] int runProbe(const std::filesystem::path& folder, const std::string& how)
{
    const std::array<std::string, 3> arguments{DEVEX_TEST_CRASH_PROBE, devex::core::toUtf8(folder), how};
    auto process = devex::platform::Process::start(arguments);
    REQUIRE(process.has_value());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline)
    {
        static_cast<void>(process->readLines());
        if (const std::optional<int> code = process->exitCode())
        {
            return *code;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FAIL("the probe did not end");
    return 0;
}

[[nodiscard]] bool hasMinidump(const std::filesystem::path& folder)
{
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error))
    {
        if (entry.path().extension() == ".dmp" && entry.file_size(error) > 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("A crash leaves its report in the log and a minidump beside it", "[platform][crash]")
{
    for (const std::string how : {"access", "abort"})
    {
        CAPTURE(how);
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("devex-crash-probe-" + how);
        std::error_code error;
        std::filesystem::remove_all(folder, error);

        CHECK(runProbe(folder, how) != 0);

        const devex::core::Result<std::string> log = devex::core::readTextFile(folder / "probe.log");
        REQUIRE(log.has_value());
        // What the process did before, then what killed it and where.
        CHECK(log->find("The probe is about to crash") != std::string::npos);
        CHECK(log->find("The process crashed") != std::string::npos);
        CHECK(log->find(how == "access" ? "access violation" : "abort") != std::string::npos);
        // The symbols sit beside the probe: the function that crashed is named.
        CHECK(log->find("crashHere") != std::string::npos);
        CHECK(log->find("Minidump: ") != std::string::npos);
        CHECK(hasMinidump(folder));

        std::filesystem::remove_all(folder, error);
    }
}

TEST_CASE("An executable becomes a windowed application", "[platform][executable]")
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "devex-windowed";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path copy = folder / "Game.exe";
    REQUIRE(std::filesystem::copy_file(DEVEX_TEST_CRASH_PROBE, copy, error));

    // The probe is a console program, as the player is.
    REQUIRE(devex::platform::opensConsole(copy).has_value());
    CHECK(*devex::platform::opensConsole(copy));
    REQUIRE(devex::platform::setWindowedApplication(copy).has_value());
    CHECK_FALSE(*devex::platform::opensConsole(copy));

    // A file that is not an executable is refused rather than written into.
    REQUIRE(devex::core::writeTextFile(folder / "notes.exe", "not a program").has_value());
    CHECK_FALSE(devex::platform::opensConsole(folder / "notes.exe").has_value());
    CHECK_FALSE(devex::platform::setWindowedApplication(folder / "notes.exe").has_value());

    std::filesystem::remove_all(folder, error);
}
#endif
