#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Executable.hpp>
#include <devex/platform/Process.hpp>

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(__linux__)
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

#ifdef _WIN32
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
#endif

} // namespace

// Linux writes no minidump: the system keeps a core dump if it is set to.
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
        // The symbols sit beside the probe, or in it: the function that crashed is named.
        CHECK(log->find("crashHere") != std::string::npos);
#ifdef _WIN32
        CHECK(log->find("Minidump: ") != std::string::npos);
        CHECK(hasMinidump(folder));
#endif

        std::filesystem::remove_all(folder, error);
    }
}

TEST_CASE("A guard turns a crash into its report, and the process lives on", "[platform][crash]")
{
    for (const std::string how : {"access", "abort", "exception"})
    {
        CAPTURE(how);
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("devex-crash-guard-" + how);
        std::error_code error;
        std::filesystem::remove_all(folder, error);

        CHECK(runProbe(folder, "guarded-" + how) == 0);

        const devex::core::Result<std::string> log = devex::core::readTextFile(folder / "probe.log");
        REQUIRE(log.has_value());
        // Twice, as the handlers are armed again after a crash they caught.
        const std::size_t first = log->find("Caught: ");
        REQUIRE(first != std::string::npos);
        CHECK(log->find("Caught: ", first + 1) != std::string::npos);
        CHECK(log->find(how == "access" ? "access violation" : how == "abort" ? "abort" : "thrown on purpose") !=
              std::string::npos);
        if (how != "exception")
        {
            CHECK(log->find("crashHere") != std::string::npos);
        }
        CHECK(log->find("The probe lives on") != std::string::npos);
        CHECK(log->find("The process crashed") == std::string::npos);
        std::filesystem::remove_all(folder, error);
    }

    // Under a barrier, the crash ends the process with its report.
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "devex-crash-guard-barrier";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    CHECK(runProbe(folder, "barrier") != 0);
    const devex::core::Result<std::string> log = devex::core::readTextFile(folder / "probe.log");
    REQUIRE(log.has_value());
    CHECK(log->find("The process crashed") != std::string::npos);
    CHECK(log->find("access violation") != std::string::npos);
    std::filesystem::remove_all(folder, error);
}
#endif

#ifdef _WIN32
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

namespace {

void appendLittle(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xFF));
    }
}

void setLittle(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFF);
    }
}

// The smallest 64-bit program with a section of resources: a manifest, and an icon to replace.
[[nodiscard]] std::vector<std::byte> smallExecutable()
{
    constexpr std::size_t coff = 0x44;
    constexpr std::size_t optional = coff + 20;
    constexpr std::size_t optionalSize = 240;
    constexpr std::size_t table = optional + optionalSize;
    constexpr std::size_t raw = 0x200;
    constexpr std::size_t address = 0x1000;
    std::vector<std::byte> bytes(raw + 0x200, std::byte{0});
    setLittle(bytes, 0, 0x5A4D, 2);
    setLittle(bytes, 60, 0x40, 4);
    setLittle(bytes, 0x40, 0x4550, 4);
    setLittle(bytes, coff, 0x8664, 2);
    setLittle(bytes, coff + 2, 1, 2);
    setLittle(bytes, coff + 16, optionalSize, 2);
    setLittle(bytes, optional, 0x20B, 2);
    setLittle(bytes, optional + 32, 0x1000, 4);
    setLittle(bytes, optional + 36, 0x200, 4);
    setLittle(bytes, optional + 56, 0x2000, 4);
    setLittle(bytes, optional + 60, raw, 4);
    setLittle(bytes, optional + 68, 3, 2);
    setLittle(bytes, optional + 108, 16, 4);
    setLittle(bytes, optional + 112 + 2 * 8, address, 4);
    setLittle(bytes, optional + 112 + 2 * 8 + 4, 0x200, 4);
    setLittle(bytes, table + 8, 0x200, 4);
    setLittle(bytes, table + 12, address, 4);
    setLittle(bytes, table + 16, 0x200, 4);
    setLittle(bytes, table + 20, raw, 4);
    // The tree: types 3 and 24, each with a name and a language, then what describes the data.
    std::vector<std::byte> tree;
    const auto directory = [&](std::uint16_t count) {
        appendLittle(tree, 0, 12);
        appendLittle(tree, 0, 2);
        appendLittle(tree, count, 2);
    };
    const auto entry = [&](std::uint32_t id, std::uint32_t target) {
        appendLittle(tree, id, 4);
        appendLittle(tree, target, 4);
    };
    constexpr std::uint32_t isDirectory = 0x80000000u;
    directory(2);
    entry(3, isDirectory | 0x20);
    entry(24, isDirectory | 0x38);
    directory(1);
    entry(1, isDirectory | 0x50);
    directory(1);
    entry(1, isDirectory | 0x68);
    directory(1);
    entry(0, 0x80);
    directory(1);
    entry(1033, 0x90);
    REQUIRE(tree.size() == 0x80);
    appendLittle(tree, address + 0xA0, 4);
    appendLittle(tree, 7, 4);
    appendLittle(tree, 0, 8);
    appendLittle(tree, address + 0xB0, 4);
    appendLittle(tree, 11, 4);
    appendLittle(tree, 1252, 4);
    appendLittle(tree, 0, 4);
    std::ranges::copy(tree, bytes.begin() + raw);
    const std::string oldIcon = "old ico";
    const std::string manifest = "<assembly/>";
    std::memcpy(bytes.data() + raw + 0xA0, oldIcon.data(), oldIcon.size());
    std::memcpy(bytes.data() + raw + 0xB0, manifest.data(), manifest.size());
    return bytes;
}

} // namespace

