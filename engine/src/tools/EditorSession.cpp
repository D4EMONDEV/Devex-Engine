#include "ToolsState.hpp"

#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <functional>
#include <utility>

namespace devex::tools::detail {
namespace {

constexpr std::size_t maxRecentProjects = 10;
constexpr const char* unsavedChangesPopup = "Unsaved changes";
constexpr const char* newProjectPopup = "New project";

using serialization::TextValue;

[[nodiscard]] std::string sceneName(const ToolsState& state)
{
    return state.scenePath.empty() ? std::string("Untitled") : core::toUtf8(state.scenePath.stem());
}

[[nodiscard]] const CommandHistory& editHistory(const ToolsState& state)
{
    return state.playState == PlayState::Editing ? state.history : state.suspendedHistory;
}

// The editor's settings for a project, kept in its cache folder: the last scene and the camera.
[[nodiscard]] std::filesystem::path projectSettingsFile(const asset::AssetDatabase& database)
{
    return database.project().cacheDirectory() / "editor.dvx";
}

[[nodiscard]] float numberOr(const serialization::TextSection& section, std::string_view key, float fallback)
{
    const TextValue* const value = section.findAttribute(key);
    const std::optional<double> number = value != nullptr ? serialization::asNumber(*value) : std::nullopt;
    return number ? static_cast<float>(*number) : fallback;
}

void writeProjectSettings(const ToolsState& state)
{
    if (state.database == nullptr)
    {
        return;
    }
    serialization::TextDocument document;
    serialization::TextSection& editor = document.sections.emplace_back();
    editor.type = "editor";
    editor.attributes.push_back({"format", TextValue(std::int64_t{1})});
    if (const std::string resource = state.database->project().resourcePath(state.scenePath); !resource.empty())
    {
        editor.attributes.push_back({"last_scene", TextValue(resource)});
    }
    serialization::TextSection& camera = document.sections.emplace_back();
    camera.type = "camera";
    const math::Vec3 pivot = state.camera.pivot();
    for (const auto& [key, value] : {std::pair<const char*, float>{"x", pivot.x},
                                     {"y", pivot.y},
                                     {"z", pivot.z},
                                     {"yaw", state.camera.yaw()},
                                     {"pitch", state.camera.pitch()},
                                     {"distance", state.camera.distance()},
                                     {"speed", state.camera.speed()}})
    {
        camera.attributes.push_back({key, TextValue(static_cast<double>(value))});
    }
    if (core::Result<void> written = core::writeTextFile(projectSettingsFile(*state.database),
                                                         serialization::writeText(document));
        !written)
    {
        DEVEX_LOG_WARNING("Cannot save the editor settings: {}", written.error());
    }
}

void writeRecentProjects(const ToolsState& state)
{
    if (state.userSettingsFile.empty())
    {
        return;
    }
    serialization::TextDocument document;
    for (const std::filesystem::path& project : state.recentProjects)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "recent_project";
        section.attributes.push_back({"path", TextValue(core::toUtf8(project))});
    }
    if (core::Result<void> written = core::writeTextFile(state.userSettingsFile, serialization::writeText(document));
        !written)
    {
        DEVEX_LOG_WARNING("Cannot save the recent projects: {}", written.error());
    }
}

void rememberProject(ToolsState& state, const std::filesystem::path& projectFile)
{
    std::error_code error;
    const std::filesystem::path normal = std::filesystem::weakly_canonical(projectFile, error);
    const std::filesystem::path path = error ? projectFile : normal;
    std::erase(state.recentProjects, path);
    state.recentProjects.insert(state.recentProjects.begin(), path);
    if (state.recentProjects.size() > maxRecentProjects)
    {
        state.recentProjects.resize(maxRecentProjects);
    }
    writeRecentProjects(state);
}

// A small lit scene to start from.
[[nodiscard]] scene::Scene makeDefaultScene()
{
    const math::Vec3 up{0.0f, 1.0f, 0.0f};
    const math::Vec3 right{1.0f, 0.0f, 0.0f};
    scene::Scene result;

    const scene::Entity sun = result.createEntity("Sun");
    result.add<scene::Transform>(sun, scene::Transform{
                                          .rotation = math::angleAxis(math::radians(35.0f), up) *
                                                      math::angleAxis(math::radians(-50.0f), right),
                                      });
    result.add<scene::DirectionalLight>(sun);

    const scene::Entity sky = result.createEntity("Sky");
    result.add<scene::Environment>(sky, scene::Environment{.color = {0.45f, 0.62f, 1.0f}, .intensity = 12000.0f});

    const scene::Entity camera = result.createEntity("Camera");
    result.add<scene::Transform>(camera, scene::Transform{
                                             .position = {0.0f, 1.5f, 6.0f},
                                             .rotation = math::angleAxis(math::radians(-8.0f), right),
                                         });
    result.add<scene::Camera>(camera);

    const scene::Entity ground = result.createEntity("Ground");
    result.add<scene::Transform>(ground, scene::Transform{.scale = {20.0f, 1.0f, 20.0f}});
    result.add<scene::MeshRenderer>(ground, scene::MeshRenderer{.mesh = asset::builtin::planeMesh});

    const scene::Entity cube = result.createEntity("Cube");
    result.add<scene::Transform>(cube, scene::Transform{.position = {0.0f, 0.5f, 0.0f}});
    result.add<scene::MeshRenderer>(cube, scene::MeshRenderer{.mesh = asset::builtin::cubeMesh});
    return result;
}

void resetDocument(ToolsState& state, std::filesystem::path path)
{
    state.history.clear();
    state.scenePath = std::move(path);
    state.savedState = state.history.stateId();
    state.selection = core::Uuid{};
    state.gizmo.end();
}

void newScene(ToolsState& state, scene::Scene& scene)
{
    scene = makeDefaultScene();
    resetDocument(state, {});
    state.camera.lookAt(math::Vec3{6.0f, 4.0f, 8.0f}, math::Vec3{0.0f, 0.5f, 0.0f});
}

[[nodiscard]] bool openScene(ToolsState& state, scene::Scene& scene, const std::filesystem::path& path)
{
    core::Result<scene::Scene> loaded = scene::loadSceneFile(path);
    if (!loaded)
    {
        DEVEX_LOG_ERROR("Cannot open the scene: {}", loaded.error());
        return false;
    }
    scene = std::move(*loaded);
    resetDocument(state, path);
    writeProjectSettings(state);
    DEVEX_LOG_INFO("Opened {}", core::toUtf8(path.filename()));
    return true;
}

void applySceneChange(ToolsState& state, scene::Scene& scene, const SceneChange& change)
{
    switch (change.kind)
    {
    case SceneChange::Kind::NewScene:
        newScene(state, scene);
        break;
    case SceneChange::Kind::OpenScene:
        static_cast<void>(openScene(state, scene, change.path));
        break;
    case SceneChange::Kind::OpenProject:
        state.requests.openProject = change.path;
        break;
    case SceneChange::Kind::Quit:
        state.requests.quit = true;
        break;
    }
}

// Opens the scene the project was left with, or its first scene, or a new one.
void openProjectScene(ToolsState& state, scene::Scene& scene)
{
    const asset::AssetDatabase& database = *state.database;
    const std::filesystem::path settingsFile = projectSettingsFile(database);
    std::optional<std::filesystem::path> lastScene;
    if (const core::Result<std::string> text = core::readTextFile(settingsFile))
    {
        if (const core::Result<serialization::TextDocument> document = serialization::parseText(*text))
        {
            for (const serialization::TextSection& section : document->sections)
            {
                if (section.type == "editor")
                {
                    const TextValue* const value = section.findAttribute("last_scene");
                    const std::string* const resource = value != nullptr ? serialization::asString(*value) : nullptr;
                    lastScene = resource != nullptr ? database.project().absolutePath(*resource) : std::nullopt;
                }
                else if (section.type == "camera")
                {
                    state.camera.set({numberOr(section, "x", 0.0f), numberOr(section, "y", 0.0f), numberOr(section, "z", 0.0f)},
                                     numberOr(section, "yaw", -30.0f), numberOr(section, "pitch", -25.0f),
                                     numberOr(section, "distance", 10.0f), numberOr(section, "speed", 6.0f));
                }
            }
        }
    }

    // The application may have built its scene itself, which the editor keeps.
    if (scene.entityCount() > 0)
    {
        resetDocument(state, {});
        return;
    }
    if (lastScene && std::filesystem::exists(*lastScene) && openScene(state, scene, *lastScene))
    {
        return;
    }
    // Scenes still importing count: a project just opened imports in the background.
    for (const asset::SourceFile& source : database.sources())
    {
        const std::optional<std::filesystem::path> path =
            source.importer == "scene" ? database.project().absolutePath(source.path) : std::nullopt;
        if (path && openScene(state, scene, *path))
        {
            return;
        }
    }
    newScene(state, scene);
}

void createNewProject(ToolsState& state)
{
    const std::filesystem::path directory =
        core::pathFromUtf8(state.newProjectLocation) / core::pathFromUtf8(state.newProjectName);
    std::error_code error;
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error))
    {
        DEVEX_LOG_ERROR("Cannot create the project: '{}' is not empty", core::toUtf8(directory));
        return;
    }
    const core::Result<asset::Project> project = asset::createProject(directory, state.newProjectName);
    if (!project)
    {
        DEVEX_LOG_ERROR("Cannot create the project: {}", project.error());
        return;
    }
    const std::filesystem::path scenePath = project->assetsDirectory() / "scenes" / "Main.dvxscene";
    if (core::Result<void> saved = scene::saveSceneFile(makeDefaultScene(), scenePath); !saved)
    {
        DEVEX_LOG_WARNING("Cannot write the first scene: {}", saved.error());
    }
    DEVEX_LOG_INFO("Created project {} in {}", project->name, core::toUtf8(project->root));
    state.requests.openProject =
        project->root / core::pathFromUtf8(project->name + std::string(asset::projectExtension));
}

