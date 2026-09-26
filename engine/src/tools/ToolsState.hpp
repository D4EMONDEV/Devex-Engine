#pragma once

#include "CodeArea.hpp"
#include "EditorCamera.hpp"
#include "EditorView.hpp"
#include "Gizmo.hpp"
#include "Icons.hpp"
#include "ProjectList.hpp"
#include "SceneTabs.hpp"
#include "Selection.hpp"
#include "TextDocument.hpp"
#include "Theme.hpp"
#include "Widgets.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetMemory.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/asset/AudioClipData.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/tools/CommandHistory.hpp>
#include <devex/tools/LogBuffer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devex::animation {
class AnimationWorld;
class Clip;
} // namespace devex::animation

namespace devex::audio {
class AudioEngine;
class Clip;
} // namespace devex::audio

namespace devex::tools::detail {

// Window names are also their identifiers in the saved layout.
inline constexpr const char* hierarchyWindow = "Scene";
inline constexpr const char* inspectorWindow = "Inspector";
inline constexpr const char* statisticsWindow = "Statistics";
inline constexpr const char* consoleWindow = "Output";
inline constexpr const char* assetsWindow = "FileSystem";
inline constexpr const char* viewportWindow = "Viewport";
inline constexpr const char* settingsWindow = "Editor Settings";
inline constexpr const char* debuggingWindow = "C# Debugging";
inline constexpr const char* animationWindow = "Animation";
inline constexpr const char* textEditorWindow = "Text Editor";
inline constexpr const char* interfaceWindow = "Interface";
inline constexpr const char* profilerWindow = "Profiler";

// Payload type of an entity dragged in the hierarchy: the 16 bytes of its UUID.
inline constexpr const char* entityPayload = "DEVEX_ENTITY";
// Payload type of an asset dragged from the assets panel.
inline constexpr const char* assetPayload = "DEVEX_ASSET";

struct AssetPayload
{
    std::array<std::uint8_t, 16> uuid{};
    asset::AssetType type = asset::AssetType::Mesh;
};

// Recent frame times, for the statistics graph.
class FrameTimes
{
public:
    void record(float milliseconds) noexcept;
    [[nodiscard]] float average() const noexcept;
    [[nodiscard]] float maximum() const noexcept;
    // Values in recording order starting at offset, as ImGui::PlotLines expects.
    [[nodiscard]] const float* values() const noexcept;
    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] int offset() const noexcept;

private:
    std::array<float, 240> m_milliseconds{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
};

// What the Profiler panel keeps from one frame to the next.
struct ProfilerView
{
    // The frame shown, by index: while recording, the newest one, taken again twice a second.
    std::uint64_t frame = 0;
    double frameTaken = -1.0;
    // The part of the frame the timeline shows, in nanoseconds from its start: all of it when the
    // end is not after the beginning.
    double visibleBegin = 0.0;
    double visibleEnd = 0.0;
    asset::MemoryReport memory;
    double memoryRead = -1.0;
};

// How a click or a rectangle changes the selection: Shift adds, Ctrl adds or removes.
enum class SelectMode : std::uint8_t
{
    Replace,
    Add,
    Toggle,
};

// What the GPU is asked about the pixels of the viewport: the entity under a click, those under a
// rectangle, or the one under a material being dragged.
struct PickQuery
{
    enum class Purpose : std::uint8_t
    {
        Click,
        Rectangle,
        MaterialTarget,
    };

    Purpose purpose = Purpose::Click;
    // Viewport pixels; a click covers one.
    math::Vec2 min{0.0f};
    math::Vec2 max{0.0f};
    SelectMode mode = SelectMode::Replace;
    // Entities a rectangle found without the GPU: the icons of lights and cameras inside it.
    std::vector<core::Uuid> icons;
};

// An entity moved with the one under the gizmo, as it was when the drag started.
struct GizmoFollower
{
    core::Uuid entity;
    scene::Transform local;
    math::Mat4 world{1.0f};
    math::Mat4 parentWorld{1.0f};
};

// What a click in the viewport does: select only, or also show a gizmo.
enum class EditorTool : std::uint8_t
{
    Select,
    Move,
    Rotate,
    Scale,
};

// An action that drops scenes or text files, waiting for a decision about their unsaved changes.
struct PendingAction
{
    enum class Kind : std::uint8_t
    {
        // Closes one scene tab.
        CloseTab,
        CloseText,
        ReloadText,
        OpenProject,
        // Goes back to the project manager.
        CloseProject,
        Quit,
    };

