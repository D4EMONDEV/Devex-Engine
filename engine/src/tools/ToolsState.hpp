#pragma once

#include <devex/core/Export.hpp>

#include "CodeArea.hpp"
#include "EditorDock.hpp"
#include "EditorHosts.hpp"
#include "EditorInput.hpp"
#include "EditorCamera.hpp"
#include "EditorView.hpp"
#include "Gizmo.hpp"
#include "Icons.hpp"
#include "ProjectList.hpp"
#include "SceneTabs.hpp"
#include "ScriptSettings.hpp"
#include "Selection.hpp"
#include "TextDocument.hpp"
#include "Theme.hpp"

#include <devex/asset/AnimatorData.hpp>
#include <devex/asset/AssetId.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/asset/NavMeshData.hpp>
#include <devex/asset/AssetMemory.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/asset/AudioClipData.hpp>
#include <devex/asset/CurveData.hpp>
#include <devex/asset/SpriteData.hpp>
#include <devex/asset/TilesetData.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Time.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/math/Math.hpp>
#include <devex/navigation/NavMeshBuilder.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/tools/CommandHistory.hpp>
#include <devex/tools/LogBuffer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

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

namespace devex::particles {
class ParticleWorld;
} // namespace devex::particles

namespace devex::animation {
class AnimationWorld;
class Clip;
} // namespace devex::animation

namespace devex::navigation {
class NavigationWorld;
} // namespace devex::navigation

namespace devex::scene {
struct NavMeshSurface;
} // namespace devex::scene

namespace devex::audio {
class AudioEngine;
class Clip;
} // namespace devex::audio

namespace devex::tools::detail {

class EditorUiKit;
struct ProjectManagerUi;
struct FileSystemUi;
struct OutputUi;
struct CreationDialogUi;
struct SceneTreeUi;
struct InspectorUi;
struct EditorSettingsUi;
struct ProjectSettingsUi;
struct ExportUi;
struct DebuggingUi;
struct EditorDialogsUi;
struct StatisticsUi;
struct ProfilerUi;
struct AnimationUi;
struct ScriptUi;
struct ViewportOverlayUi;
struct EditorDockUi;
struct ModalLayerUi;

// What the editor draws over the image of the viewport this frame, in points of the window; the
// overlay of the viewport draws it.
struct DEVEX_API ViewportMarks
{
    // The rectangle a drag selects in.
    std::optional<std::pair<math::Vec2, math::Vec2>> selecting;
    // What the game shows, when no camera draws its own box.
    std::optional<std::pair<math::Vec2, math::Vec2>> gameFrame;
    // The element of an interface that is selected, its handles and its anchors.
    std::optional<std::pair<math::Vec2, math::Vec2>> element;
    std::vector<math::Vec2> handles;
    std::vector<math::Vec2> anchors;
    float handleRadius = 5.0f;
    // A word on what the screen shows, at its bottom.
    std::string hint;
    // The game runs: it is framed in the accent colour.
    bool playing = false;
};
struct AnimatorUi;
struct MenuBarUi;
struct StatusBarUi;
struct ViewportHeaderUi;
struct EditorLayerUi;

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
inline constexpr const char* animatorWindow = "Animator";
inline constexpr const char* textEditorWindow = "Text Editor";
inline constexpr const char* profilerWindow = "Profiler";

// Payload type of an entity dragged in the hierarchy: the 16 bytes of its UUID.
inline constexpr const char* entityPayload = "DEVEX_ENTITY";
// Payload type of an asset dragged from the assets panel.
inline constexpr const char* assetPayload = "DEVEX_ASSET";

struct DEVEX_API AssetPayload
{
    std::array<std::uint8_t, 16> uuid{};
    asset::AssetType type = asset::AssetType::Mesh;
};

// Recent frame times, for the statistics graph.
class DEVEX_API FrameTimes
{
public:
    void record(float milliseconds) noexcept;
    [[nodiscard]] float average() const noexcept;
    [[nodiscard]] float maximum() const noexcept;
    // Values in recording order starting at offset.
    [[nodiscard]] const float* values() const noexcept;
    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] int offset() const noexcept;

private:
    std::array<float, 240> m_milliseconds{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
};

// The last values of a measure, one a frame, which the monitors of the Statistics panel draw.
class DEVEX_API MonitorHistory
{
public:
    void record(float value) noexcept;
    // Oldest first.
    void values(std::vector<float>& out) const;
    [[nodiscard]] float last() const noexcept;
    [[nodiscard]] float maximum() const noexcept;
    [[nodiscard]] std::size_t count() const noexcept;

private:
    std::array<float, 240> m_values{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
};

// The measures of the Statistics panel, recorded every frame whether it shows or not.
struct DEVEX_API Monitors
{
    // In milliseconds.
    MonitorHistory frameTime;
    MonitorHistory drawCalls;
    MonitorHistory culled;
    // In megabytes: the memory of the GPU the engine takes, and what it sent to the GPU last frame.
    MonitorHistory gpuMemory;
    MonitorHistory uploads;
    MonitorHistory entities;
};

// What the Profiler panel keeps from one frame to the next.
struct DEVEX_API ProfilerView
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
struct DEVEX_API PickQuery
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
struct DEVEX_API GizmoFollower
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
struct DEVEX_API PendingAction
{
    enum class Kind : std::uint8_t
    {
        // Closes scene tabs.
        CloseTab,
        CloseText,
        ReloadText,
        OpenProject,
        // Goes back to the project manager.
        CloseProject,
        Quit,
    };

