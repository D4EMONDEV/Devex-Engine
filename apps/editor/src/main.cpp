#include <devex/core/Log.hpp>
#include <devex/core/LogFile.hpp>
#include <devex/platform/CrashHandler.hpp>
#include <devex/platform/DesktopEntry.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/runtime/GameExport.hpp>

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace {

// The editor without game code: scenes are edited and played with the engine's components only.
// Games run inside the editor with their own code by setting ApplicationConfig::editor.
class EditorApplication final : public devex::runtime::Application
{
};

} // namespace

// devex-editor [project.dvxproj] [--screenshot <picture.png>] [--profile <report.txt>]
// [--log-level <levels>]: opens the project, or the welcome screen without one; with --profile,
// writes where the time of a frame goes once the project is drawn, with --screenshot a picture of the
// window, and quits. --log-level sets what the log keeps, as the DEVEX_LOG variable does: "debug",
// or "info,build=warning,render=trace".
// devex-editor --export <project.dvxproj> [--output <folder>] [--configuration <Release|Debug>]:
// exports the game without opening a window.
int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--export")
    {
        const std::vector<std::string_view> arguments(argv + 1, argv + argc);
        return devex::runtime::exportFromCommandLine(arguments, devex::platform::executableDirectory());
    }
    devex::runtime::ApplicationConfig config{
        .title = "Devex Editor",
        .width = 1600,
        .height = 900,
        .editor = true,
    };
    // Arguments come in the system code page, which std::filesystem::path converts.
    std::string_view logLevels;
    for (int index = 1; index < argc; ++index)
    {
        if (std::string_view(argv[index]) == "--log-level" && index + 1 < argc)
        {
            logLevels = argv[++index];
        }
        else if (std::string_view(argv[index]) == "--screenshot" && index + 1 < argc)
        {
            config.screenshot = std::filesystem::path(argv[++index]);
        }
        else if (std::string_view(argv[index]) == "--profile" && index + 1 < argc)
        {
            config.profileReport = std::filesystem::path(argv[++index]);
        }
        else if (config.project.empty())
        {
            config.project = std::filesystem::path(argv[index]);
        }
    }

    devex::runtime::configureLogging(logLevels);

    // The log of the editor stays beside its settings, with the report and the minidump of a crash.
    std::unique_ptr<devex::core::LogFile> log;
    std::filesystem::path logs;
    if (const devex::core::Result<std::filesystem::path> user = devex::platform::userDataDirectory("Devex", "Editor"))
    {
        logs = *user / "logs";
        if (devex::core::Result<std::unique_ptr<devex::core::LogFile>> opened =
                devex::core::LogFile::open(logs / "editor.log"))
        {
            log = std::move(*opened);
        }
        else
        {
            DEVEX_LOG_WARNING("The log is not kept in a file: {}", opened.error());
        }
    }
    devex::platform::installCrashHandler({.directory = logs, .log = log.get()});

    // The dock of a Linux desktop shows the name and the icon of the editor's entry among the
    // applications, not those of its window.
    const devex::platform::DesktopEntry entry{
        .name = "Devex Engine",
        .comment = "Make 2D and 3D games",
        .icon = devex::platform::executableDirectory() / "resources" / "icons" / "devex.png",
        .categories = "Development;IDE;",
        .opensFiles = true,
    };
    if (const devex::core::Result<void> registered = devex::platform::registerDesktopEntry(entry); !registered)
    {
        DEVEX_LOG_WARNING("The editor is not listed among the applications of the desktop: {}", registered.error());
    }
    return devex::runtime::run<EditorApplication>(config);
}
