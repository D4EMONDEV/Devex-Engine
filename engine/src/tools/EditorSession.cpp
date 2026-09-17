#include "ToolsState.hpp"

#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <utility>

namespace devex::tools::detail {
namespace {

using serialization::TextValue;

// Folders the project scan does not enter: build output and caches can hold thousands of files.
constexpr std::array<std::string_view, 6> skippedFolders{".git", ".devex", ".vs", "out", "build", "node_modules"};
constexpr int scanDepth = 6;

[[nodiscard]] std::int64_t secondsSinceEpoch() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// The editor's settings for a project, kept in its cache folder: the open scenes and their cameras.
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

[[nodiscard]] const std::string* stringAttribute(const serialization::TextSection& section, std::string_view key)
{
    const TextValue* const value = section.findAttribute(key);
    return value != nullptr ? serialization::asString(*value) : nullptr;
}

void writeCamera(serialization::TextSection& section, const EditorCamera& camera)
{
    const math::Vec3 pivot = camera.pivot();
    for (const auto& [key, value] : {std::pair<const char*, float>{"x", pivot.x},
                                     {"y", pivot.y},
                                     {"z", pivot.z},
                                     {"yaw", camera.yaw()},
                                     {"pitch", camera.pitch()},
                                     {"distance", camera.distance()},
                                     {"speed", camera.speed()}})
    {
        section.attributes.push_back({key, TextValue(static_cast<double>(value))});
    }
}

[[nodiscard]] EditorCamera readCamera(const serialization::TextSection& section)
{
    EditorCamera camera;
    camera.set({numberOr(section, "x", 0.0f), numberOr(section, "y", 0.0f), numberOr(section, "z", 0.0f)},
               numberOr(section, "yaw", -30.0f), numberOr(section, "pitch", -25.0f), numberOr(section, "distance", 10.0f),
               numberOr(section, "speed", 6.0f));
    return camera;
}

void writeProjectSettings(ToolsState& state)
{
    if (state.database == nullptr || state.mode != ToolsMode::Editor)
    {
        return;
    }
    const asset::Project& project = state.database->project();
    serialization::TextDocument document;
    serialization::TextSection& editor = document.sections.emplace_back();
    editor.type = "editor";
    editor.attributes.push_back({"format", TextValue(std::int64_t{2})});
    if (const std::string active = project.resourcePath(state.scenePath); !active.empty())
    {
        editor.attributes.push_back({"active_scene", TextValue(active)});
    }
    for (std::size_t index = 0; index < state.tabs.size(); ++index)
    {
        const bool isActive = index == state.tabs.active();
        const std::filesystem::path& path = isActive ? state.scenePath : state.tabs.background(index).path;
        const std::string resource = project.resourcePath(path);
        if (resource.empty())
        {
            continue;
        }
        serialization::TextSection& tab = document.sections.emplace_back();
        tab.type = "scene";
        tab.attributes.push_back({"path", TextValue(resource)});
        writeCamera(tab, isActive ? state.camera : state.tabs.background(index).camera);
    }
    std::error_code error;
    std::filesystem::create_directories(project.cacheDirectory(), error);
    if (core::Result<void> written =
            core::writeTextFile(projectSettingsFile(*state.database), serialization::writeText(document));
        !written)
    {
        DEVEX_LOG_WARNING("Cannot save the editor settings: {}", written.error());
    }
}

[[nodiscard]] EditorCamera defaultCamera()
{
    EditorCamera camera;
    camera.lookAt(math::Vec3{6.0f, 4.0f, 8.0f}, math::Vec3{0.0f, 0.5f, 0.0f});
    return camera;
}

[[nodiscard]] SceneDocument makeDocument(std::filesystem::path path, scene::Scene scene, EditorCamera camera)
{
    SceneDocument document;
    document.path = std::move(path);
    document.scene = std::move(scene);
    document.savedState = document.history.stateId();
    document.camera = camera;
    return document;
}

[[nodiscard]] std::optional<SceneDocument> loadDocument(const std::filesystem::path& path, EditorCamera camera)
{
    core::Result<scene::Scene> loaded = scene::loadSceneFile(path);
    if (!loaded)
    {
        DEVEX_LOG_ERROR("Cannot open the scene: {}", loaded.error());
        return std::nullopt;
    }
    return makeDocument(path, std::move(*loaded), camera);
}

// Edits in progress refer to the entities of the scene that leaves the screen.
void resetTransientEdits(ToolsState& state)
{
    state.gizmo.end();
    state.hoveredHandle = GizmoHandle::None;
    state.clickStart.reset();
    state.pickPixel.reset();
    state.awaitedPick = 0;
    state.pendingCommand.reset();
    state.nameBufferEntity = core::Uuid{};
    state.eulerEditId = 0;
}

void addAndActivate(ToolsState& state, scene::Scene& scene, SceneDocument document)
{
    const std::size_t index = state.tabs.add(std::move(document));
    activateSceneTab(state, scene, index);
}

[[nodiscard]] bool isUntouchedNewScene(ToolsState& state, scene::Scene& scene, std::size_t index)
{
    const ActiveDocument live = activeDocument(state, scene);
    return state.tabs.path(index, live).empty() && !state.tabs.isModified(index, live);
}

// Opens the scenes the project was left with, or its first scene, or a new one.
void openProjectScenes(ToolsState& state, scene::Scene& scene)
{
    const asset::AssetDatabase& database = *state.database;
    state.tabs.clear(activeDocument(state, scene));

    // The application may have built its scene itself, which the editor keeps in an untitled tab.
    if (scene.entityCount() > 0)
    {
        scene::Scene built = std::move(scene);
        scene = scene::Scene{};
        addAndActivate(state, scene, makeDocument({}, std::move(built), defaultCamera()));
        return;
    }

    std::vector<SceneDocument> documents;
    std::optional<std::filesystem::path> activePath;
    if (const core::Result<std::string> text = core::readTextFile(projectSettingsFile(database)))
    {
        if (const core::Result<serialization::TextDocument> settings = serialization::parseText(*text))
        {
            EditorCamera legacyCamera = defaultCamera();
            std::optional<std::filesystem::path> legacyScene;
            for (const serialization::TextSection& section : settings->sections)
            {
                const std::string* const path = stringAttribute(section, section.type == "scene" ? "path" : "active_scene");
                if (section.type == "editor")
                {
                    activePath = path != nullptr ? database.project().absolutePath(*path) : std::nullopt;
                    // Editors before scene tabs remembered one scene.
                    const std::string* const last = stringAttribute(section, "last_scene");
                    legacyScene = last != nullptr ? database.project().absolutePath(*last) : std::nullopt;
                }
                else if (section.type == "camera")
                {
                    legacyCamera = readCamera(section);
                }
                else if (const std::optional<std::filesystem::path> file =
                             section.type == "scene" && path != nullptr ? database.project().absolutePath(*path)
                                                                        : std::nullopt;
                         file && std::filesystem::exists(*file))
                {
                    if (std::optional<SceneDocument> document = loadDocument(*file, readCamera(section)))
                    {
                        documents.push_back(std::move(*document));
                    }
                }
            }
            if (documents.empty() && legacyScene && std::filesystem::exists(*legacyScene))
            {
                if (std::optional<SceneDocument> document = loadDocument(*legacyScene, legacyCamera))
                {
                    documents.push_back(std::move(*document));
                }
                activePath = legacyScene;
            }
        }
    }

    if (documents.empty())
    {
        // Scenes still importing count: a project just opened imports in the background.
        for (const asset::SourceFile& source : database.sources())
        {
            const std::optional<std::filesystem::path> path =
                source.importer == "scene" ? database.project().absolutePath(source.path) : std::nullopt;
            if (path)
            {
                if (std::optional<SceneDocument> document = loadDocument(*path, defaultCamera()))
                {
                    documents.push_back(std::move(*document));
                    break;
                }
            }
        }
    }
    if (documents.empty())
    {
        documents.push_back(makeDocument({}, makeDefaultScene(), defaultCamera()));
    }

    std::size_t active = 0;
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        if (activePath && documents[index].path == *activePath)
        {
            active = index;
        }
        static_cast<void>(state.tabs.add(std::move(documents[index])));
    }
    activateSceneTab(state, scene, active);
}

// The tabs whose unsaved changes the action would drop.
[[nodiscard]] std::vector<std::size_t> affectedTabs(ToolsState& state, scene::Scene& scene, const PendingAction& action)
{
    std::vector<std::size_t> tabs;
    if (state.database == nullptr)
    {
        return tabs;
    }
    const ActiveDocument live = activeDocument(state, scene);
    for (std::size_t index = 0; index < state.tabs.size(); ++index)
    {
        const bool concerned = action.kind != PendingAction::Kind::CloseTab || state.tabs.id(index) == action.tab;
        if (concerned && state.tabs.isModified(index, live))
        {
            tabs.push_back(index);
        }
    }
    return tabs;
}

void applyAction(ToolsState& state, scene::Scene& scene, const PendingAction& action)
{
    switch (action.kind)
    {
    case PendingAction::Kind::CloseTab:
        if (const std::optional<std::size_t> index = state.tabs.findById(action.tab))
        {
            resetTransientEdits(state);
            state.tabs.close(*index, activeDocument(state, scene));
            if (state.tabs.empty())
            {
                newSceneTab(state, scene);
            }
            writeProjectSettings(state);
        }
        break;
    case PendingAction::Kind::OpenProject:
        state.requests.openProject = action.path;
        break;
    case PendingAction::Kind::CloseProject:
        state.requests.closeProject = true;
        break;
    case PendingAction::Kind::Quit:
        state.requests.quit = true;
        break;
    }
}

[[nodiscard]] bool saveBackgroundTab(ToolsState& state, std::size_t index)
{
    SceneDocument& document = state.tabs.background(index);
    if (core::Result<void> saved = scene::saveSceneFile(document.scene, document.path); !saved)
    {
        DEVEX_LOG_ERROR("Cannot save the scene: {}", saved.error());
        return false;
    }
    document.savedState = document.history.stateId();
    DEVEX_LOG_INFO("Saved {} ({} entities)", core::toUtf8(document.path.filename()), document.scene.entityCount());
    return true;
}

void scanForProjects(ToolsState& state, const std::filesystem::path& folder)
{
    std::size_t found = 0;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(folder, std::filesystem::directory_options::skip_permission_denied,
                                                           error);
    for (; !error && iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(error))
    {
        const std::filesystem::directory_entry& entry = *iterator;
        const std::string name = core::toUtf8(entry.path().filename());
        if (entry.is_directory(error))
        {
            if (iterator.depth() >= scanDepth || std::ranges::find(skippedFolders, name) != skippedFolders.end())
            {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (entry.path().extension() == asset::projectExtension)
        {
            state.projects.add(entry.path());
            ++found;
        }
    }
    DEVEX_LOG_INFO("Found {} project{} in {}", found, found == 1 ? "" : "s", core::toUtf8(folder));
    state.projectManager.refresh = true;
    saveUserSettings(state);
}

// Adds an entity built in a scratch scene as one undoable step, at the editor camera's pivot.
void requestCreatePreset(ToolsState& state, core::Uuid parent, const char* name,
                         const std::function<void(scene::Scene&, scene::Entity)>& build)
{
    scene::Scene scratch;
    const scene::Entity entity = scratch.createEntity(name);
    scratch.add<scene::Transform>(entity, scene::Transform{.position = parent.isNil() ? state.camera.pivot() : math::Vec3{0.0f}});
    build(scratch, entity);
    const core::Uuid uuid = scratch.uuid(entity);
    state.pendingCommand = makeCreateEntityTreeCommand(scene::saveEntityTree(scratch, entity), uuid, parent,
                                                       std::format("Create {}", name));
    state.selection = uuid;
}

// Writes the entity chosen for Save as Prefab and its descendants to a new scene file, then replaces
// them with an instance of it, as one undoable step.
void saveAsPrefab(ToolsState& state, scene::Scene& scene, std::filesystem::path path)
{
    const scene::Entity entity = scene.findEntity(state.prefabEntity);
    if (state.database == nullptr || !entity.isValid() || state.playState != PlayState::Editing ||
        scene::isInsidePrefabInstance(scene, entity))
    {
        return;
    }
    if (path.extension() != scene::sceneExtension)
    {
        path += scene::sceneExtension;
    }
    const std::string resource = state.database->project().resourcePath(path);
    std::error_code error;
    if (resource.empty())
    {
        DEVEX_LOG_ERROR("Prefabs are saved inside the project folder, not in '{}'", core::toUtf8(path));
        return;
    }
    // Instances of an existing file would still show its previous version.
    if (std::filesystem::exists(path, error))
    {
        DEVEX_LOG_ERROR("{} already exists: save the prefab to a new file", resource);
        return;
    }

    // The prefab: the entity and its descendants, with the root at the origin.
    scene::Scene prefab;
    const core::Result<scene::Entity> root = scene::loadEntityTree(prefab, scene::saveEntityTree(scene, entity), {});
    if (!root)
    {
        DEVEX_LOG_ERROR("Cannot save {} as a prefab: {}", scene.name(entity), root.error());
        return;
    }
    math::Vec3 position{0.0f};
    if (scene::Transform* const transform = prefab.tryGet<scene::Transform>(*root))
    {
        position = std::exchange(transform->position, math::Vec3{0.0f});
    }
    if (core::Result<void> saved = scene::saveSceneFile(prefab, path); !saved)
    {
        DEVEX_LOG_ERROR("Cannot save {}: {}", resource, saved.error());
        return;
    }
    state.database->refresh();
    const std::optional<asset::AssetId> id = state.database->findByPath(resource);
    if (!id)
    {
        DEVEX_LOG_ERROR("{} was saved but is not an asset of the project", resource);
        return;
    }

    // The instance keeps the UUID, the name and the position of the entity.
    serialization::TextSection header{.type = "entity"};
    header.attributes.push_back({"uuid", TextValue(state.prefabEntity.toString())});
    header.attributes.push_back({"name", TextValue(scene.name(entity))});
    header.properties.push_back({"prefab", serialization::makeCall("asset", {TextValue(id->uuid.toString())})});
    serialization::TextDocument document;
    document.sections.push_back(std::move(header));
    scene::Scene scratch;
    const core::Result<scene::Entity> instance = scene::loadEntityTree(scratch, serialization::writeText(document), {});
    if (!instance)
    {
        DEVEX_LOG_ERROR("Cannot place the prefab {}: {}", resource, instance.error());
        return;
    }
    if (scene::Transform* const transform = scratch.tryGet<scene::Transform>(*instance))
    {
        transform->position = position;
    }
    state.pendingCommand = makeReplaceEntityTreeCommand(state.prefabEntity, scene::saveEntityTree(scratch, *instance),
                                                        std::format("Save {} as prefab", scene.name(entity)));
    state.selection = state.prefabEntity;
    DEVEX_LOG_INFO("Saved {} as {}", scene.name(entity), resource);
}

} // namespace

scene::Scene makeDefaultScene()
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

ActiveDocument activeDocument(ToolsState& state, scene::Scene& scene) noexcept
{
    return {state.scenePath, scene, state.history, state.savedState, state.selection, state.camera};
}

std::string tabName(const std::filesystem::path& path)
{
    return path.empty() ? std::string("[unsaved]") : core::toUtf8(path.stem());
}

void activateSceneTab(ToolsState& state, scene::Scene& scene, std::size_t index)
{
    if (state.playState != PlayState::Editing || index >= state.tabs.size() || index == state.tabs.active())
    {
        return;
    }
    resetTransientEdits(state);
    state.tabs.activate(index, activeDocument(state, scene));
}

void newSceneTab(ToolsState& state, scene::Scene& scene)
{
    if (state.playState != PlayState::Editing)
    {
        return;
    }
    addAndActivate(state, scene, makeDocument({}, makeDefaultScene(), defaultCamera()));
}

void openSceneTab(ToolsState& state, scene::Scene& scene, const std::filesystem::path& path)
{
    if (state.playState != PlayState::Editing)
    {
        return;
    }
    if (const std::optional<std::size_t> open = state.tabs.findByPath(path, activeDocument(state, scene)))
    {
        activateSceneTab(state, scene, *open);
        return;
    }
    std::optional<SceneDocument> document = loadDocument(path, defaultCamera());
    if (!document)
    {
        return;
    }
    // An untouched new scene gives its place, like the empty document of a text editor.
    const std::optional<std::uint64_t> replaced =
        state.tabs.size() == 1 && isUntouchedNewScene(state, scene, 0) ? std::optional(state.tabs.id(0)) : std::nullopt;
    addAndActivate(state, scene, std::move(*document));
    if (replaced)
    {
        state.tabs.close(*state.tabs.findById(*replaced), activeDocument(state, scene));
    }
    writeProjectSettings(state);
    DEVEX_LOG_INFO("Opened {}", core::toUtf8(path.filename()));
}

std::vector<std::size_t> tabsWithUnsavedChanges(ToolsState& state, scene::Scene& scene)
{
    return state.pendingAction ? affectedTabs(state, scene, *state.pendingAction) : std::vector<std::size_t>{};
}

void cancelPendingAction(ToolsState& state)
{
    state.pendingAction.reset();
    state.resumeActionAfterPlay = false;
}

bool hasUnsavedChanges(ToolsState& state, scene::Scene& scene)
{
    return !affectedTabs(state, scene, {.kind = PendingAction::Kind::Quit}).empty();
}

void requestAction(ToolsState& state, scene::Scene& scene, PendingAction action)
{
    if (state.playState != PlayState::Editing)
    {
        if (action.kind != PendingAction::Kind::CloseTab)
        {
            // Play ends first; the action is asked again once editing.
            state.pendingAction = std::move(action);
            state.resumeActionAfterPlay = true;
            state.requests.stop = true;
        }
        return;
    }
    if (!affectedTabs(state, scene, action).empty())
    {
        state.pendingAction = std::move(action);
        state.openUnsavedChangesPopup = true;
        return;
    }
    state.pendingAction.reset();
    applyAction(state, scene, action);
}

bool saveForPendingAction(ToolsState& state, scene::Scene& scene)
{
    if (!state.pendingAction)
    {
        return false;
    }
    for (const std::size_t index : affectedTabs(state, scene, *state.pendingAction))
    {
        if (index == state.tabs.active())
        {
            if (!saveScene(state, scene))
            {
                return false;
            }
        }
        else if (state.tabs.background(index).path.empty())
        {
            activateSceneTab(state, scene, index);
            showSaveSceneDialog(state);
            return false;
        }
        else if (!saveBackgroundTab(state, index))
        {
            state.pendingAction.reset();
            return false;
        }
    }
    state.database->refresh();
    return true;
}

void continuePendingAction(ToolsState& state, scene::Scene& scene)
{
    if (state.pendingAction)
    {
        requestAction(state, scene, *std::exchange(state.pendingAction, std::nullopt));
    }
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

void saveAllScenes(ToolsState& state, scene::Scene& scene)
{
    if (state.playState != PlayState::Editing || state.database == nullptr)
    {
        return;
    }
    const ActiveDocument live = activeDocument(state, scene);
    bool savedBackground = false;
    for (std::size_t index = 0; index < state.tabs.size(); ++index)
    {
        if (index == state.tabs.active() || !state.tabs.isModified(index, live))
        {
            continue;
        }
        if (state.tabs.background(index).path.empty())
        {
            DEVEX_LOG_WARNING("[unsaved] scenes are saved from their own tab");
            continue;
        }
        savedBackground |= saveBackgroundTab(state, index);
    }
    if (state.tabs.active() && state.tabs.isModified(*state.tabs.active(), live))
    {
        static_cast<void>(saveScene(state, scene));
    }
    else if (savedBackground)
    {
        state.database->refresh();
    }
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
    const std::string name = state.scenePath.empty() ? std::string("New scene") : core::toUtf8(state.scenePath.stem());
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {
                                      .type = platform::FileDialogType::SaveFile,
                                      .filters = {{"Scenes", "dvxscene"}},
                                      .defaultLocation = folder / core::pathFromUtf8(name + ".dvxscene"),
                                  },
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock())
                                      {
                                          inbox->saveSceneAs = chosen.value_or(std::filesystem::path());
                                      }
                                  });
}

