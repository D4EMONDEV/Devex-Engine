#include <devex/core/Log.hpp>
#include <devex/core/LogFile.hpp>
#include <devex/platform/CrashHandler.hpp>
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

// devex-editor [project.dvxproj]: opens the project, or the welcome screen without one.
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
    if (argc > 1)
    {
        // Arguments come in the system code page, which std::filesystem::path converts.
        config.project = std::filesystem::path(argv[1]);
    }

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
    return devex::runtime::run<EditorApplication>(config);
}