// A button drawing an icon: 0 play, 1 stop, 2 pause, 3 step.
[[nodiscard]] bool iconButton(const char* id, int icon, bool active, bool enabled)
{
    const float size = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(!enabled);
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const bool pressed = ImGui::Button(id, ImVec2(size * 1.4f, size));
    if (active)
    {
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();

    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 center = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f;
    const float half = size * 0.22f;
    const ImU32 color = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    switch (icon)
    {
    case 0:
        draw->AddTriangleFilled(center + ImVec2(-half * 0.8f, -half), center + ImVec2(-half * 0.8f, half),
                                center + ImVec2(half, 0.0f), color);
        break;
    case 1:
        draw->AddRectFilled(center - ImVec2(half, half), center + ImVec2(half, half), color);
        break;
    case 2:
        draw->AddRectFilled(center + ImVec2(-half, -half), center + ImVec2(-half * 0.3f, half), color);
        draw->AddRectFilled(center + ImVec2(half * 0.3f, -half), center + ImVec2(half, half), color);
        break;
    default:
        draw->AddTriangleFilled(center + ImVec2(-half, -half), center + ImVec2(-half, half),
                                center + ImVec2(half * 0.5f, 0.0f), color);
        draw->AddRectFilled(center + ImVec2(half * 0.6f, -half), center + ImVec2(half, half), color);
        break;
    }
    return pressed;
}

void drawPlayControls(ToolsState& state)
{
    const bool editing = state.playState == PlayState::Editing;
    const float width = ImGui::GetFrameHeight() * 1.4f * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    if (iconButton(editing ? "##play" : "##stop", editing ? 0 : 1, !editing, state.database != nullptr))
    {
        (editing ? state.requests.play : state.requests.stop) = true;
    }
    ImGui::SetItemTooltip(editing ? "Play (Ctrl+P)" : "Stop (Ctrl+P)");
    if (iconButton("##pause", 2, state.playState == PlayState::Paused, !editing))
    {
        state.requests.togglePause = true;
    }
    ImGui::SetItemTooltip("Pause (Ctrl+Shift+P)");
    if (iconButton("##step", 3, false, state.playState == PlayState::Paused))
    {
        state.requests.step = true;
    }
    ImGui::SetItemTooltip("Step (Ctrl+Alt+P)");
}

// Adds an entity built in a scratch scene as one undoable step, in front of the editor camera.
void requestCreatePreset(ToolsState& state, const char* name, const std::function<void(scene::Scene&, scene::Entity)>& build)
{
    scene::Scene scratch;
    const scene::Entity entity = scratch.createEntity(name);
    scratch.add<scene::Transform>(entity, scene::Transform{.position = state.camera.pivot()});
    build(scratch, entity);
    const core::Uuid uuid = scratch.uuid(entity);
    state.pendingCommand = makeCreateEntityTreeCommand(scene::saveEntityTree(scratch, entity), uuid, core::Uuid{},
                                                       std::format("Create {}", name));
    state.selection = uuid;
}

void drawCreateMenu(ToolsState& state)
{
    if (ImGui::MenuItem("Empty"))
    {
        requestCreatePreset(state, "Entity", [](scene::Scene&, scene::Entity) {});
    }
    ImGui::Separator();
    for (const auto& [name, mesh] : {std::pair<const char*, asset::AssetId>{"Cube", asset::builtin::cubeMesh},
                                     {"Sphere", asset::builtin::sphereMesh},
                                     {"Plane", asset::builtin::planeMesh}})
    {
        if (ImGui::MenuItem(name))
        {
            const asset::AssetId meshId = mesh;
            requestCreatePreset(state, name, [meshId](scene::Scene& scratch, scene::Entity entity) {
                scratch.add<scene::MeshRenderer>(entity, scene::MeshRenderer{.mesh = meshId});
            });
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Directional light"))
    {
        requestCreatePreset(state, "Directional light", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.get<scene::Transform>(entity).rotation = math::angleAxis(math::radians(-50.0f), math::Vec3{1.0f, 0.0f, 0.0f});
            scratch.add<scene::DirectionalLight>(entity);
        });
    }
    if (ImGui::MenuItem("Point light"))
    {
        requestCreatePreset(state, "Point light", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::PointLight>(entity);
        });
    }
    if (ImGui::MenuItem("Spot light"))
    {
        requestCreatePreset(state, "Spot light", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.get<scene::Transform>(entity).rotation = math::angleAxis(math::radians(-90.0f), math::Vec3{1.0f, 0.0f, 0.0f});
            scratch.add<scene::SpotLight>(entity);
        });
    }
    if (ImGui::MenuItem("Camera"))
    {
        requestCreatePreset(state, "Camera", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::Camera>(entity, scene::Camera{.primary = false});
        });
    }
    if (ImGui::MenuItem("Environment"))
    {
        requestCreatePreset(state, "Environment", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.remove<scene::Transform>(entity);
            scratch.add<scene::Environment>(entity);
        });
    }
}

} // namespace