    Kind kind = Kind::CloseTab;
    // The tabs to close.
    std::vector<std::uint64_t> tabs;
    // The project to open, or the text file to close/reload.
    std::filesystem::path path;
};

// Answers of native file dialogs, which arrive between frames. Dialogs hold it weakly, so that an
// answer arriving after the tools are gone is dropped.
// What the Create Entity and Add Component window was opened for.
struct DEVEX_API CreationRequest
{
    bool addComponent = false;
    // The entity the new one goes under, nil for a root.
    core::Uuid parent;
    // The entities a component is added to.
    std::vector<core::Uuid> targets;
};

struct DEVEX_API DialogAnswers
{
    std::optional<std::filesystem::path> importProject;
    std::optional<std::filesystem::path> scanFolder;
    std::optional<std::filesystem::path> newProjectLocation;
    std::optional<std::filesystem::path> openScene;
    std::optional<std::filesystem::path> openText;
    std::optional<std::filesystem::path> saveSceneAs;
    std::optional<std::filesystem::path> saveAsPrefab;
    std::optional<std::filesystem::path> exportFolder;
    std::optional<std::pair<std::size_t, std::filesystem::path>> scriptEditor;
};

// What the project manager shows of a project, read from its file.
struct DEVEX_API ProjectInfo
{
    std::string name;
    bool exists = false;
    ProjectCodeStatus code;
    // When the project file last changed, in seconds since 1970.
    std::int64_t modified = 0;
};

struct DEVEX_API ProjectManagerState
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
    // The viewport facing the XY plane: 2D games and the interfaces of the scene.
    TwoD,
    // The viewport in perspective.
    ThreeD,
    Script,
};

[[nodiscard]] DEVEX_API std::string_view toString(MainScreen screen) noexcept;

// Whether the window is sized for the project manager or for the editor.
enum class WindowLayout : std::uint8_t
{
    Unset,
    ProjectManager,
    Editor,
};

// The curve the inspector edits: a copy of its file, saved when a change is over.
// The tileset the inspector edits: a copy of its file, saved when a change is over.
struct DEVEX_API TilesetEditor
{
    asset::AssetId asset;
    std::filesystem::path file;
    // When the file was read or written, to read it again once changed elsewhere.
    std::filesystem::file_time_type fileTime{};
    asset::TilesetData tileset;
    std::string error;
    // The tile the inspector shows, 0 for none.
    std::uint32_t selectedTile = 0;
};

// What the graph of the Animator panel has selected.
enum class AnimatorElement : std::uint8_t
{
    None,
    State,
    Transition,
    Entry,
    AnyState,
};

// The animator controller the Animator panel edits: a copy of its file, saved when a change is over,
// with its own undo history.
struct DEVEX_API AnimatorEditor
{
    asset::AssetId asset;
    std::filesystem::path file;
    // When the file was read or written, to read it again once changed elsewhere.
    std::filesystem::file_time_type fileTime{};
    asset::AnimatorData animator;
    // As the file holds it: what an undo step goes back to.
    asset::AnimatorData saved;
    std::vector<asset::AnimatorData> undo;
    std::vector<asset::AnimatorData> redo;
    // Why the file could not be read, or why the last change is not saved.
    std::string error;
    AnimatorElement selected = AnimatorElement::None;
    // The state or the transition selected.
    std::int32_t index = -1;
    // The inspector shows the element selected, until another entity or asset is selected.
    bool inspecting = false;
    core::Uuid inspectedEntity;
    asset::AssetId inspectedAsset;
    // The graph: where its origin sits in the panel, in pixels, and how large it is drawn.
    math::Vec2 pan{280.0f, 120.0f};
    float zoom = 1.0f;
    // Fits the view to the graph at the next frame: when another controller opens, or asked.
    bool frame = true;
    // A transition being drawn from a state (-1: from Any State), to the state clicked next.
    std::optional<std::int32_t> connectingFrom;
    // A node being dragged, and whether it moved.
    AnimatorElement dragged = AnimatorElement::None;
    std::int32_t draggedIndex = -1;
    bool panning = false;
    // The entity whose Animator the panel follows, for its state while the game plays.
    core::Uuid entity;
    // The panel had the keyboard focus last frame: its undo takes Ctrl+Z.
    bool focused = false;
};

// How the viewport paints the cells of the selected tilemap.
enum class TileTool : std::uint8_t
{
    // The viewport selects and moves entities, as usual.
    None,
    Paint,
    Erase,
    // Fills the rectangle dragged.
    Rectangle,
    // Fills the cells like the one clicked that touch it.
    Fill,
    // Takes the tile of the cell clicked, then paints with it.
    Pick,
};

struct DEVEX_API TilePainter
{
    TileTool tool = TileTool::None;
    // The tile painted with, and how it is mirrored.
    std::uint32_t tile = 0;
    bool flipX = false;
    bool flipY = false;
    // While the mouse is held: the tilemap painted, its cells before, the cell the stroke reached,
    // and where a rectangle started.
    bool stroking = false;
    core::Uuid target;
    serialization::TextValue before;
    math::IVec2 lastCell{0};
    std::optional<math::IVec2> rectangleStart;
    // The cell under the mouse, shown in the viewport.
    std::optional<math::IVec2> hovered;
};

// The sprite frames the inspector edits: a copy of their file, saved when a change is over.
struct DEVEX_API SpriteFramesEditor
{
    asset::AssetId asset;
    std::filesystem::path file;
    // When the file was read or written, to read it again once changed elsewhere.
    std::filesystem::file_time_type fileTime{};
    asset::SpriteFramesData frames;
    std::string error;
    int selectedAnimation = 0;
    // The frame shown while the preview is paused; -1 for none.
    int selectedFrame = -1;
    // The name of the selected animation while it is typed.
    std::string renaming;
    bool previewPlaying = true;
    float previewTime = 0.0f;
};

struct DEVEX_API CurveEditor
{
    asset::AssetId asset;
    std::filesystem::path file;
    // When the file was read or written, to read it again once changed elsewhere.
    std::filesystem::file_time_type fileTime{};
    asset::CurveData curve;
    std::string error;
    int selectedKey = -1;
    // The key being dragged, -1 for none; its point (0) or the handle of a slope (-1 in, 1 out).
    int draggedKey = -1;
    int draggedPart = 0;
    bool moved = false;
    // The values the graph shows, held while a key moves so that the graph does not follow it.
    float low = 0.0f;
    float high = 1.0f;
};

struct DEVEX_API ToolsState
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
    // Appearance.
    IconSet icons;
    // The fonts, icons and theme the panels made with the interface of the engine share, and the
    // project manager, the first of them.
    std::shared_ptr<EditorUiKit> uiKit;
    std::shared_ptr<ProjectManagerUi> projectManagerUi;
    std::shared_ptr<FileSystemUi> fileSystemUi;
    std::shared_ptr<OutputUi> outputUi;
    std::shared_ptr<CreationDialogUi> creationDialog;
    std::shared_ptr<SceneTreeUi> sceneTreeUi;
    std::shared_ptr<InspectorUi> inspectorUi;
    // The windows that float over the others, and the dialogs.
    std::shared_ptr<EditorSettingsUi> editorSettingsUi;
    std::shared_ptr<ProjectSettingsUi> projectSettingsUi;
    std::shared_ptr<ExportUi> exportUi;
    std::shared_ptr<DebuggingUi> debuggingUi;
    std::shared_ptr<EditorDialogsUi> dialogsUi;
    // The panels of measures.
    std::shared_ptr<StatisticsUi> statisticsUi;
    std::shared_ptr<ProfilerUi> profilerUi;
    std::shared_ptr<AnimationUi> animationUi;
    std::shared_ptr<ScriptUi> scriptUi;
    std::shared_ptr<ViewportOverlayUi> viewportOverlayUi;
    ViewportMarks viewportMarks;
    std::shared_ptr<AnimatorUi> animatorUi;
    // The frame of the editor: its bars, the header of the view, and the layer of their menus.
    std::shared_ptr<MenuBarUi> menuBarUi;
    // A menu of the menu bar to open at its next update, by the place of its title.
    std::optional<std::size_t> menuRequest;
    std::shared_ptr<StatusBarUi> statusBarUi;
    std::shared_ptr<ViewportHeaderUi> viewportHeaderUi;
    std::shared_ptr<EditorLayerUi> editorLayerUi;
    // Opens the window at the next frame.
    std::optional<CreationRequest> creationRequest;
    ThemeSettings theme;
    ScriptSettings scripts;
    bool scriptSettingsUnsaved = false;
    // The theme changed and applies before the next frame.
    bool themeChanged = true;
    // The theme changed since the user's settings were last written.
    bool themeUnsaved = false;
    float appliedDisplayScale = 0.0f;