    Kind kind = Kind::CloseTab;
    // The tab to close.
    std::uint64_t tab = 0;
    // The project to open, or the text file to close/reload.
    std::filesystem::path path;
};

// Answers of native file dialogs, which arrive between frames. Dialogs hold it weakly, so that an
// answer arriving after the tools are gone is dropped.
struct DialogAnswers
{
    std::optional<std::filesystem::path> importProject;
    std::optional<std::filesystem::path> scanFolder;
    std::optional<std::filesystem::path> newProjectLocation;
    std::optional<std::filesystem::path> openScene;
    std::optional<std::filesystem::path> openText;
    std::optional<std::filesystem::path> saveSceneAs;
    std::optional<std::filesystem::path> saveAsPrefab;
    std::optional<std::filesystem::path> exportFolder;
};

// What the project manager shows of a project, read from its file.
struct ProjectInfo
{
    std::string name;
    bool exists = false;
    ProjectCodeStatus code;
    // When the project file last changed, in seconds since 1970.
    std::int64_t modified = 0;
};

struct ProjectManagerState
{
    std::string filter;
    ProjectSort sort = ProjectSort::LastOpened;
    std::filesystem::path selected;
    // Keyed by the UTF-8 path of the project file, refreshed when the list changes.
    std::unordered_map<std::string, ProjectInfo> projects;
    bool refresh = true;

    bool openCreate = false;
    std::string createName = "New Game Project";
    std::string createParent;
    bool createFolder = true;
    bool openRename = false;
    std::string renameBuffer;
    bool openRemove = false;
};

// What the middle of the window shows. The panels around it stay where they are.
enum class MainScreen : std::uint8_t
{
    // Interfaces, and later the 2D games the engine will also make.
    TwoD,
    ThreeD,
    Script,
};

[[nodiscard]] std::string_view toString(MainScreen screen) noexcept;

// Whether the window is sized for the project manager or for the editor.
enum class WindowLayout : std::uint8_t
{
    Unset,
    ProjectManager,
    Editor,
};

struct ToolsState
{
    ToolsState(platform::Platform& platformLayer, platform::Window& mainWindow, render::Renderer& gpu,
               ToolsMode toolsMode) noexcept
        : platform(platformLayer)
        , window(mainWindow)
        , renderer(gpu)
        , mode(toolsMode)
    {
    }

    platform::Platform& platform;
    platform::Window& window;
    render::Renderer& renderer;
    ToolsMode mode;
    // Kept alive for ImGuiIO::IniFilename.
    std::string settingsFile;

    // Appearance.
    IconSet icons;
    EditorFonts fonts;
    ThemeSettings theme;
    // The theme changed and applies before the next frame.
    bool themeChanged = true;
    // The theme changed since the user's settings were last written.
    bool themeUnsaved = false;
    float appliedDisplayScale = 0.0f;

    bool visible = false;
    bool capturesKeyboard = false;
    bool capturesMouse = false;
    bool resetLayout = false;
    // Frames before the output tab is brought to the front of a new layout, once its windows are docked.
    int selectOutputTabFrames = 0;