bool hasUnsavedChanges(const ToolsState& state)
{
    return editHistory(state).stateId() != state.savedState;
}

void loadRecentProjects(ToolsState& state, const std::filesystem::path& file)
{
    if (file.empty())
    {
        const core::Result<std::filesystem::path> directory = state.platform.userDataDirectory("Editor");
        if (!directory)
        {
            DEVEX_LOG_WARNING("Recent projects are not remembered: {}", directory.error());
            return;
        }
        state.userSettingsFile = *directory / "editor.dvx";
    }
    else
    {
        state.userSettingsFile = file;
    }
    const core::Result<std::string> text = core::readTextFile(state.userSettingsFile);
    if (!text)
    {
        return;
    }
    const core::Result<serialization::TextDocument> document = serialization::parseText(*text);
    if (!document)
    {
        DEVEX_LOG_WARNING("Ignoring {}: {}", core::toUtf8(state.userSettingsFile), document.error());
        return;
    }
    for (const serialization::TextSection& section : document->sections)
    {
        const TextValue* const value = section.findAttribute("path");
        if (const std::string* const path = value != nullptr ? serialization::asString(*value) : nullptr;
            section.type == "recent_project" && path != nullptr)
        {
            state.recentProjects.push_back(core::pathFromUtf8(*path));
        }
    }
}