    bool visible = false;
    bool capturesKeyboard = false;
    bool capturesMouse = false;
    bool resetLayout = false;
    // The keyboard and the mouse this frame, as the platform saw them, and the seconds the tools
    // have run for.
    EditorInput input;
    double clock = 0.0;
    // The places of the screen the editor shows its images in, and where the strips of the frame and
    // the room between them stand this frame, in points.
    EditorHosts hosts;
    math::Vec2 menuBarMin{0.0f, 0.0f};
    math::Vec2 menuBarMax{0.0f, 0.0f};
    math::Vec2 statusBarMin{0.0f, 0.0f};
    math::Vec2 statusBarMax{0.0f, 0.0f};
    math::Vec2 workMin{0.0f, 0.0f};
    math::Vec2 workMax{0.0f, 0.0f};
    // Whether a field took what is typed at the last frame: the shortcuts without Ctrl leave it the
    // letters.
    bool typing = false;
    // Where the panels stand, where the dock put them this frame, and the panel the keyboard goes to
    // next; a change of the layout is saved once the mouse lets go.
    DockLayout dock;
    DockPlaces dockPlaces;
    std::shared_ptr<EditorDockUi> dockUi;
    std::string panelToFocus;
    bool dockChanged = false;
    // The modals open, the last one on top; those asked for since the last frame began; the one its
    // cross or Escape closed.
    std::vector<std::string> modals;
    std::vector<std::string> modalsAsked;
    std::string modalToClose;
    std::shared_ptr<ModalLayerUi> modalLayerUi;