void showSaveAsPrefabDialog(ToolsState& state, const scene::Scene& scene, core::Uuid entity)
{
    const scene::Entity found = scene.findEntity(entity);
    if (state.database == nullptr || !found.isValid())
    {
        return;
    }
    state.prefabEntity = entity;
    const std::filesystem::path folder = state.database->project().assetsDirectory() / "prefabs";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::string name = scene.name(found).empty() ? std::string("prefab") : scene.name(found);
    std::ranges::replace_if(name, [](char character) { return std::string_view("<>:\"/\\|?*").contains(character); }, '_');
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {
                                      .type = platform::FileDialogType::SaveFile,
                                      .filters = {{"Scenes", "dvxscene"}},
                                      .defaultLocation = folder / core::pathFromUtf8(name + ".dvxscene"),
                                  },
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock())
                                      {
                                          inbox->saveAsPrefab = chosen.value_or(std::filesystem::path());
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

void loadUserSettings(ToolsState& state, const std::filesystem::path& file)
{
    if (file.empty())
    {
        const core::Result<std::filesystem::path> directory = state.platform.userDataDirectory("Editor");
        if (!directory)
        {
            DEVEX_LOG_WARNING("The editor settings are not remembered: {}", directory.error());
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
    state.projects = ProjectList::read(*document);
    for (const serialization::TextSection& section : document->sections)
    {
        if (section.type == "theme")
        {
            state.theme = readThemeSettings(section);
        }
    }
}

void saveUserSettings(const ToolsState& state)
{
    if (state.userSettingsFile.empty())
    {
        return;
    }
    serialization::TextDocument document;
    document.sections.push_back(writeThemeSettings(state.theme));
    state.projects.write(document);
    std::error_code error;
    std::filesystem::create_directories(state.userSettingsFile.parent_path(), error);
    if (core::Result<void> written = core::writeTextFile(state.userSettingsFile, serialization::writeText(document));
        !written)
    {
        DEVEX_LOG_WARNING("Cannot save the editor settings: {}", written.error());
    }
}

void updateEditorSession(ToolsState& state, scene::Scene& scene)
{
    if (state.projectChanged && state.database != nullptr)
    {
        state.projectChanged = false;
        state.projects.add(state.database->project().file, secondsSinceEpoch());
        state.projectManager.refresh = true;
        saveUserSettings(state);
        openProjectScenes(state, scene);
    }

    DialogAnswers& answers = *state.dialogAnswers;
    if (std::optional<std::filesystem::path> project = std::exchange(answers.importProject, std::nullopt))
    {
        state.projects.add(*project);
        state.projectManager.selected = normalProjectPath(*project);
        state.projectManager.refresh = true;
        saveUserSettings(state);
        requestAction(state, scene, {.kind = PendingAction::Kind::OpenProject, .path = std::move(*project)});
    }
    if (std::optional<std::filesystem::path> folder = std::exchange(answers.scanFolder, std::nullopt))
    {
        scanForProjects(state, *folder);
    }
    if (std::optional<std::filesystem::path> location = std::exchange(answers.newProjectLocation, std::nullopt))
    {
        state.projectManager.createParent = core::toUtf8(*location);
    }
    if (std::optional<std::filesystem::path> path = std::exchange(answers.openScene, std::nullopt))
    {
        openSceneTab(state, scene, *path);
    }
    if (std::optional<std::filesystem::path> path = std::exchange(answers.saveSceneAs, std::nullopt))
    {
        // An empty path means the dialog was cancelled, which also cancels an action waiting for it.
        if (path->empty())
        {
            state.pendingAction.reset();
        }
        else if (state.database != nullptr && state.database->project().resourcePath(*path).empty())
        {
            DEVEX_LOG_ERROR("Scenes are saved inside the project folder, not in '{}'", core::toUtf8(*path));
            state.pendingAction.reset();
        }
        else
        {
            if (path->extension() != scene::sceneExtension)
            {
                *path += scene::sceneExtension;
            }
            state.scenePath = std::move(*path);
            if (saveScene(state, scene))
            {
                continuePendingAction(state, scene);
            }
        }
    }

    if (std::optional<std::filesystem::path> path = std::exchange(answers.saveAsPrefab, std::nullopt); path && !path->empty())
    {
        saveAsPrefab(state, scene, std::move(*path));
    }
    if (const std::optional<asset::AssetId> prefab = std::exchange(state.prefabToOpen, std::nullopt);
        prefab && state.database != nullptr)
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(*prefab);
        if (const std::optional<std::filesystem::path> path =
                source ? state.database->project().absolutePath(source->path) : std::nullopt)
        {
            openSceneTab(state, scene, *path);
        }
        else
        {
            DEVEX_LOG_WARNING("Prefab {} is not in the project", prefab->uuid);
        }
    }

    // An action that waited for play to stop.
    if (state.resumeActionAfterPlay && state.playState == PlayState::Editing)
    {
        state.resumeActionAfterPlay = false;
        continuePendingAction(state, scene);
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
    std::string title = "Devex - Project Manager";
    if (state.database != nullptr)
    {
        const char* const play = state.playState == PlayState::Playing ? " [Playing]"
                                 : state.playState == PlayState::Paused ? " [Paused]"
                                                                         : "";
        const bool modified = state.history.stateId() != state.savedState && state.playState == PlayState::Editing;
        title = std::format("{}{} - {} - Devex Editor{}", tabName(state.scenePath), modified ? "*" : "",
                            state.database->project().name, play);
    }
    if (title != state.windowTitle)
    {
        state.windowTitle = title;
        state.window.setTitle(title);
    }
}

void saveEditorSettings(ToolsState& state)
{
    writeProjectSettings(state);
}

void drawCreateEntityMenu(ToolsState& state, core::Uuid parent)
{
    const ThemeColors& colors = themeColors();
    const auto item = [](IconText icon, ImVec4 color, const char* label) {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const std::string text = std::format("      {}", label);
        const bool clicked = ImGui::MenuItem(text.c_str());
        ImGui::GetWindowDrawList()->AddText(position, uiColorU32(color), icon.c_str());
        return clicked;
    };
    if (item(icons::Axis, colors.entity, "Empty"))
    {
        requestCreatePreset(state, parent, "Entity", [](scene::Scene&, scene::Entity) {});
    }
    ImGui::Separator();
    for (const auto& [name, mesh] : {std::pair<const char*, asset::AssetId>{"Cube", asset::builtin::cubeMesh},
                                     {"Sphere", asset::builtin::sphereMesh},
                                     {"Plane", asset::builtin::planeMesh}})
    {
        if (item(icons::Box, colors.entity, name))
        {
            const asset::AssetId meshId = mesh;
            requestCreatePreset(state, parent, name, [meshId](scene::Scene& scratch, scene::Entity entity) {
                scratch.add<scene::MeshRenderer>(entity, scene::MeshRenderer{.mesh = meshId});
            });
        }
    }
    ImGui::Separator();
    if (item(icons::Sun, colors.light, "Directional light"))
    {
        requestCreatePreset(state, parent, "Directional light", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.get<scene::Transform>(entity).rotation = math::angleAxis(math::radians(-50.0f), math::Vec3{1.0f, 0.0f, 0.0f});
            scratch.add<scene::DirectionalLight>(entity);
        });
    }
    if (item(icons::Lightbulb, colors.light, "Point light"))
    {
        requestCreatePreset(state, parent, "Point light", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::PointLight>(entity);
        });
    }
    if (item(icons::Flashlight, colors.light, "Spot light"))
    {
        requestCreatePreset(state, parent, "Spot light", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.get<scene::Transform>(entity).rotation = math::angleAxis(math::radians(-90.0f), math::Vec3{1.0f, 0.0f, 0.0f});
            scratch.add<scene::SpotLight>(entity);
        });
    }
    ImGui::Separator();
    if (item(icons::Video, colors.camera, "Camera"))
    {
        requestCreatePreset(state, parent, "Camera", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::Camera>(entity, scene::Camera{.primary = false});
        });
    }
    if (item(icons::CloudSun, colors.environment, "Environment"))
    {
        requestCreatePreset(state, parent, "Environment", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.remove<scene::Transform>(entity);
            scratch.add<scene::Environment>(entity);
        });
    }
    ImGui::Separator();
    if (item(icons::SquareDashed, colors.physics, "Static box"))
    {
        requestCreatePreset(state, parent, "Static box", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::MeshRenderer>(entity, scene::MeshRenderer{.mesh = asset::builtin::cubeMesh});
            scratch.add<scene::BoxCollider>(entity);
        });
    }
    if (item(icons::Weight, colors.physics, "Rigid box"))
    {
        requestCreatePreset(state, parent, "Rigid box", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::MeshRenderer>(entity, scene::MeshRenderer{.mesh = asset::builtin::cubeMesh});
            scratch.add<scene::RigidBody>(entity);
            scratch.add<scene::BoxCollider>(entity);
        });
    }
    if (item(icons::CircleDashed, colors.physics, "Rigid sphere"))
    {
        requestCreatePreset(state, parent, "Rigid sphere", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::MeshRenderer>(entity, scene::MeshRenderer{.mesh = asset::builtin::sphereMesh});
            scratch.add<scene::RigidBody>(entity);
            scratch.add<scene::SphereCollider>(entity);
        });
    }
    if (item(icons::PersonStanding, colors.physics, "Character"))
    {
        requestCreatePreset(state, parent, "Character", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::CharacterController>(entity);
        });
    }
    if (item(icons::Scan, colors.physics, "Trigger zone"))
    {
        requestCreatePreset(state, parent, "Trigger zone", [](scene::Scene& scratch, scene::Entity entity) {
            scratch.add<scene::BoxCollider>(entity, scene::BoxCollider{.size = {2.0f, 2.0f, 2.0f}, .trigger = true});
        });
    }
}

} // namespace devex::tools::detail
