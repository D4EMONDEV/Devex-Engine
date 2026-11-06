#include <devex/runtime/SystemReport.hpp>

#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/runtime/Application.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <system_error>
#include <vector>

namespace devex::runtime {
namespace {

constexpr std::size_t problemsShown = 30;
constexpr std::size_t crashesShown = 5;

// Whether a program can be started by its name. Where it is stays out of the report: paths often say who the user is.
[[nodiscard]] bool isOnPath(std::string_view program)
{
#ifdef _WIN32
    constexpr char separator = ';';
    constexpr std::array suffixes{".exe", ".cmd", ".bat"};
#else
    constexpr char separator = ':';
    constexpr std::array suffixes{""};
#endif
    const std::string path = platform::environmentVariable("PATH");
    std::error_code error;
    for (std::size_t start = 0; start <= path.size();)
    {
        const std::size_t end = std::min(path.find(separator, start), path.size());
        if (end > start)
        {
            const std::filesystem::path folder = core::pathFromUtf8(path.substr(start, end - start));
            for (const char* const suffix : suffixes)
            {
                if (std::filesystem::is_regular_file(folder / core::pathFromUtf8(std::string(program) + suffix), error))
                {
                    return true;
                }
            }
        }
        start = end + 1;
    }
    return false;
}

[[nodiscard]] std::string gameCodeTools()
{
    std::string report;
#ifdef _WIN32
    constexpr std::array programs{"dotnet", "cmake", "ninja"};
    std::error_code error;
    const std::string programFiles = platform::environmentVariable("ProgramFiles(x86)");
    report += std::format("Visual Studio installer: {}\n",
                          std::filesystem::is_regular_file(core::pathFromUtf8(programFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe", error) ? "found" : "missing");
#else
    constexpr std::array programs{"dotnet", "cmake", "ninja", "g++-14", "g++"};
#endif
    for (const char* const program : programs)
    {
        report += std::format("{}: {}\n", program, isOnPath(program) ? "found" : "missing");
    }
    return report;
}

// The warnings and errors the log kept, the last ones. Its lines read "[    1.234] error   render    message".
[[nodiscard]] std::string recentProblems(const std::filesystem::path& logs)
{
    const core::Result<std::string> text = core::readTextFile(logs / "editor.log");
    if (!text)
    {
        return "No log yet.\n";
    }
    std::vector<std::string_view> problems;
    const std::string_view all(*text);
    for (std::size_t start = 0; start < all.size();)
    {
        const std::size_t end = std::min(all.find('\n', start), all.size());
        std::string_view line = all.substr(start, end - start);
        start = end + 1;
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        const std::size_t close = line.find("] ");
        if (close == std::string_view::npos)
        {
            continue;
        }
        const std::string_view level = line.substr(close + 2);
        if (level.starts_with("warning") || level.starts_with("error") || level.starts_with("fatal"))
        {
            problems.push_back(line);
        }
    }
    if (problems.empty())
    {
        return "None.\n";
    }
    std::string report;
    for (std::size_t index = problems.size() > problemsShown ? problems.size() - problemsShown : 0; index < problems.size(); ++index)
    {
        report += std::format("{}\n", problems[index]);
    }
    return report;
}

// A crash leaves its report and its minidump beside the log.
[[nodiscard]] std::string crashes(const std::filesystem::path& logs)
{
    std::vector<std::string> names;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(logs, error), end; !error && entry != end; entry.increment(error))
    {
        std::string name = core::toUtf8(entry->path().filename());
        if (name.starts_with("crash-"))
        {
            names.push_back(std::move(name));
        }
    }
    if (names.empty())
    {
        return "None.\n";
    }
    std::ranges::sort(names);
    std::string report;
    for (std::size_t index = names.size() > crashesShown ? names.size() - crashesShown : 0; index < names.size(); ++index)
    {
        report += std::format("{}\n", names[index]);
    }
    return report;
}

} // namespace

std::string systemReport(std::string_view machine, std::string_view graphics, const std::filesystem::path& logs)
{
    return std::format("Devex Engine {}\n\n== Machine ==\n{}\n== Graphics ==\n{}\n== Tools for game code ==\n{}\n== Last problems in the log ==\n{}\n== Crashes ==\n{}",
                       core::version(), machine, graphics, gameCodeTools(), recentProblems(logs), crashes(logs));
}

int reportFromCommandLine(std::span<const std::string_view> arguments)
{
    // The report says what went wrong: the log has no more to add while it is being made.
    configureLogging("error");
    const core::Result<std::filesystem::path> user = platform::userDataDirectory("Devex", "Editor");
    const std::filesystem::path logs = user ? *user / "logs" : std::filesystem::path{};

    std::string machine;
    std::string graphics;
    auto created = platform::Platform::create();
    if (!created)
    {
        machine = std::format("The video system does not start: {}\n", created.error());
        graphics = "Not tried, without a video system.\n";
    }
    else
    {
        machine = created->describeMachine();
        auto window = created->createWindow({.title = "Devex report", .width = 64, .height = 64, .vulkan = true, .hidden = true});
        if (!window)
        {
            graphics = std::format("A window for Vulkan does not open: {}\n", window.error());
        }
        else if (const core::Result<std::string> described = render::Renderer::describeGraphicsWithoutRenderer(*created, *window))
        {
            graphics = *described;
        }
        else
        {
            graphics = std::format("Vulkan does not start: {}\n", described.error());
        }
    }

    const std::string report = systemReport(machine, graphics, logs);
    std::fputs(report.c_str(), stdout);
    const std::filesystem::path file = arguments.size() > 1 ? core::pathFromUtf8(std::string(arguments[1])) : logs / "system-report.txt";
    if (const core::Result<void> written = core::writeTextFile(file, report); !written)
    {
        std::fprintf(stderr, "Cannot write the report: %s\n", written.error().message.c_str());
        return 1;
    }
    std::printf("\nWritten to %s\n", core::toUtf8(file).c_str());
    return 0;
}

} // namespace devex::runtime