void requestSceneChange(ToolsState& state, scene::Scene& scene, SceneChange change)
{
    if (state.playState != PlayState::Editing)
    {
        if (change.kind == SceneChange::Kind::Quit || change.kind == SceneChange::Kind::OpenProject)
        {
            // Play mode ends first; the change is asked again once editing.
            state.requests.stop = true;
        }
        return;
    }
    if (state.database != nullptr && hasUnsavedChanges(state))
    {
        state.pendingChange = std::move(change);
        state.openUnsavedChangesPopup = true;
        return;
    }
    applySceneChange(state, scene, change);
}

bool saveScene(ToolsState& state, scene::Scene& scene)
{
    if (state.playState != PlayState::Editing || state.database == nullptr)
    {
        return false;
    }
    if (state.scenePath.empty())
    {
        showSaveSceneDialog(state);
        return false;
    }
    if (core::Result<void> saved = scene::saveSceneFile(scene, state.scenePath); !saved)
    {
        DEVEX_LOG_ERROR("Cannot save the scene: {}", saved.error());
        return false;
    }
    state.savedState = state.history.stateId();
    writeProjectSettings(state);
    // Imports the scene now rather than when the file watcher notices it.
    state.database->refresh();
    DEVEX_LOG_INFO("Saved {} ({} entities)", core::toUtf8(state.scenePath.filename()), scene.entityCount());
    return true;
}