    bool showHierarchy = true;
    bool showInspector = true;
    bool showStatistics = true;
    bool showConsole = true;
    bool showAssets = true;
    bool showViewport = true;
    bool showSettings = false;
    // The screen the middle of the window shows, chosen in the menu bar or by what the editor
    // opens: a scene shows the viewport, a file shows the text editor.
    MainScreen mainScreen = MainScreen::ThreeD;
    // The screen was chosen this frame: its window takes the focus of the middle.
    bool mainScreenChanged = false;
    bool showTextEditor = false;
    bool showInterface = false;
    bool focusTextEditor = false;
    bool selectTextTab = false;
    // Opening a document moves the others in memory: hold the path of a document across a frame,
    // never a pointer or a reference to it.
    std::vector<TextDocument> textDocuments;
    // What the editor keeps for the document it shows: cursor, search, completions.
    TextEditState textEdit;
    // The errors and warnings of the last build of the game code, shown in the margin.
    std::vector<CodeDiagnostic> codeDiagnostics;
    // The script screen: the files of the project's code, listed beside the text.
    std::vector<std::filesystem::path> scriptFiles;
    double scriptsScanned = -1.0;
    std::string scriptFilter;
    std::string symbolFilter;
    std::filesystem::path activeText;
    std::string textOpenError;

    // Null when the application runs without a project.
    asset::AssetDatabase* database = nullptr;
    // The mixer clips are previewed on, and where clips come from; null without audio.
    audio::AudioEngine* audio = nullptr;
    std::function<std::shared_ptr<const audio::Clip>(asset::AssetId)> audioClips;
    // The asset of the FileSystem shown in the inspector (an audio clip or a model), invalid when an
    // entity or a code file is selected.
    asset::AssetId selectedAsset;
    // What the inspector shows of the selected clip, read again once the file is imported again.
    std::optional<asset::AudioClipData> selectedClipInfo;
    std::size_t selectedClipSize = 0;
    bool selectedClipStale = true;
    // The clip playing in the editor, whose playhead the inspector shows.
    asset::AssetId previewedClip;
    // Animations: where clips come from, and the animations of the game while it plays.
    std::function<std::shared_ptr<const animation::Clip>(asset::AssetId)> animationClips;
    // The themes of interfaces, for the inspector to show what a style sets.
    std::function<std::shared_ptr<const asset::ThemeData>(asset::AssetId)> themes;
    // What the loaded assets take, for the Profiler panel.
    std::function<asset::MemoryReport()> memoryReport;
    // The meshes and textures loading in the background, for the status bar.
    std::function<std::size_t()> pendingLoads;
    animation::AnimationWorld* animationWorld = nullptr;
    bool showAnimation = false;
    // The profiler records while its panel is open.
    bool showProfiler = false;
    ProfilerView profiler;
    // What the Animation panel shows: the clip it last posed, and where its playhead stands.
    asset::AssetId previewedAnimation;
    float animationPreviewTime = 0.0f;
    bool animationPreviewPlaying = false;
    std::string assetFilter;
    std::string hierarchyFilter;

    // The commands of the scene on screen: while playing, those of the played copy, with the
    // commands of the edited scene set aside until play stops.
    CommandHistory history;
    CommandHistory suspendedHistory;
    LogBuffer log;
    FrameTimes frameTimes;
    Selection selection;
    // Entities hidden in the viewport, with their descendants; the game still shows them. Kept per
    // scene tab and in the project's editor settings.
    std::unordered_set<core::Uuid> hiddenEntities;
    // The entity renamed in the scene tree, and the name being typed.
    core::Uuid renamedEntity;
    std::string renameBuffer;
    bool focusRename = false;
    // The entities in the order the scene tree last listed them, which Shift+click ranges follow,
    // and the entity a range starts from.
    std::vector<core::Uuid> hierarchyOrder;
    core::Uuid rangeAnchor;
    // A click on a row of a selection of several, applied on release unless the rows are dragged.
    core::Uuid pendingRowClick;
    bool hierarchyFocused = false;

    // The 2D screen: how much of the reference resolution it shows, and the drag under way.
    float interfaceZoom = 1.0f;
    core::Uuid interfaceDragEntity;
    scene::UiRect interfaceDragStart;
    std::uint8_t interfaceHandle = 0;

    // A structural change requested while walking the scene, applied once the panels are drawn.
    std::unique_ptr<Command> pendingCommand;
    // A prefab to open in a tab, which replaces the edited scene: done before the next frame's panels.
    std::optional<asset::AssetId> prefabToOpen;
    // The entity that Save as Prefab turns into a prefab once its file is chosen.
    core::Uuid prefabEntity;

