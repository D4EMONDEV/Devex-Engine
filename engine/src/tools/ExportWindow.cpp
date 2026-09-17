#include "ToolsState.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {
namespace {

inline constexpr const char* exportWindow = "Export Game";

void sectionTitle(const char* title)
{
    ImGui::Spacing();
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText(title);
    ImGui::PopFont();
}

// The name of the executable, as the export makes it from the name of the game.
[[nodiscard]] std::string executableFileName(std::string_view gameName)
{
    std::string name;
    for (const char character : gameName)
    {
        const bool forbidden = static_cast<unsigned char>(character) < 0x20 ||
                               std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos;
        name.push_back(forbidden ? '_' : character);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
    {
        name.pop_back();
    }
    while (!name.empty() && (name.front() == ' ' || name.front() == '.'))
    {
        name.erase(name.begin());
    }
    return (name.empty() ? std::string("Game") : name) + ".exe";
}

void showOutputFolderDialog(ToolsState& state)
{
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {.type = platform::FileDialogType::OpenFolder,
                                   .defaultLocation = state.database->project().root},
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock(); inbox && chosen)
                                      {
                                          inbox->exportFolder = std::move(chosen);
                                      }
                                  });
}

// The folders of the project that hold assets, as res:// paths.
[[nodiscard]] std::vector<std::string> assetFolders(const asset::AssetDatabase& database)
{
    std::set<std::string> folders;
    for (const asset::SourceFile& source : database.sources())
    {
        std::string_view path = source.path;
        for (std::size_t slash = path.rfind('/'); slash != std::string_view::npos && path.substr(0, slash).size() > asset::resourceScheme.size();
             slash = path.rfind('/'))
        {
            path = path.substr(0, slash);
            folders.emplace(path);
        }
    }
    return {folders.begin(), folders.end()};
}

void drawExportStatus(ToolsState& state)
{
    const ThemeColors& colors = themeColors();
    const ExportStatus& status = state.exportStatus;
    switch (status.state)
    {
    case ExportStatus::State::Idle:
        return;
    case ExportStatus::State::Running:
        ImGui::ProgressBar(status.fraction, ImVec2(-FLT_MIN, 0.0f), status.message.c_str());
        return;
    case ExportStatus::State::Succeeded:
        iconLabel(icons::CircleCheck, colors.success);
        ImGui::TextWrapped("%s", status.message.c_str());
        if (labelButton(icons::FolderOpen, "Open Folder"))
        {
            if (core::Result<void> opened = state.platform.openPath(status.output); !opened)
            {
                DEVEX_LOG_WARNING("{}", opened.error());
            }
        }
        ImGui::SameLine();
        if (labelButton(icons::Play, "Run Game", 0.0f, !status.executable.empty()))
        {
            const std::array<std::string, 1> arguments{core::toUtf8(status.executable)};
            if (core::Result<void> launched = platform::Process::launch(arguments, status.output); !launched)
            {
                DEVEX_LOG_ERROR("Cannot run the game: {}", launched.error());
            }
        }
        return;
    case ExportStatus::State::Failed:
        iconLabel(icons::CircleX, colors.error);
        ImGui::PushStyleColor(ImGuiCol_Text, uiColor(colors.error));
        ImGui::TextWrapped("%s", status.message.c_str());
        ImGui::PopStyleColor();
        return;
    }
}

} // namespace