void showSaveSceneDialog(ToolsState& state)
{
    if (state.database == nullptr)
    {
        return;
    }
    const std::filesystem::path folder = state.scenePath.empty() ? state.database->project().assetsDirectory() / "scenes"
                                                                 : state.scenePath.parent_path();
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {
                                      .type = platform::FileDialogType::SaveFile,
                                      .filters = {{"Scenes", "dvxscene"}},
                                      .defaultLocation = folder / core::pathFromUtf8(sceneName(state) + ".dvxscene"),
                                  },
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock())
                                      {
                                          inbox->saveSceneAs = chosen.value_or(std::filesystem::path());
                                      }
                                  });
}

void showOpenSceneDialog(ToolsState& state)
{
    if (state.database == nullptr)
    {
        return;
    }
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {
                                      .type = platform::FileDialogType::OpenFile,
                                      .filters = {{"Scenes", "dvxscene"}},
                                      .defaultLocation = state.database->project().assetsDirectory(),
                                  },
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock(); inbox && chosen)
                                      {
                                          inbox->openScene = std::move(chosen);
                                      }
                                  });
}

void showOpenProjectDialog(ToolsState& state)
{
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {
                                      .type = platform::FileDialogType::OpenFile,
                                      .filters = {{"Devex projects", "dvxproj"}},
                                  },
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock(); inbox && chosen)
                                      {
                                          inbox->openProject = std::move(chosen);
                                      }
                                  });
}