TEST_CASE("The icon of a Windows executable is written from any system", "[platform][executable]")
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("devex-icon-" + devex::core::Uuid::generate().toString());
    const std::filesystem::path file = folder / "Game.exe";
    std::filesystem::create_directories(folder);
    REQUIRE(devex::core::writeFileAtomically(file, smallExecutable()));
    auto before = devex::platform::readExecutableResources(file);
    REQUIRE(before);
    REQUIRE(before->size() == 2);
    CHECK(before->at(0).type == 3);
    CHECK(before->at(1).type == 24);
    CHECK(before->at(1).language == 1033);

    const std::vector<devex::platform::IconImage> images{
        {.size = 16, .rgba = std::vector<std::uint8_t>(16 * 16 * 4, 200)},
        {.size = 256, .rgba = std::vector<std::uint8_t>(256 * 256 * 4, 100), .png = {std::byte{1}, std::byte{2}}},
    };
    REQUIRE(devex::platform::setExecutableIcon(file, images));
    auto after = devex::platform::readExecutableResources(file);
    REQUIRE(after);
    REQUIRE(after->size() == 4);
    std::vector<devex::platform::ExecutableResource> icons;
    const devex::platform::ExecutableResource* group = nullptr;
    const devex::platform::ExecutableResource* manifest = nullptr;
    for (const devex::platform::ExecutableResource& resource : *after)
    {
        if (resource.type == 3)
        {
            icons.push_back(resource);
        }
        group = resource.type == 14 ? &resource : group;
        manifest = resource.type == 24 ? &resource : manifest;
    }
    // What was there stays, as it was.
    REQUIRE(manifest != nullptr);
    CHECK(manifest->language == 1033);
    CHECK(manifest->codePage == 1252);
    CHECK(std::string(reinterpret_cast<const char*>(manifest->data.data()), manifest->data.size()) == "<assembly/>");
    // The old icon is gone: a bitmap with its header, its pixels and its mask, and a PNG as it came.
    REQUIRE(icons.size() == 2);
    CHECK(icons[0].data.size() == 40 + 16 * 16 * 4 + 4 * 16);
    CHECK(icons[1].data.size() == 2);
    REQUIRE(group != nullptr);
    CHECK(group->data.size() == 6 + 2 * 14);
    CHECK(static_cast<std::uint8_t>(group->data[6]) == 16);
    // 256 is written as 0.
    CHECK(static_cast<std::uint8_t>(group->data[6 + 14]) == 0);
    // The header is still that of a program.
    CHECK(devex::platform::opensConsole(file).has_value());

    // A file that is not a program is refused.
    REQUIRE(devex::core::writeTextFile(folder / "notes.exe", "not a program"));
    CHECK_FALSE(devex::platform::setExecutableIcon(folder / "notes.exe", images));
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

#ifdef _WIN32
TEST_CASE("Windows reads the icon written into a real program, which still runs", "[platform][executable]")
{
    // Beside the program it copies, for the libraries it needs. A library missing would stop on a box of the system,
    // waiting for someone: this says to fail at once.
    SetErrorMode(SEM_FAILCRITICALERRORS);
    const std::filesystem::path probe = DEVEX_TEST_CRASH_PROBE;
    const std::filesystem::path copy = probe.parent_path() / ("devex-icon-" + devex::core::Uuid::generate().toString() + ".exe");
    std::error_code error;
    REQUIRE(std::filesystem::copy_file(probe, copy, error));
    const std::vector<devex::platform::IconImage> images{{.size = 32, .rgba = std::vector<std::uint8_t>(32 * 32 * 4, 180)}};
    REQUIRE(devex::platform::setExecutableIcon(copy, images));

    const HMODULE image = LoadLibraryExW(copy.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    REQUIRE(image != nullptr);
    const HRSRC group = FindResourceW(image, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14));
    REQUIRE(group != nullptr);
    CHECK(SizeofResource(image, group) == 6 + 14);
    const HRSRC icon = FindResourceW(image, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(3));
    REQUIRE(icon != nullptr);
    CHECK(SizeofResource(image, icon) == 40 + 32 * 32 * 4 + 4 * 32);
    FreeLibrary(image);

    // Without arguments, the probe says 2 from its main: Windows loaded it and ran it.
    const std::array<std::string, 1> arguments{devex::core::toUtf8(copy)};
    auto process = devex::platform::Process::start(arguments, probe.parent_path());
    REQUIRE(process);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::optional<int> code;
    while (!code && std::chrono::steady_clock::now() < deadline)
    {
        code = process->exitCode();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(code);
    CHECK(*code == 2);
    std::filesystem::remove(copy, error);
}
#endif