void drawExportWindow(ToolsState& state)
{
    if (!state.showExport || state.database == nullptr)
    {
        return;
    }
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, ImGui::GetFontSize() * 36.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin(exportWindow, &state.showExport, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const ThemeColors& colors = themeColors();
    const asset::Project& project = state.database->project();
    asset::ExportSettings settings = state.pendingExport.value_or(project.exportSettings);
    const bool running = state.exportStatus.state == ExportStatus::State::Running;

    if (std::optional<std::filesystem::path> folder = std::exchange(state.dialogAnswers->exportFolder, std::nullopt))
    {
        // Folders inside the project are kept relative to it, so that the project can move.
        const std::string resource = project.resourcePath(*folder);
        settings.output = resource.empty() ? core::toUtf8(*folder) : resource.substr(asset::resourceScheme.size());
    }

    sectionTitle("Game");
    if (beginProperties("game"))
    {
        propertyName("Executable");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(executableFileName(project.name).c_str());
        ImGui::SetItemTooltip("Named after the game: rename it in Project > Project Settings");

        propertyName("Output folder");
        const float browseWidth = toolButtonWidth();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##output", &settings.output);
        ImGui::SetItemTooltip("Relative to the project folder. A previous export there is replaced.");
        ImGui::SameLine();
        if (toolButton("browse", icons::FolderOpen, "Choose the folder"))
        {
            showOutputFolderDialog(state);
        }

        propertyName("Configuration");
        if (beginCombo("##configuration", settings.configuration.c_str()))
        {
            for (const char* const configuration : {"Release", "Debug"})
            {
                if (ImGui::Selectable(configuration, settings.configuration == configuration))
                {
                    settings.configuration = configuration;
                }
            }
            ImGui::EndCombo();
        }
        endProperties();
    }
    const auto build = std::ranges::find_if(state.engineBuilds, [&](const EngineBuildChoice& choice) {
        return choice.configuration == settings.configuration;
    });
    if (build == state.engineBuilds.end())
    {
        iconLabel(icons::TriangleAlert, colors.warning);
        ImGui::TextWrapped("There is no %s build of the engine next to the editor. Build it first, for instance with "
                           "cmake --build --preset build-x64-%s.",
                           settings.configuration.c_str(), settings.configuration == "Debug" ? "debug" : "release");
    }
    else
    {
        ImGui::TextDisabled("With the engine build %s. %s", build->name.c_str(),
                            settings.configuration == "Debug" ? "Debug games run slower and need Visual Studio."
                                                              : "Players need nothing else installed.");
    }

    sectionTitle("Scenes");
    const std::string startup = project.startupScene;
    for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Scene))
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(info.id);
        if (!source)
        {
            continue;
        }
        ImGui::PushID(source->path.c_str());
        const bool isStartup = startup.empty() ? false : source->path == startup;
        const auto listed = std::ranges::find(settings.scenes, source->path);
        bool exported = isStartup || listed != settings.scenes.end();
        ImGui::BeginDisabled(isStartup);
        if (ImGui::Checkbox(isStartup ? std::format("{} (startup)", source->path).c_str() : source->path.c_str(), &exported))
        {
            if (exported)
            {
                settings.scenes.push_back(source->path);
            }
            else
            {
                settings.scenes.erase(listed);
            }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::TextDisabled("The scenes and prefabs that exported scenes refer to are exported with them.");

    sectionTitle("Always Included Folders");
    for (std::size_t index = 0; index < settings.includeFolders.size();)
    {
        ImGui::PushID(static_cast<int>(index));
        const bool removed = toolButton("remove", icons::Trash, "Stop including the folder");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        iconLabel(icons::Folder, colors.folder);
        ImGui::TextUnformatted(settings.includeFolders[index].c_str());
        ImGui::PopID();
        if (removed)
        {
            settings.includeFolders.erase(settings.includeFolders.begin() + static_cast<std::ptrdiff_t>(index));
        }
        else
        {
            ++index;
        }
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    if (beginCombo("##add folder", "Add a folder..."))
    {
        for (const std::string& folder : assetFolders(*state.database))
        {
            if (std::ranges::find(settings.includeFolders, folder) == settings.includeFolders.end() &&
                ImGui::Selectable(folder.c_str()))
            {
                settings.includeFolders.push_back(folder);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Every asset of these folders is exported, for the assets that code loads by itself.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (primaryButton(icons::Package, running ? "Exporting..." : "Export", ImGui::GetFontSize() * 9.0f,
                      !running && build != state.engineBuilds.end() && !settings.output.empty()))
    {
        state.requests.exportGame = true;
    }
    ImGui::Spacing();
    drawExportStatus(state);
    ImGui::End();

    // Settings are saved once an edit ends, and always before an export starts.
    if (settings != project.exportSettings)
    {
        state.pendingExport = std::move(settings);
    }
    else
    {
        state.pendingExport.reset();
    }
    if (state.pendingExport && (!ImGui::IsAnyItemActive() || state.requests.exportGame))
    {
        asset::Project changed = project;
        changed.exportSettings = *std::exchange(state.pendingExport, std::nullopt);
        if (core::Result<void> written = state.database->updateProject(changed); !written)
        {
            DEVEX_LOG_ERROR("Cannot save the project: {}", written.error());
        }
    }
}

} // namespace devex::tools::detail
