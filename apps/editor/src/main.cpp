#include <devex/runtime/Application.hpp>

#include <filesystem>

namespace {

// The editor without game code: scenes are edited and played with the engine's components only.
// Games run inside the editor with their own code by setting ApplicationConfig::editor.
class EditorApplication final : public devex::runtime::Application
{
};

} // namespace

// devex-editor [project.dvxproj]: opens the project, or the welcome screen without one.
int main(int argc, char** argv)
{
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