void updateEditorSession(ToolsState& state, scene::Scene& scene)
{
    if (state.projectChanged && state.database != nullptr)
    {
        state.projectChanged = false;
        rememberProject(state, state.database->project().root /
                                   core::pathFromUtf8(state.database->project().name + std::string(asset::projectExtension)));
        openProjectScene(state, scene);
    }

    DialogAnswers& answers = *state.dialogAnswers;
    if (std::optional<std::filesystem::path> project = std::exchange(answers.openProject, std::nullopt))
    {
        requestSceneChange(state, scene, {SceneChange::Kind::OpenProject, std::move(*project)});
    }
    if (std::optional<std::filesystem::path> location = std::exchange(answers.newProjectLocation, std::nullopt))
    {
        state.newProjectLocation = core::toUtf8(*location);
    }
    if (std::optional<std::filesystem::path> path = std::exchange(answers.openScene, std::nullopt))
    {
        requestSceneChange(state, scene, {SceneChange::Kind::OpenScene, std::move(*path)});
    }
    if (std::optional<std::filesystem::path> path = std::exchange(answers.saveSceneAs, std::nullopt))
    {
        // An empty path means the dialog was cancelled, which also cancels a change waiting for it.
        if (path->empty())
        {
            state.pendingChange.reset();
        }
        else if (state.database != nullptr && state.database->project().resourcePath(*path).empty())
        {
            DEVEX_LOG_ERROR("Scenes are saved inside the project folder, not in '{}'", core::toUtf8(*path));
            state.pendingChange.reset();
        }
        else
        {
            if (path->extension() != scene::sceneExtension)
            {
                *path += scene::sceneExtension;
            }
            state.scenePath = std::move(*path);
            if (saveScene(state, scene) && state.pendingChange)
            {
                applySceneChange(state, scene, *std::exchange(state.pendingChange, std::nullopt));
            }
        }
    }

    for (const render::PickResult& result : state.renderer.takePickResults())
    {
        if (result.request != state.awaitedPick)
        {
            continue;
        }
        state.awaitedPick = 0;
        const scene::Entity entity = result.objectId > 0 ? scene.entityAtIndex(result.objectId - 1) : scene::Entity{};
        state.selection = entity.isValid() ? scene.uuid(entity) : core::Uuid{};
    }
}

void updateWindowTitle(ToolsState& state, const scene::Scene& /*scene*/)
{
    std::string title = "Devex Editor";
    if (state.database != nullptr)
    {
        const char* const play = state.playState == PlayState::Playing ? " [Playing]"
                                 : state.playState == PlayState::Paused ? " [Paused]"
                                                                         : "";
        title = std::format("{}{} - {} - Devex Editor{}", sceneName(state), hasUnsavedChanges(state) ? "*" : "",
                            state.database->project().name, play);
    }
    if (title != state.windowTitle)
    {
        state.windowTitle = title;
        state.window.setTitle(title);
    }
}

