#include "ToolsState.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// The code of the game in FileSystem, and new components created from the inspector.
namespace devex::tools::detail {
namespace {

inline constexpr const char* newScriptPopup = "New Script";

} // namespace

EntityIcon codeIcon(const std::filesystem::path& path)
{
    const ThemeColors& colors = themeColors();
    if (path.extension() == ".cs")
    {
        return {icons::FileCode, colors.gameCode};
    }
    if (path.extension() == ".cpp" || path.extension() == ".hpp" || path.extension() == ".h")
    {
        return {icons::Code, colors.entity};
    }
    return {icons::FileText, colors.neutral};
}

void openInCodeEditor(ToolsState& state, const std::filesystem::path& file)
{
    if (core::Result<void> opened = state.platform.openPath(file); !opened)
    {
        DEVEX_LOG_WARNING("Cannot open {}: {}", core::toUtf8(file.filename()), opened.error());
    }
}

void drawNewScriptPopup(ToolsState& state)
{
    if (std::exchange(state.openNewScriptPopup, false))
    {
        ImGui::OpenPopup(newScriptPopup);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(newScriptPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::TextDisabled("A component with fields shown in the inspector, written to the code folder.");
    ImGui::Spacing();
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    ImGui::InputTextWithHint("##name", "Component name", &state.newScriptName);
    ImGui::SameLine();
    if (ImGui::RadioButton("C#", state.newScriptCSharp))
    {
        state.newScriptCSharp = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("C++", !state.newScriptCSharp))
    {
        state.newScriptCSharp = false;
    }

    // A component name is a C# or C++ identifier.
    const std::string& name = state.newScriptName;
    const bool valid = !name.empty() && (std::isalpha(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_') &&
                       std::ranges::all_of(name, [](char character) {
                           return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
                       }) &&
                       scene::componentRegistry().find(name) == nullptr;
    if (!valid)
    {
        iconLabel(icons::TriangleAlert, themeColors().warning);
        ImGui::TextUnformatted(name.empty() ? "The component needs a name."
                                            : "Use a name that no component has yet, made of letters, digits and _.");
    }
    ImGui::Spacing();
    if (primaryButton(icons::FilePlus, "Create", ImGui::GetFontSize() * 8.0f, valid))
    {
        state.requests.newScript = NewScript{.name = name, .csharp = state.newScriptCSharp};
        state.pendingScript = name;
        state.pendingScriptEntity = state.selection.active();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (labelButton(icons::Close, "Cancel", ImGui::GetFontSize() * 8.0f))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawDebuggingWindow(ToolsState& state)
{
    if (!state.showDebugging)
    {
        return;
    }
    const DebuggerStatus& debugger = state.debugger;
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin(debuggingWindow, &state.showDebugging,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }
    const ThemeColors& colors = themeColors();
    ImGui::AlignTextToFramePadding();
    if (!debugger.available)
    {
        iconLabel(icons::Info, colors.warning);
        ImGui::TextUnformatted("The project has no C# code loaded.");
    }
    else if (debugger.attached)
    {
        iconLabel(icons::CircleCheck, colors.success);
        ImGui::TextUnformatted("A debugger is attached: breakpoints in the C# code stop the game.");
    }
    else if (debugger.waiting)
    {
        iconLabel(icons::Loader, colors.accent);
        ImGui::TextUnformatted("Play starts as soon as a debugger attaches.");
    }
    else
    {
        iconLabel(icons::Bug, colors.warning);
        ImGui::TextUnformatted("No debugger is attached.");
    }

    ImGui::Spacing();
    const std::string process = std::format("devex-editor, process {}", debugger.processId);
    ImGui::AlignTextToFramePadding();
    boldText(process.c_str());
    ImGui::SameLine();
    if (labelButton(icons::Copy, "Copy Process Id"))
    {
        ImGui::SetClipboardText(std::to_string(debugger.processId).c_str());
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 33.0f);
    ImGui::TextDisabled("Attach the debugger of your code editor to this process, for .NET code:");
    ImGui::BulletText("Visual Studio: Debug > Attach to Process (Ctrl+Alt+P), devex-editor.exe, "
                      "code type Managed (.NET Core, .NET 5+).");
    ImGui::BulletText("Rider: Run > Attach to Process, devex-editor.");
    ImGui::BulletText("VS Code with C# Dev Kit: .NET: Attach to a .NET 5+ or .NET Core process.");
    ImGui::TextDisabled("Breakpoints follow the code as the editor builds and reloads it.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Checkbox("Wait for a debugger when Play starts", &state.waitForDebugger);
    if (debugger.waiting)
    {
        ImGui::SameLine();
        if (labelButton(icons::Close, "Stop Waiting"))
        {
            state.requests.stop = true;
        }
    }
    ImGui::End();
}

void updatePendingScript(ToolsState& state, scene::Scene& scene)
{
    if (state.pendingScript.empty())
    {
        return;
    }
    if (!scene.findEntity(state.pendingScriptEntity).isValid())
    {
        state.pendingScript.clear();
        return;
    }
    // The component appears once its file is compiled and its code loaded.
    if (scene::componentRegistry().find(state.pendingScript) != nullptr)
    {
        state.pendingCommand = makeAddComponentCommand(state.pendingScriptEntity, std::exchange(state.pendingScript, {}));
    }
}

} // namespace devex::tools::detail