    bool showHierarchy = true;
    bool showInspector = true;
    bool showStatistics = true;
    bool showConsole = true;
    bool showAssets = true;
    bool showViewport = true;
    bool showSettings = false;
    // The screen the middle of the window shows, chosen in the menu bar or by what the editor
    // opens: a scene shows the viewport in the screen it was left in, 2D or 3D, a file shows the
    // text editor.
    MainScreen mainScreen = MainScreen::ThreeD;
    // The screen was chosen this frame: its window takes the focus of the middle.
    bool mainScreenChanged = false;
    bool showTextEditor = false;
    bool focusTextEditor = false;
    bool selectTextTab = false;
    // Opening a document moves the others in memory: hold the path of a document across a frame,
    // never a pointer or a reference to it.
    std::vector<TextDocument> textDocuments;
    // What the editor keeps for the document it shows: cursor, search, completions.
    TextEditState textEdit;
    // The errors and warnings of the last build of the game code, shown in the margin.
    std::vector<CodeDiagnostic> codeDiagnostics;
    // The file the Script screen shows, among those that are open.
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
    // The curve the inspector edits, as its file holds it.
    CurveEditor curveEditor;
    SpriteFramesEditor spriteFramesEditor;
    TilesetEditor tilesetEditor;
    AnimatorEditor animatorEditor;
    TilePainter tilePainter;
    // A file just created, selected once it is imported, as a res:// path.
    std::string assetToSelect;
    // A file the FileSystem shows at its next update, as a res:// path: revealInFileSystem.
    std::string assetToReveal;
    // Animations: where clips come from, and the animations of the game while it plays.
    std::function<std::shared_ptr<const animation::Clip>(asset::AssetId)> animationClips;
    // The themes of interfaces, for the inspector to show what a style sets.
    std::function<std::shared_ptr<const asset::ThemeData>(asset::AssetId)> themes;
    // Previews of textures and sprites: textures as the renderer holds them, their sizes, and where
    // sprites lie on them. Loading starts when they are asked for.
    std::function<render::TextureHandle(asset::AssetId)> textures;
    std::function<math::Extent2D(asset::AssetId)> textureSizes;
    std::function<std::shared_ptr<const asset::SpriteData>(asset::AssetId)> sprites;
    std::function<std::shared_ptr<const asset::TilesetData>(asset::AssetId)> tilesets;
    // Navigation: the meshes of mesh colliders, to bake, and baked navigation meshes, with the lines
    // of the one last drawn and what the last bake said.
    std::function<const asset::MeshData*(asset::AssetId)> meshes;
    std::function<std::shared_ptr<const asset::NavMeshData>(asset::AssetId)> navMeshes;
    std::shared_ptr<const asset::NavMeshData> navMeshDrawn;
    navigation::NavMeshLines navMeshLines;
    std::string navMeshBakeStatus;
    bool navMeshBakeFailed = false;
    // What the loaded assets take, for the Profiler panel.
    std::function<asset::MemoryReport()> memoryReport;
    // The meshes and textures loading in the background, for the status bar.
    std::function<std::size_t()> pendingLoads;
    animation::AnimationWorld* animationWorld = nullptr;
    navigation::NavigationWorld* navigationWorld = nullptr;
    // The particles of the scene shown, previewed in the editor; null without them.
    particles::ParticleWorld* particleWorld = nullptr;
    bool showAnimation = false;
    bool showAnimator = false;
    // The profiler records while its panel is open.
    bool showProfiler = false;
    // The Profiler panel comes to the front of its dock at its next frame, as when it is opened.
    bool focusProfiler = false;
    // The same for the panels of the clips and of the state machines.
    bool focusAnimation = false;
    bool focusAnimator = false;
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
    Monitors monitors;
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

    // The 2D screen: where it draws the interfaces this frame, and the drag of an element under way.
    std::optional<InterfaceFrame> interfaceFrame;
    core::Uuid interfaceDragEntity;
    scene::UiRect interfaceDragStart;
    std::uint8_t interfaceHandle = 0;