void drawEditorMenus(ToolsState& state, scene::Scene& scene)
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }
    const bool editing = state.playState == PlayState::Editing;
    const bool hasProject = state.database != nullptr;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New scene", "Ctrl+N", false, editing && hasProject))
        {
            requestSceneChange(state, scene, {SceneChange::Kind::NewScene, {}});
        }
        if (ImGui::MenuItem("Open scene...", "Ctrl+O", false, editing && hasProject))
        {
            showOpenSceneDialog(state);
        }
        if (ImGui::MenuItem("Save scene", "Ctrl+S", false, editing && hasProject))
        {
            static_cast<void>(saveScene(state, scene));
        }
        if (ImGui::MenuItem("Save scene as...", "Ctrl+Shift+S", false, editing && hasProject))
        {
            showSaveSceneDialog(state);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New project...", nullptr, false, editing))
        {
            state.openNewProjectPopup = true;
        }
        if (ImGui::MenuItem("Open project...", nullptr, false, editing))
        {
            showOpenProjectDialog(state);
        }
        if (ImGui::BeginMenu("Recent projects", editing && !state.recentProjects.empty()))
        {
            for (const std::filesystem::path& project : state.recentProjects)
            {
                if (ImGui::MenuItem(core::toUtf8(project).c_str()))
                {
                    requestSceneChange(state, scene, {SceneChange::Kind::OpenProject, project});
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
        {
            requestSceneChange(state, scene, {SceneChange::Kind::Quit, {}});
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        const Command* const nextUndo = state.history.nextUndo();
        const std::string undoLabel = nextUndo != nullptr ? std::format("Undo {}", nextUndo->description()) : "Undo";
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, nextUndo != nullptr))
        {
            if (core::Result<void> undone = state.history.undo(scene); !undone)
            {
                DEVEX_LOG_WARNING("{}", undone.error());
            }
        }
        const Command* const nextRedo = state.history.nextRedo();
        const std::string redoLabel = nextRedo != nullptr ? std::format("Redo {}", nextRedo->description()) : "Redo";
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, nextRedo != nullptr))
        {
            if (core::Result<void> redone = state.history.redo(scene); !redone)
            {
                DEVEX_LOG_WARNING("{}", redone.error());
            }
        }
        ImGui::Separator();
        const bool hasSelection = scene.findEntity(state.selection).isValid();
        if (ImGui::MenuItem("Frame selection", "F", false, hasSelection))
        {
            frameSelection(state, scene);
        }
        if (ImGui::MenuItem("Delete", "Delete", false, hasSelection))
        {
            state.pendingCommand = makeDestroyEntityCommand(state.selection);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Entity"))
    {
        if (ImGui::BeginMenu("Create"))
        {
            drawCreateMenu(state);
            ImGui::EndMenu();
        }
        const bool hasSelection = scene.findEntity(state.selection).isValid();
        if (ImGui::MenuItem("Create child", nullptr, false, hasSelection))
        {
            requestCreateEntity(state, state.selection);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem(viewportWindow, nullptr, &state.showViewport);
        ImGui::MenuItem(hierarchyWindow, nullptr, &state.showHierarchy);
        ImGui::MenuItem(inspectorWindow, nullptr, &state.showInspector);
        ImGui::MenuItem(assetsWindow, nullptr, &state.showAssets);
        ImGui::MenuItem(consoleWindow, nullptr, &state.showConsole);
        ImGui::MenuItem(statisticsWindow, nullptr, &state.showStatistics);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout"))
        {
            state.resetLayout = true;
        }
        ImGui::EndMenu();
    }

    drawPlayControls(state);
    ImGui::EndMainMenuBar();
}

void drawEditorPopups(ToolsState& state, scene::Scene& scene)
{
    if (std::exchange(state.openUnsavedChangesPopup, false))
    {
        ImGui::OpenPopup(unsavedChangesPopup);
    }
    if (std::exchange(state.openNewProjectPopup, false))
    {
        ImGui::OpenPopup(newProjectPopup);
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(unsavedChangesPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s has unsaved changes.", sceneName(state).c_str());
        ImGui::TextDisabled("They are lost unless the scene is saved first.");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(110.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
            // An untitled scene waits for the save dialog, which applies the change.
            if (saveScene(state, scene) && state.pendingChange)
            {
                applySceneChange(state, scene, *std::exchange(state.pendingChange, std::nullopt));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(110.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
            if (state.pendingChange)
            {
                applySceneChange(state, scene, *std::exchange(state.pendingChange, std::nullopt));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
            state.pendingChange.reset();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(newProjectPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("Name", &state.newProjectName);
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("Location", &state.newProjectLocation);
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
        {
            std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
            state.platform.showFileDialog(state.window, {.type = platform::FileDialogType::OpenFolder},
                                          [answers](std::optional<std::filesystem::path> chosen) {
                                              if (const std::shared_ptr<DialogAnswers> inbox = answers.lock(); inbox && chosen)
                                              {
                                                  inbox->newProjectLocation = std::move(chosen);
                                              }
                                          });
        }
        if (!state.newProjectLocation.empty() && !state.newProjectName.empty())
        {
            ImGui::TextDisabled("%s", core::toUtf8(core::pathFromUtf8(state.newProjectLocation) /
                                                   core::pathFromUtf8(state.newProjectName))
                                          .c_str());
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(state.newProjectName.empty() || state.newProjectLocation.empty());
        if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
            if (state.database != nullptr && hasUnsavedChanges(state))
            {
                DEVEX_LOG_WARNING("Save or discard the changes to the scene before creating a project");
            }
            else
            {
                createNewProject(state);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void handleEditorShortcuts(ToolsState& state, scene::Scene& scene)
{
    const bool editing = state.playState == PlayState::Editing;
    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, global))
    {
        (editing ? state.requests.play : state.requests.stop) = true;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, global) && !editing)
    {
        state.requests.togglePause = true;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, global) && state.playState == PlayState::Paused)
    {
        state.requests.step = true;
    }
    if (!editing)
    {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global))
    {
        static_cast<void>(saveScene(state, scene));
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, global))
    {
        showSaveSceneDialog(state);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, global))
    {
        requestSceneChange(state, scene, {SceneChange::Kind::NewScene, {}});
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, global))
    {
        showOpenSceneDialog(state);
    }
}

void drawWelcomeScreen(ToolsState& state)
{
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("Welcome", nullptr, flags))
    {
        const float width = std::min(640.0f, viewport->WorkSize.x - 40.0f);
        const float top = viewport->WorkSize.y * 0.12f;
        ImGui::SetCursorPos(ImVec2((viewport->WorkSize.x - width) * 0.5f, top));
        ImGui::BeginChild("welcome", ImVec2(width, viewport->WorkSize.y - top - 20.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(2.0f);
        ImGui::TextUnformatted("Devex Editor");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("Version %s", std::string(core::version()).c_str());
        ImGui::Dummy(ImVec2(width, 12.0f));

        if (ImGui::Button("New project...", ImVec2(160.0f, 0.0f)))
        {
            state.openNewProjectPopup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open project...", ImVec2(160.0f, 0.0f)))
        {
            showOpenProjectDialog(state);
        }
        ImGui::Dummy(ImVec2(width, 12.0f));
        ImGui::SeparatorText("Recent projects");

        if (state.recentProjects.empty())
        {
            ImGui::TextDisabled("Projects you open appear here.");
        }
        std::optional<std::filesystem::path> removed;
        for (const std::filesystem::path& project : state.recentProjects)
        {
            ImGui::PushID(core::toUtf8(project).c_str());
            const bool exists = std::filesystem::exists(project);
            ImGui::BeginDisabled(!exists);
            if (ImGui::Selectable("##project", false, ImGuiSelectableFlags_None, ImVec2(width, ImGui::GetTextLineHeight() * 2.2f)))
            {
                state.requests.openProject = project;
            }
            ImGui::EndDisabled();
            if (ImGui::BeginPopupContextItem("project menu"))
            {
                if (ImGui::MenuItem("Remove from list"))
                {
                    removed = project;
                }
                ImGui::EndPopup();
            }
            const ImVec2 textPosition = ImGui::GetItemRectMin() + ImVec2(8.0f, 2.0f);
            ImGui::GetWindowDrawList()->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text),
                                                core::toUtf8(project.stem()).c_str());
            const std::string detail = exists ? core::toUtf8(project.parent_path()) : "Missing: " + core::toUtf8(project);
            ImGui::GetWindowDrawList()->AddText(textPosition + ImVec2(0.0f, ImGui::GetTextLineHeight()),
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), detail.c_str());
            ImGui::PopID();
        }
        if (removed)
        {
            std::erase(state.recentProjects, *removed);
            writeRecentProjects(state);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void saveEditorSettings(ToolsState& state)
{
    writeProjectSettings(state);
}

} // namespace devex::tools::detail
