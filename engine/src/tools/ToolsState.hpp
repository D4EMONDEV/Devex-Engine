#pragma once

#include "EditorCamera.hpp"
#include "EditorView.hpp"
#include "Gizmo.hpp"

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
#include <vector>

namespace devex::tools::detail {

inline constexpr const char* hierarchyWindow = "Hierarchy";
inline constexpr const char* inspectorWindow = "Inspector";
inline constexpr const char* statisticsWindow = "Statistics";
inline constexpr const char* consoleWindow = "Console";
inline constexpr const char* assetsWindow = "Assets";
inline constexpr const char* viewportWindow = "Viewport";

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

// An action that replaces the edited scene, waiting for the user to decide about unsaved changes.
struct SceneChange
{
    enum class Kind : std::uint8_t
    {
        NewScene,
        OpenScene,
        OpenProject,
        Quit,
    };

    Kind kind = Kind::NewScene;
    std::filesystem::path path;
};

// Answers of native file dialogs, which arrive between frames. Dialogs hold it weakly, so that an
// answer arriving after the tools are gone is dropped.
struct DialogAnswers
{
    std::optional<std::filesystem::path> openProject;
    std::optional<std::filesystem::path> newProjectLocation;
    std::optional<std::filesystem::path> openScene;
    std::optional<std::filesystem::path> saveSceneAs;
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

    bool visible = false;
    bool capturesKeyboard = false;
    bool capturesMouse = false;
    bool resetLayout = false;

    bool showHierarchy = true;
    bool showInspector = true;
    bool showStatistics = true;
    bool showConsole = true;
    bool showAssets = true;
    bool showViewport = true;

    // Null when the application runs without a project.
    asset::AssetDatabase* database = nullptr;
    std::string assetFilter;

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

    bool consoleShowDebug = true;
    bool consoleShowInfo = true;
    bool consoleShowWarnings = true;
    bool consoleShowErrors = true;
    bool consoleAutoScroll = true;

    // Editor.
    PlayState playState = PlayState::Editing;
    EditorRequests requests;
    std::shared_ptr<DialogAnswers> dialogAnswers = std::make_shared<DialogAnswers>();
    std::string windowTitle;
    // Where recent projects are remembered, empty when there is no user data directory.
    std::filesystem::path userSettingsFile;
    std::vector<std::filesystem::path> recentProjects;
    std::string newProjectName = "My game";
    std::string newProjectLocation;
    bool openNewProjectPopup = false;

    // The project changed since the last update: its last scene opens.
    bool projectChanged = false;
    // The .dvxscene file of the edited scene, empty until it is saved.
    std::filesystem::path scenePath;
    std::uint64_t savedState = 0;
    std::optional<SceneChange> pendingChange;
    bool openUnsavedChangesPopup = false;

    EditorCamera camera;
    Gizmo gizmo;
    GizmoHandle hoveredHandle = GizmoHandle::None;
    // The view of the last rendered frame, which mouse interactions refer to.
    ViewportView view;
    math::Extent2D viewportPixels;
    // Position of the viewport image on screen, in ImGui coordinates, and pixels per coordinate.
    math::Vec2 viewportOrigin{0.0f};
    float pixelsPerPoint = 1.0f;
    bool viewportHovered = false;
    bool viewportFocused = false;
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
void drawWelcomeScreen(ToolsState& state);
void drawEditorMenus(ToolsState& state, scene::Scene& scene);
void drawEditorPopups(ToolsState& state, scene::Scene& scene);
void handleEditorShortcuts(ToolsState& state, scene::Scene& scene);
// Opens the project's last scene when the project changed, handles dialog answers and pick results.
void updateEditorSession(ToolsState& state, scene::Scene& scene);
void updateWindowTitle(ToolsState& state, const scene::Scene& scene);
void addEditorOverlay(ToolsState& state, scene::Scene& scene, render::RenderWorld& world);
// From the file, or from the user's data directory when the file is empty.
void loadRecentProjects(ToolsState& state, const std::filesystem::path& file);
// Runs the change now, or asks first when the scene has unsaved changes.
void requestSceneChange(ToolsState& state, scene::Scene& scene, SceneChange change);
[[nodiscard]] bool hasUnsavedChanges(const ToolsState& state);
[[nodiscard]] bool saveScene(ToolsState& state, scene::Scene& scene);
void showSaveSceneDialog(ToolsState& state);
void showOpenSceneDialog(ToolsState& state);
void showOpenProjectDialog(ToolsState& state);
void frameSelection(ToolsState& state, const scene::Scene& scene);
// Remembers the project's last scene and the editor camera.
void saveEditorSettings(ToolsState& state);
[[nodiscard]] core::Uuid uuidFromBytes(const std::array<std::uint8_t, 16>& bytes) noexcept;

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

// Converts a color authored in sRGB, as ImGui colors are, to the linear space of the swapchain.
[[nodiscard]] ImVec4 linearColor(ImVec4 srgb) noexcept;

// "vertical_fov" becomes "Vertical fov".
[[nodiscard]] std::string displayName(std::string_view identifier);

} // namespace devex::tools::detail