    // A structural change requested while walking the scene, applied once the panels are drawn.
    std::unique_ptr<Command> pendingCommand;
    // A prefab to open in a tab, which replaces the edited scene: done before the next frame's panels.
    std::optional<asset::AssetId> prefabToOpen;
    // The entity that Save as Prefab turns into a prefab once its file is chosen.
    core::Uuid prefabEntity;

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
    core::Uuid newScriptTarget;
    bool openCreateFilePopup = false;
    std::string newFileFolder = "res://assets";
    std::string newFileName;

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
    // A res:// path captured when Delete was requested; acted on only after confirmation.
    std::string fileToDelete;
    bool openDeleteFilePopup = false;
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
    // The interfaces of a 3D scene over its 3D screen, as the game will draw them.
    bool showInterfaces = true;
    // The kind of the edited scene on the previous frame: the screen follows it when it changes.
    std::optional<scene::SceneKind> shownSceneKind;
    bool showProjectSettings = false;
    // The key pressed since the previous frame, whatever window had the keyboard, and the one of
    // this frame: the input settings bind it by its place, which the letters typed do not tell.
    std::optional<platform::Key> notifiedKey;
    std::optional<platform::Key> pressedKey;
    // Export.
    bool showExport = false;
    std::vector<EngineBuildChoice> engineBuilds;
    ExportStatus exportStatus;
    GizmoHandle hoveredHandle = GizmoHandle::None;
    // The view of the last rendered frame, which mouse interactions refer to.
    ViewportView view;
    math::Extent2D viewportPixels;
    // Position of the viewport image in points of the window, and pixels per point.
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

DEVEX_API void drawHierarchyPanel(ToolsState& state, scene::Scene& scene);
DEVEX_API void drawInspectorPanel(ToolsState& state, scene::Scene& scene);
// Whether a text holds a part, ignoring the case; an empty part matches everything.
[[nodiscard]] DEVEX_API bool containsIgnoringCase(std::string_view text, std::string_view part);
// The icon of a code file, by its extension.
[[nodiscard]] DEVEX_API EntityIcon codeIcon(const std::filesystem::path& path);
DEVEX_API void openTextFile(ToolsState& state, const std::filesystem::path& path);
DEVEX_API void showOpenTextDialog(ToolsState& state);
DEVEX_API void drawTextEditorPanel(ToolsState& state, scene::Scene& scene);
[[nodiscard]] DEVEX_API bool textEditorFocused(const ToolsState& state);
DEVEX_API void renderTextEditor(ToolsState& state, render::RenderWorld& world);
[[nodiscard]] DEVEX_API TextDocument* findTextDocument(ToolsState& state, const std::filesystem::path& path);
[[nodiscard]] DEVEX_API std::vector<TextDocument*> affectedTextDocuments(ToolsState& state, const PendingAction& action);
[[nodiscard]] DEVEX_API bool saveTextFile(ToolsState& state, scene::Scene& scene, TextDocument& document);
DEVEX_API void discardPendingAction(ToolsState& state, scene::Scene& scene);
// The text of the open document: line numbers, colors, margin markers and the completion popup.
DEVEX_API void drawCodeArea(ToolsState& state, TextDocument& document);
DEVEX_API void drawFindBar(ToolsState& state, TextDocument& document);
DEVEX_API void drawGoToLinePopup(ToolsState& state, TextDocument& document);
// Adds the component asked for in New Script to its entity, once its code is compiled and loaded.
DEVEX_API void updatePendingScript(ToolsState& state, scene::Scene& scene);
// How to attach a debugger to the C# code, and whether Play waits for one.
DEVEX_API void drawDebuggingWindow(ToolsState& state);
DEVEX_API void renderDebuggingWindow(ToolsState& state, render::RenderWorld& world);
// Opens a file in the code editor of the system.
DEVEX_API void openInCodeEditor(ToolsState& state, const std::filesystem::path& file);
DEVEX_API void openInPreferredEditor(ToolsState& state, const std::filesystem::path& file);
// The device and the monitors of the measures of the frames, a curve each, as Godot's Monitors.
DEVEX_API void drawStatisticsPanel(ToolsState& state, const scene::Scene& scene);
// Records the measures of the monitors, once a frame.
DEVEX_API void recordMonitors(ToolsState& state, const scene::Scene& scene, core::Duration frameDelta);
DEVEX_API void renderStatistics(ToolsState& state, render::RenderWorld& world);
// Where the time of the recorded frames went, on the CPU and on the GPU, and what the loaded assets
// take.
DEVEX_API void drawProfilerPanel(ToolsState& state);
DEVEX_API void renderProfiler(ToolsState& state, render::RenderWorld& world);
DEVEX_API void drawConsolePanel(ToolsState& state);
DEVEX_API void drawAssetsPanel(ToolsState& state, scene::Scene& scene);
DEVEX_API void refreshFileSystem(ToolsState& state);
DEVEX_API void requestNewScript(ToolsState& state, bool addToSelection = false);
DEVEX_API std::vector<std::string> creationFolders(const asset::Project& project, asset::ContentRoot content);
DEVEX_API core::Result<std::filesystem::path> createContentFolder(ToolsState& state, std::string_view parent, std::string_view name);
DEVEX_API core::Result<std::filesystem::path> writeNewAssetFile(ToolsState& state, std::string_view folder,
    std::string_view requestedName, std::string_view defaultName, std::string_view extension, std::string_view text);
DEVEX_API core::Result<void> deleteFileSystemPath(ToolsState& state, scene::Scene& scene, std::string_view resourcePath);
// The clips of the selected Animator: a timeline of their keys, played or scrubbed. Outside Play
// the panel poses the skeleton itself; during Play it follows the game.
DEVEX_API void drawAnimationPanel(ToolsState& state, scene::Scene& scene);
DEVEX_API void renderAnimationPanel(ToolsState& state, render::RenderWorld& world);
// Shows an asset of the FileSystem in the inspector, in place of the selected entity or code file.
DEVEX_API void selectAsset(ToolsState& state, asset::AssetId id);
// Brings the FileSystem to the front, opens the folders around a res:// file and shows its line,
// chosen, as Godot's Show in FileSystem. The inspector keeps what it shows.
DEVEX_API void revealInFileSystem(ToolsState& state, std::string resource);
// The sprites a texture was cut into, in the order of its cells.
[[nodiscard]] DEVEX_API std::vector<asset::AssetId> spritesOfTexture(const ToolsState& state, asset::AssetId texture);
// Writes a new tileset into a res:// folder of the assets, and selects it once imported. From a
// texture, it holds a tile for each of its sprites.
DEVEX_API core::Result<std::filesystem::path> createTilesetFile(ToolsState& state, std::string_view folder,
                                                                asset::AssetId fromTexture = {}, std::string_view name = {});
// The Animator panel: the graph of the states and transitions of an animator controller, with its
// parameters, followed live while the game plays. It edits the controller of the Animator of the
// selected entity, or the animator selected in the FileSystem.
DEVEX_API void drawAnimatorPanel(ToolsState& state, scene::Scene& scene);
DEVEX_API void renderAnimatorPanel(ToolsState& state, render::RenderWorld& world);
// Writes a new animator controller into a res:// folder of the assets, and selects it once imported.
DEVEX_API core::Result<std::filesystem::path> createAnimatorFile(ToolsState& state, std::string_view folder, std::string_view name = {});
// Under the NavMeshSurface of the inspected entity: what its navigation mesh holds, and whether its
// settings changed since it was baked.
struct DEVEX_API NavMeshSummary
{
    std::string text;
    bool stale = false;
};
[[nodiscard]] DEVEX_API NavMeshSummary navMeshSummary(ToolsState& state, const scene::NavMeshSurface& surface);
// Bakes the navigation mesh of the surface again from the colliders of the scene, next to the scene
// file, or forgets it, each as one undoable step; state.navMeshBakeStatus says how it went.
DEVEX_API void bakeNavMesh(ToolsState& state, scene::Scene& scene, scene::Entity entity);
DEVEX_API void clearNavMesh(ToolsState& state, scene::Scene& scene, scene::Entity entity);
// The navigation meshes of the surfaces shown, the agents and obstacles, and the paths agents walk
// while the game plays, as lines of the overlay.
DEVEX_API void addNavigationLines(ToolsState& state, scene::Scene& scene, const std::unordered_set<std::uint32_t>& shown, bool showAll,
                                  std::vector<render::OverlayVertex>& lines);
// Paints the selected tilemap with the mouse when a tool is chosen. Returns whether it took the
// mouse, which the selection and the gizmo then leave alone.
DEVEX_API bool handleTilePainting(ToolsState& state, scene::Scene& scene, const ViewportView& view, math::Vec2 mouse, bool hovered);
// The cells around the mouse and the tile about to be painted.
DEVEX_API void addTilePainterOverlay(ToolsState& state, scene::Scene& scene, render::RenderWorld& world);
// Writes new sprite frames into a res:// folder of the assets, and selects them once imported. From a
// texture, they hold one animation of all its sprites.
DEVEX_API core::Result<std::filesystem::path> createSpriteFramesFile(ToolsState& state, std::string_view folder,
                                                                     asset::AssetId fromTexture = {}, std::string_view name = {});
// Writes a new curve into a res:// folder of the assets, and selects it once imported.
DEVEX_API core::Result<std::filesystem::path> createCurveFile(ToolsState& state, std::string_view folder, std::string_view name = {});
DEVEX_API void previewAudioClip(ToolsState& state, asset::AssetId clip);
DEVEX_API void stopAudioPreview(ToolsState& state);

// Editor.
DEVEX_API void drawViewportPanel(ToolsState& state, scene::Scene& scene);
// The 2D screen: picks, moves and resizes the elements of the interfaces under the mouse, before
// the entities of the world. Answers whether it took the mouse.
DEVEX_API bool handleInterfaceEditing(ToolsState& state, scene::Scene& scene, bool hovered);
// The 2D screen: the frame of the game when no camera draws it, and the handles of the selected
// element, left in the marks of the viewport for its overlay.
DEVEX_API void drawInterfaceOverlay(ToolsState& state, const scene::Scene& scene);
// The rectangle of the XY plane the selected elements of the interfaces cover in the 2D screen.
[[nodiscard]] DEVEX_API std::optional<std::pair<math::Vec2, math::Vec2>> selectedInterfaceBounds(const ToolsState& state,
                                                                                               const scene::Scene& scene);
// Shows the viewport in the screen of the kind of the active scene, 2D or 3D.
DEVEX_API void showSceneScreen(ToolsState& state, const scene::Scene& scene);
DEVEX_API void drawProjectManager(ToolsState& state);
// Adds the image of the project manager to the frame, when it was drawn this frame.
DEVEX_API void renderProjectManager(ToolsState& state, render::RenderWorld& world);
DEVEX_API void renderFileSystem(ToolsState& state, render::RenderWorld& world);
DEVEX_API void renderOutput(ToolsState& state, render::RenderWorld& world);
DEVEX_API void renderSceneTree(ToolsState& state, render::RenderWorld& world);
DEVEX_API void renderInspector(ToolsState& state, render::RenderWorld& world);
// The inspector of entities, made with the interface of the engine: made once, then updated in the
// room left in the Inspector window.
[[nodiscard]] DEVEX_API std::shared_ptr<InspectorUi> makeInspectorUi();
DEVEX_API void updateInspectorUi(ToolsState& state, scene::Scene& scene, core::Duration delta);
DEVEX_API void drawEditorMenus(ToolsState& state, scene::Scene& scene);
// Shows a screen in the middle of the window: the panel it needs opens and takes the focus.
DEVEX_API void setMainScreen(ToolsState& state, MainScreen screen);
// The window of a screen, as the dock builder and the focus use it.
[[nodiscard]] DEVEX_API const char* windowOf(MainScreen screen) noexcept;
DEVEX_API void drawStatusBar(ToolsState& state, const scene::Scene& scene);
// The windows of settings and the Export window, made with the interface of the engine, each floating
// over the others while it is open.
DEVEX_API void drawSettingsWindow(ToolsState& state);
DEVEX_API void drawProjectSettingsWindow(ToolsState& state);
DEVEX_API void drawExportWindow(ToolsState& state);
DEVEX_API void renderExportWindow(ToolsState& state, render::RenderWorld& world);
// The dialogs of the editor: New Script, the unsaved changes an action would drop, and About.
DEVEX_API void drawEditorPopups(ToolsState& state, scene::Scene& scene);
DEVEX_API void renderEditorDialogs(ToolsState& state, render::RenderWorld& world);
// Adds the images of the windows of settings, of the Export and Debugging windows and of the dialogs
// to the frame, those drawn this frame.
DEVEX_API void renderFormWindows(ToolsState& state, render::RenderWorld& world);
DEVEX_API void handleEditorShortcuts(ToolsState& state, scene::Scene& scene);
// Opens the project's scenes when the project changed, handles dialog answers and pick results.
DEVEX_API void updateEditorSession(ToolsState& state, scene::Scene& scene);
DEVEX_API void updateWindowTitle(ToolsState& state, const scene::Scene& scene);
DEVEX_API void addEditorOverlay(ToolsState& state, scene::Scene& scene, render::RenderWorld& world);
// Loads the theme and the project list from the file, or from the user's data directory when the
// file is empty.
DEVEX_API void loadUserSettings(ToolsState& state, const std::filesystem::path& file);
DEVEX_API void saveUserSettings(const ToolsState& state);
// Remembers the open scenes of the project and the editor camera.
DEVEX_API void saveEditorSettings(ToolsState& state);

// Scene tabs.
// A small lit scene to start from: a sun, a sky, a camera, a ground and a cube.
[[nodiscard]] DEVEX_API scene::Scene makeDefaultScene();
// A 2D scene holding a 2D camera.
[[nodiscard]] DEVEX_API scene::Scene makeDefault2DScene();
[[nodiscard]] DEVEX_API ActiveDocument activeDocument(ToolsState& state, scene::Scene& scene) noexcept;
// The name shown for a tab: its file name, or "[unsaved]".
[[nodiscard]] DEVEX_API std::string tabName(const std::filesystem::path& path);
// A new scene of that kind, or of the kind of the screen shown.
DEVEX_API void newSceneTab(ToolsState& state, scene::Scene& scene, std::optional<scene::SceneKind> kind = std::nullopt);
// Opens a scene in a new tab, or shows its tab when it is already open.
DEVEX_API void openSceneTab(ToolsState& state, scene::Scene& scene, const std::filesystem::path& path);
DEVEX_API void activateSceneTab(ToolsState& state, scene::Scene& scene, std::size_t index);
// Asks about unsaved changes first; the actions of the project and of quitting also stop play.
DEVEX_API void requestAction(ToolsState& state, scene::Scene& scene, PendingAction action);
[[nodiscard]] DEVEX_API bool hasUnsavedChanges(ToolsState& state, scene::Scene& scene);
// The tabs whose unsaved changes the pending action would drop.
[[nodiscard]] DEVEX_API std::vector<std::size_t> tabsWithUnsavedChanges(ToolsState& state, scene::Scene& scene);
// Saves the scenes the pending action would drop. An untitled scene comes to the screen with its
// save dialog, and the action continues once it is saved. Returns whether every scene was saved.
[[nodiscard]] DEVEX_API bool saveForPendingAction(ToolsState& state, scene::Scene& scene);
// Runs the pending action if nothing unsaved stands in its way anymore, or asks again.
DEVEX_API void continuePendingAction(ToolsState& state, scene::Scene& scene);
DEVEX_API void cancelPendingAction(ToolsState& state);
[[nodiscard]] DEVEX_API bool saveScene(ToolsState& state, scene::Scene& scene);
DEVEX_API void saveAllScenes(ToolsState& state, scene::Scene& scene);
DEVEX_API void showSaveSceneDialog(ToolsState& state);
DEVEX_API void showOpenSceneDialog(ToolsState& state);
DEVEX_API void frameSelection(ToolsState& state, const scene::Scene& scene);
[[nodiscard]] DEVEX_API core::Uuid uuidFromBytes(const std::array<std::uint8_t, 16>& bytes) noexcept;

// The window that creates an entity under parent (nil for a root, at the editor camera's pivot), as
// Godot's Create New Node does, and that adds components to entities.
DEVEX_API void openCreateEntity(ToolsState& state, core::Uuid parent);
DEVEX_API void openAddComponent(ToolsState& state, std::vector<core::Uuid> targets);
DEVEX_API void drawCreationDialog(ToolsState& state, scene::Scene& scene);
DEVEX_API void renderCreationDialog(ToolsState& state, render::RenderWorld& world);
// Creates an entity showing a sprite, at a position of the world, as one undoable step.
DEVEX_API void requestCreateSprite(ToolsState& state, asset::AssetId sprite, math::Vec3 position);
// Adds an entity built in a scratch scene as one undoable step, under parent or at the root at the
// editor camera's pivot, and selects it.
DEVEX_API void requestCreatePreset(ToolsState& state, core::Uuid parent, const char* name,
                                   const std::function<void(scene::Scene&, scene::Entity)>& build);

// The meshes every project has, which a mesh field offers before those of the project.
struct DEVEX_API BuiltinAsset
{
    const char* name;
    asset::AssetId id;
};
[[nodiscard]] DEVEX_API std::span<const BuiltinAsset> builtinMeshAssets() noexcept;
// The name an asset field shows: "(none)", a built-in mesh, the name of the asset, or its UUID when
// the project does not know it.
[[nodiscard]] DEVEX_API std::string assetLabel(const ToolsState& state, asset::AssetId id);


// Accepts an asset dropped on the last item, of the given type when one is given.
[[nodiscard]] DEVEX_API std::optional<asset::AssetId> acceptDroppedAsset(ToolsState& state, std::optional<asset::AssetType> type = std::nullopt);

// Queues the creation of the model's entities under parent (nil for a root), at a position relative
// to it when one is given, and selects them.
DEVEX_API void requestInstantiateModel(ToolsState& state, asset::AssetId model, core::Uuid parent,
                                       std::optional<math::Vec3> position = std::nullopt);

// Queues the creation of an instance of the prefab under parent (nil for a root), at a position
// relative to it when one is given, and selects it. A scene cannot contain itself.
DEVEX_API void requestInstantiatePrefab(ToolsState& state, asset::AssetId prefab, core::Uuid parent,
                                        std::optional<math::Vec3> position = std::nullopt);

// Asks where to save the entity and its descendants as a prefab, which then replaces them with an
// instance of it.
DEVEX_API void showSaveAsPrefabDialog(ToolsState& state, const scene::Scene& scene, core::Uuid entity);

// The name of a scene asset: its file name without its extension.
[[nodiscard]] DEVEX_API std::string sceneAssetName(const ToolsState& state, asset::AssetId sceneAsset);

// Queues the creation of an entity under parent (nil for a root) and selects it.
DEVEX_API void requestCreateEntity(ToolsState& state, core::Uuid parent);

// The selection copied to the clipboard of the system, cut, pasted beside the active entity,
// duplicated beside itself or deleted, each as one undo step. Pasted and duplicated entities are
// selected, and renamed when a sibling has their name.
DEVEX_API void copySelection(ToolsState& state, const scene::Scene& scene);
DEVEX_API void cutSelection(ToolsState& state, scene::Scene& scene);
DEVEX_API void pasteEntities(ToolsState& state, scene::Scene& scene);
DEVEX_API void duplicateSelection(ToolsState& state, scene::Scene& scene);
DEVEX_API void deleteSelection(ToolsState& state, scene::Scene& scene);
DEVEX_API void selectAll(ToolsState& state, const scene::Scene& scene);
// Whether the selection holds something that deleting or cutting may remove: the entities of a
// prefab instance stay with it.
[[nodiscard]] DEVEX_API bool canDeleteSelection(const ToolsState& state, const scene::Scene& scene);
// A name for an entity under parent that none of its children has, nor any of the names taken:
// "Crate 3" becomes "Crate 4", "Crate" becomes "Crate 2".
[[nodiscard]] DEVEX_API std::string uniqueChildName(const scene::Scene& scene, scene::Entity parent, std::string_view name,
                                                    const std::vector<std::string>& taken = {});
// Changes the selection as a click or a rectangle does.
DEVEX_API void selectEntities(ToolsState& state, std::span<const core::Uuid> entities, SelectMode mode);
// Starts renaming the entity in the scene tree.
DEVEX_API void startRename(ToolsState& state, core::Uuid entity);

// Whether the entity or one of its ancestors is hidden in the viewport.
[[nodiscard]] DEVEX_API bool isHidden(const ToolsState& state, const scene::Scene& scene, scene::Entity entity);
// Hides the selected entities, or shows them again when all of them are hidden.
DEVEX_API void toggleSelectionHidden(ToolsState& state, const scene::Scene& scene);
// The shortcuts that act on the selected entities, while the scene tree or the viewport has the
// keyboard: cut, copy, paste, duplicate, rename, hide, select all and delete.
DEVEX_API void handleEntityShortcuts(ToolsState& state, scene::Scene& scene);

// "vertical_fov" becomes "Vertical fov".
[[nodiscard]] DEVEX_API std::string displayName(std::string_view identifier);

} // namespace devex::tools::detail