    // Value of the field being edited when the edit began, recorded as one undo step at the end;
    // with several entities selected, the value of each.
    serialization::TextValue fieldEditStart;
    std::vector<std::pair<core::Uuid, serialization::TextValue>> fieldEditStarts;
    // The text typed in a field whose value differs between the selected entities.
    std::string mixedTextBuffer;
    std::string nameEditStart;
    std::string nameBuffer;
    core::Uuid nameBufferEntity;
    // Rotations are edited as Euler angles, kept while the widget is active to avoid jumps.
    ImGuiID eulerEditId = 0;
    math::Vec3 eulerEditDegrees{0.0f};

    std::string consoleFilter;
    bool consoleShowDebug = true;
    bool consoleShowInfo = true;
    bool consoleShowWarnings = true;
    bool consoleShowErrors = true;
    bool consoleAutoScroll = true;

    // Editor.
    PlayState playState = PlayState::Editing;
    EditorRequests requests;
    GameCodeStatus gameCode;
    std::function<ProjectCodeStatus(const asset::Project&)> projectCodeStatus;
    DebuggerStatus debugger;
    // The C# Debugging window, and whether Play waits for a debugger.
    bool showDebugging = false;
    bool waitForDebugger = false;
    std::shared_ptr<DialogAnswers> dialogAnswers = std::make_shared<DialogAnswers>();
    std::string windowTitle;
    WindowLayout windowLayout = WindowLayout::Unset;
    // The editor's settings of the user (theme, projects), empty when there is no user data directory.
    std::filesystem::path userSettingsFile;
    ProjectList projects;
    ProjectManagerState projectManager;
    bool openAboutPopup = false;

    // Code.
    // The file of the code folder shown in the inspector, empty when an entity is selected.
    std::filesystem::path selectedCode;
    // A component the editor asked for, added to this entity once its code is compiled and loaded.
    std::string pendingScript;
    core::Uuid pendingScriptEntity;
    bool openNewScriptPopup = false;
    std::string newScriptName = "NewComponent";
    bool newScriptCSharp = true;

    // The project changed since the last update: its scenes open.
    bool projectChanged = false;
    // Scene tabs. The active tab's document is made of scenePath, the edited scene, history,
    // savedState, selection and camera.
    SceneTabs tabs;
    // The .dvxscene file of the edited scene, empty until it is saved.
    std::filesystem::path scenePath;
    std::uint64_t savedState = 0;
    std::optional<PendingAction> pendingAction;
    bool openUnsavedChangesPopup = false;
    // The pending action waits for play to stop.
    bool resumeActionAfterPlay = false;

