#pragma once

#include "EditorCamera.hpp"
#include "EditorView.hpp"
#include "Gizmo.hpp"
#include "Icons.hpp"
#include "ProjectList.hpp"
#include "SceneTabs.hpp"
#include "Theme.hpp"
#include "Widgets.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/tools/CommandHistory.hpp>
#include <devex/tools/LogBuffer.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace devex::tools::detail {

// Window names are also their identifiers in the saved layout.
inline constexpr const char* hierarchyWindow = "Scene";
inline constexpr const char* inspectorWindow = "Inspector";
inline constexpr const char* statisticsWindow = "Statistics";
inline constexpr const char* consoleWindow = "Output";
inline constexpr const char* assetsWindow = "FileSystem";
inline constexpr const char* viewportWindow = "Viewport";
inline constexpr const char* settingsWindow = "Editor Settings";

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

// What a click in the viewport does: select only, or also show a gizmo.
enum class EditorTool : std::uint8_t
{
    Select,
    Move,
    Rotate,
    Scale,
};

// An action that drops scenes, waiting for the user to decide about their unsaved changes.
struct PendingAction
{
    enum class Kind : std::uint8_t
    {
        // Closes one scene tab.
        CloseTab,
        OpenProject,
        // Goes back to the project manager.
        CloseProject,
        Quit,
    };

    Kind kind = Kind::CloseTab;
    // The tab to close.
    std::uint64_t tab = 0;
    // The project to open.
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
    std::optional<std::filesystem::path> saveSceneAs;
};

// What the project manager shows of a project, read from its file.
struct ProjectInfo
{
    std::string name;
    bool exists = false;
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

    // Null when the application runs without a project.
    asset::AssetDatabase* database = nullptr;
    std::string assetFilter;
    std::string hierarchyFilter;

    // The commands of the scene on screen: while playing, those of the played copy, with the
    // commands of the edited scene set aside until play stops.
    CommandHistory history;
    CommandHistory suspendedHistory;
    LogBuffer log;
    FrameTimes frameTimes;
    core::Uuid selection;

    // A structural change requested while walking the scene, applied once the panels are drawn.
    std::unique_ptr<Command> pendingCommand;

    // Value of the field being edited when the edit began, recorded as one undo step at the end.
    serialization::TextValue fieldEditStart;
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
    std::shared_ptr<DialogAnswers> dialogAnswers = std::make_shared<DialogAnswers>();
    std::string windowTitle;
    WindowLayout windowLayout = WindowLayout::Unset;
    // The editor's settings of the user (theme, projects), empty when there is no user data directory.
    std::filesystem::path userSettingsFile;
    ProjectList projects;
    ProjectManagerState projectManager;
    bool openAboutPopup = false;

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
    std::optional<math::Vec2> pickPixel;
    std::uint64_t nextPickId = 1;
    std::uint64_t awaitedPick = 0;
};

void drawHierarchyPanel(ToolsState& state, scene::Scene& scene);
void drawInspectorPanel(ToolsState& state, scene::Scene& scene);
void drawStatisticsPanel(ToolsState& state, const scene::Scene& scene);
void drawConsolePanel(ToolsState& state);
void drawAssetsPanel(ToolsState& state, scene::Scene& scene);

// Editor.
void drawViewportPanel(ToolsState& state, scene::Scene& scene);
void drawProjectManager(ToolsState& state);
void drawEditorMenus(ToolsState& state, scene::Scene& scene);
void drawStatusBar(ToolsState& state, const scene::Scene& scene);
void drawSettingsWindow(ToolsState& state);
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

// Makes the last item a drag source for the asset.
void dragAsset(asset::AssetId id, asset::AssetType type, const std::string& label);
// Accepts an asset dropped on the last item, of the given type when one is given.
[[nodiscard]] std::optional<asset::AssetId> acceptDroppedAsset(
    std::optional<asset::AssetType> type = std::nullopt);

// Queues the creation of the model's entities under parent (nil for a root), at a position relative
// to it when one is given, and selects them.
void requestInstantiateModel(ToolsState& state, asset::AssetId model, core::Uuid parent,
                             std::optional<math::Vec3> position = std::nullopt);

// Queues the creation of an entity under parent (nil for a root) and selects it.
void requestCreateEntity(ToolsState& state, core::Uuid parent);

// "vertical_fov" becomes "Vertical fov".
[[nodiscard]] std::string displayName(std::string_view identifier);

} // namespace devex::tools::detail
