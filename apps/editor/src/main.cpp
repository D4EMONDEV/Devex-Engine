#include <devex/platform/Platform.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/runtime/GameExport.hpp>

#include <filesystem>
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
    return devex::runtime::run<EditorApplication>(config);
}