    EditorCamera camera;
    Gizmo gizmo;
    EditorTool tool = EditorTool::Move;
    // Snapping without holding Ctrl, which then disables it.
    bool snap = false;
    bool showGrid = true;
    bool showIcons = true;
    // The collision shapes of every entity, rather than only those of the selection.
    bool showColliders = false;
    bool showProjectSettings = false;
    // The binding of the input settings waiting for a key or a gamepad button, by action and
    // binding, and the filter of the list bindings are chosen from.
    std::optional<std::pair<std::size_t, std::size_t>> listeningBinding;
    std::string inputSourceFilter;
    // The key pressed since the previous frame, whatever window had the keyboard, and the one of
    // this frame: the input settings bind it by its place, which ImGui does not tell.
    std::optional<platform::Key> notifiedKey;
    std::optional<platform::Key> pressedKey;
    // Edits of the project settings, saved once the edited field is released.
    std::optional<asset::Project> pendingProject;
    // Export.
    bool showExport = false;
    std::optional<asset::ExportSettings> pendingExport;
    std::vector<EngineBuildChoice> engineBuilds;
    ExportStatus exportStatus;
    GizmoHandle hoveredHandle = GizmoHandle::None;
    // The view of the last rendered frame, which mouse interactions refer to.
    ViewportView view;
    math::Extent2D viewportPixels;
    // Position of the viewport image on screen, in ImGui coordinates, and pixels per coordinate.
    math::Vec2 viewportOrigin{0.0f};
    float pixelsPerPoint = 1.0f;
    bool viewportHovered = false;
    bool viewportFocused = false;
    // Gives the viewport the keyboard on its next frame, as when play starts.
    bool focusViewport = false;
    bool flying = false;
    bool orbiting = false;
    bool panning = false;
    std::optional<math::Vec2> clickStart;
    // A click that moved became a rectangle, drawn until the button is released.
    bool drawingRectangle = false;
    // Sent with the next frame, then awaited until the renderer answers.
    std::optional<PickQuery> pickQuery;
    PickQuery awaitedQuery;
    std::uint64_t nextPickId = 1;
    std::uint64_t awaitedPick = 0;
    // The other selected entities that the gizmo moves, rotates or scales.
    std::vector<GizmoFollower> gizmoFollowers;
    // The entity a material dragged over the viewport would go to, as the last pick found it.
    core::Uuid materialTarget;
};

void drawHierarchyPanel(ToolsState& state, scene::Scene& scene);
void drawInspectorPanel(ToolsState& state, scene::Scene& scene);
// Whether a text holds a part, ignoring the case; an empty part matches everything.
[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part);
// The icon of a code file, by its extension.
[[nodiscard]] EntityIcon codeIcon(const std::filesystem::path& path);
// The files of the code folder of the project, under the assets.
void drawCodeFiles(ToolsState& state);
void openTextFile(ToolsState& state, const std::filesystem::path& path);
void showOpenTextDialog(ToolsState& state);
void drawTextEditorPanel(ToolsState& state, scene::Scene& scene);
[[nodiscard]] bool textEditorFocused();
[[nodiscard]] TextDocument* findTextDocument(ToolsState& state, const std::filesystem::path& path);
[[nodiscard]] std::vector<TextDocument*> affectedTextDocuments(ToolsState& state, const PendingAction& action);
[[nodiscard]] bool saveTextFile(ToolsState& state, scene::Scene& scene, TextDocument& document);
void discardPendingAction(ToolsState& state, scene::Scene& scene);
// The file selected in the code folder, shown read-only in the inspector.
void drawCodeInspector(ToolsState& state);
// The text of the open document: line numbers, colors, margin markers and the completion popup.
void drawCodeArea(ToolsState& state, TextDocument& document);
void drawFindBar(ToolsState& state, TextDocument& document);
void drawGoToLinePopup(ToolsState& state, TextDocument& document);
// Asks for a new component file and, once it is compiled, adds it to the entity.
void drawNewScriptPopup(ToolsState& state);
void updatePendingScript(ToolsState& state, scene::Scene& scene);
// How to attach a debugger to the C# code, and whether Play waits for one.
void drawDebuggingWindow(ToolsState& state);
// Opens a file in the code editor of the system.
void openInCodeEditor(ToolsState& state, const std::filesystem::path& file);
void drawStatisticsPanel(ToolsState& state, const scene::Scene& scene);
// Where the time of the recorded frames went, on the CPU and on the GPU, and what the loaded assets
// take.
void drawProfilerPanel(ToolsState& state);
void drawConsolePanel(ToolsState& state);
void drawAssetsPanel(ToolsState& state, scene::Scene& scene);
// The clips of the selected Animator: a timeline of their keys, played or scrubbed. Outside Play
// the panel poses the skeleton itself; during Play it follows the game.
void drawAnimationPanel(ToolsState& state, scene::Scene& scene);
// Shows an asset of the FileSystem in the inspector, in place of the selected entity or code file.
void selectAsset(ToolsState& state, asset::AssetId id);
// The selected audio clip: its format, its waveform, how it loads, and a preview.
void drawAudioClipInspector(ToolsState& state);
// The selected model: what its file brought, and how it imports (scale, textures).
void drawModelInspector(ToolsState& state);
void previewAudioClip(ToolsState& state, asset::AssetId clip);
void stopAudioPreview(ToolsState& state);

// Editor.
void drawViewportPanel(ToolsState& state, scene::Scene& scene);
// The 2D screen: the canvases of the scene, where their elements are moved and resized.
void drawInterfacePanel(ToolsState& state, scene::Scene& scene);
void drawProjectManager(ToolsState& state);
void drawEditorMenus(ToolsState& state, scene::Scene& scene);
// Shows a screen in the middle of the window: the panel it needs opens and takes the focus.
void setMainScreen(ToolsState& state, MainScreen screen);
// The window of a screen, as the dock builder and the focus use it.
[[nodiscard]] const char* windowOf(MainScreen screen) noexcept;
void drawStatusBar(ToolsState& state, const scene::Scene& scene);
void drawSettingsWindow(ToolsState& state);
void drawProjectSettingsWindow(ToolsState& state);
// The Input page of the project settings: contexts, actions and their bindings.
void drawInputSettings(ToolsState& state, asset::InputSettings& input);
void drawExportWindow(ToolsState& state);
void drawEditorPopups(ToolsState& state, scene::Scene& scene);
void handleEditorShortcuts(ToolsState& state, scene::Scene& scene);
// Opens the project's scenes when the project changed, handles dialog answers and pick results.
void updateEditorSession(ToolsState& state, scene::Scene& scene);
void updateWindowTitle(ToolsState& state, const scene::Scene& scene);
void addEditorOverlay(ToolsState& state, scene::Scene& scene, render::RenderWorld& world);
// Loads the theme and the project list from the file, or from the user's data directory when the
// file is empty.
void loadUserSettings(ToolsState& state, const std::filesystem::path& file);
void saveUserSettings(const ToolsState& state);
// Remembers the open scenes of the project and the editor camera.
void saveEditorSettings(ToolsState& state);

// Scene tabs.
// A small lit scene to start from: a sun, a sky, a camera, a ground and a cube.
[[nodiscard]] scene::Scene makeDefaultScene();
[[nodiscard]] ActiveDocument activeDocument(ToolsState& state, scene::Scene& scene) noexcept;
// The name shown for a tab: its file name, or "[unsaved]".
[[nodiscard]] std::string tabName(const std::filesystem::path& path);
void newSceneTab(ToolsState& state, scene::Scene& scene);
// Opens a scene in a new tab, or shows its tab when it is already open.
void openSceneTab(ToolsState& state, scene::Scene& scene, const std::filesystem::path& path);
void activateSceneTab(ToolsState& state, scene::Scene& scene, std::size_t index);
// Asks about unsaved changes first; the actions of the project and of quitting also stop play.
void requestAction(ToolsState& state, scene::Scene& scene, PendingAction action);
[[nodiscard]] bool hasUnsavedChanges(ToolsState& state, scene::Scene& scene);
// The tabs whose unsaved changes the pending action would drop.
[[nodiscard]] std::vector<std::size_t> tabsWithUnsavedChanges(ToolsState& state, scene::Scene& scene);
// Saves the scenes the pending action would drop. An untitled scene comes to the screen with its
// save dialog, and the action continues once it is saved. Returns whether every scene was saved.
[[nodiscard]] bool saveForPendingAction(ToolsState& state, scene::Scene& scene);
// Runs the pending action if nothing unsaved stands in its way anymore, or asks again.
void continuePendingAction(ToolsState& state, scene::Scene& scene);
void cancelPendingAction(ToolsState& state);
[[nodiscard]] bool saveScene(ToolsState& state, scene::Scene& scene);
void saveAllScenes(ToolsState& state, scene::Scene& scene);
void showSaveSceneDialog(ToolsState& state);
void showOpenSceneDialog(ToolsState& state);
void frameSelection(ToolsState& state, const scene::Scene& scene);
[[nodiscard]] core::Uuid uuidFromBytes(const std::array<std::uint8_t, 16>& bytes) noexcept;

// The create menu of entities: empty, primitives, lights, camera, environment. Created entities go
// under parent (nil for a root), at the editor camera's pivot.
void drawCreateEntityMenu(ToolsState& state, core::Uuid parent);

// A combo listing the assets of a type (any type without one), which also accepts dropped assets.
// Returns whether the value changed. A mixed value shows a dash.
bool drawAssetPicker(ToolsState& state, const char* id, std::optional<asset::AssetType> type, asset::AssetId& value,
                     bool mixed = false);

// Makes the last item a drag source for the asset.
void dragAsset(asset::AssetId id, asset::AssetType type, const std::string& label);
// Accepts an asset dropped on the last item, of the given type when one is given.
[[nodiscard]] std::optional<asset::AssetId> acceptDroppedAsset(
    std::optional<asset::AssetType> type = std::nullopt);

// Queues the creation of the model's entities under parent (nil for a root), at a position relative
// to it when one is given, and selects them.
void requestInstantiateModel(ToolsState& state, asset::AssetId model, core::Uuid parent,
                             std::optional<math::Vec3> position = std::nullopt);

// Queues the creation of an instance of the prefab under parent (nil for a root), at a position
// relative to it when one is given, and selects it. A scene cannot contain itself.
void requestInstantiatePrefab(ToolsState& state, asset::AssetId prefab, core::Uuid parent,
                              std::optional<math::Vec3> position = std::nullopt);

// Asks where to save the entity and its descendants as a prefab, which then replaces them with an
// instance of it.
void showSaveAsPrefabDialog(ToolsState& state, const scene::Scene& scene, core::Uuid entity);

// The name of a scene asset: its file name without its extension.
[[nodiscard]] std::string sceneAssetName(const ToolsState& state, asset::AssetId sceneAsset);

// Queues the creation of an entity under parent (nil for a root) and selects it.
void requestCreateEntity(ToolsState& state, core::Uuid parent);

// The selection copied to the clipboard of the system, cut, pasted beside the active entity,
// duplicated beside itself or deleted, each as one undo step. Pasted and duplicated entities are
// selected, and renamed when a sibling has their name.
void copySelection(ToolsState& state, const scene::Scene& scene);
void cutSelection(ToolsState& state, scene::Scene& scene);
void pasteEntities(ToolsState& state, scene::Scene& scene);
void duplicateSelection(ToolsState& state, scene::Scene& scene);
void deleteSelection(ToolsState& state, scene::Scene& scene);
void selectAll(ToolsState& state, const scene::Scene& scene);
// Whether the selection holds something that deleting or cutting may remove: the entities of a
// prefab instance stay with it.
[[nodiscard]] bool canDeleteSelection(const ToolsState& state, const scene::Scene& scene);
// A name for an entity under parent that none of its children has, nor any of the names taken:
// "Crate 3" becomes "Crate 4", "Crate" becomes "Crate 2".
[[nodiscard]] std::string uniqueChildName(const scene::Scene& scene, scene::Entity parent, std::string_view name,
                                          const std::vector<std::string>& taken = {});
// Changes the selection as a click or a rectangle does.
void selectEntities(ToolsState& state, std::span<const core::Uuid> entities, SelectMode mode);
// Starts renaming the entity in the scene tree.
void startRename(ToolsState& state, core::Uuid entity);

// Whether the entity or one of its ancestors is hidden in the viewport.
[[nodiscard]] bool isHidden(const ToolsState& state, const scene::Scene& scene, scene::Entity entity);
// Hides the selected entities, or shows them again when all of them are hidden.
void toggleSelectionHidden(ToolsState& state, const scene::Scene& scene);
// The items of the Edit menu that act on the selected entities.
void drawEntityEditMenuItems(ToolsState& state, scene::Scene& scene);
// The shortcuts that act on the selected entities, while the scene tree or the viewport has the
// keyboard: cut, copy, paste, duplicate, rename, hide, select all and delete.
void handleEntityShortcuts(ToolsState& state, scene::Scene& scene);

// "vertical_fov" becomes "Vertical fov".
[[nodiscard]] std::string displayName(std::string_view identifier);

} // namespace devex::tools::detail
