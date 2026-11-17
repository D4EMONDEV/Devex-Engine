#include <devex/core/Uuid.hpp>
#include <devex/platform/ExportTools.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>
#include <devex/platform/SharedLibrary.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
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

TEST_CASE("A supervised process accepts commands and tolerates a closed command pipe",
          "[platform][process][interactive]")
{
#ifdef _WIN32
    const std::vector<std::string> command{"cmd.exe", "/d", "/v:on", "/c",
                                           "set /p reply=& echo !reply!"};
#else
    const std::vector<std::string> command{"/bin/sh", "-c",
                                           "IFS= read -r reply; printf '%s\\n' \"$reply\""};
#endif
    auto process = devex::platform::Process::startInteractive(command);
    REQUIRE(process);
    CHECK(process->id() != 0);
    CHECK(process->id() != devex::platform::currentProcessId());
    CHECK_FALSE(process->sendLine("one\ntwo"));
    REQUIRE(process->sendLine("hello from the editor"));
    int code = -1;
    const auto lines = readUntilExit(*process, code);
    REQUIRE(code == 0);
    REQUIRE(lines.size() == 1);
    CHECK(lines.front() == "hello from the editor");
    // On POSIX a broken pipe would otherwise send SIGPIPE to the editor itself.
    CHECK_FALSE(process->sendLine("after exit"));
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

TEST_CASE("The tools that the editor settings choose pass before those found elsewhere", "[platform][export]")
{
    namespace platform = devex::platform;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("devex-tools-" + devex::core::Uuid::generate().toString());
    const auto write = [](const std::filesystem::path& file, std::string_view text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << text;
    };
#if defined(_WIN32)
    const std::string java = "java.exe";
#else
    const std::string java = "java";
#endif
    const platform::ExportToolPaths before = platform::exportToolPaths();
    const platform::ExportToolPaths chosen{.androidSdk = root / "sdk", .androidNdk = root / "ndk", .java = root / "jdk", .emsdk = root / "emsdk"};
    // Kept as the settings of the editor keep them, without the empty ones.
    CHECK(platform::readExportToolPaths(platform::writeExportToolPaths(chosen)) == chosen);
    CHECK(platform::writeExportToolPaths({}).attributes.empty());

    write(root / "sdk" / "platform-tools" / "adb", "");
    write(root / "ndk" / "build" / "cmake" / "android.toolchain.cmake", "");
    write(root / "jdk" / "bin" / java, "");
    write(root / "emsdk" / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake", "");
    write(root / "emsdk" / "upstream" / "emscripten" / "emscripten-version.txt", "\"7.0.0\"\n");
    platform::setExportToolPaths(chosen);
    CHECK(platform::findAndroidSdk() == chosen.androidSdk);
    CHECK(platform::findAndroidNdk(chosen.androidSdk) == chosen.androidNdk);
    CHECK(platform::findJava() == chosen.java / "bin" / java);
    CHECK(platform::emsdkCandidates() == std::vector{chosen.emsdk});
    CHECK(platform::emscriptenVersion(chosen.emsdk) == "7.0.0");

    // A folder chosen that is not the tool is not looked past: the exports say so.
    platform::setExportToolPaths({.androidSdk = root / "ndk", .androidNdk = root / "sdk", .java = root / "sdk"});
    CHECK(platform::findAndroidSdk().empty());
    CHECK(platform::findAndroidNdk({}).empty());
    CHECK(platform::findJava().empty());
    CHECK_FALSE(platform::emscriptenVersion(root / "sdk").has_value());

    platform::setExportToolPaths(before);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("The files that a game keeps are there before it starts", "[platform][user]")
{
    // A browser loads them from IndexedDB first; a program run without its page, or a system, has them at once.
    bool ready = false;
    devex::platform::restoreUserFiles(std::filesystem::temp_directory_path() / "devex-user-files", [&ready] { ready = true; });
    CHECK(ready);
    ready = false;
    devex::platform::restoreUserFiles({}, [&ready] { ready = true; });
    CHECK(ready);
    // Nothing to write back where nothing was restored.
    devex::platform::persistUserFiles();
}
