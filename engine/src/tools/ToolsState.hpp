#pragma once

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

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace devex::tools::detail {

inline constexpr const char* hierarchyWindow = "Hierarchy";
inline constexpr const char* inspectorWindow = "Inspector";
inline constexpr const char* statisticsWindow = "Statistics";
inline constexpr const char* consoleWindow = "Console";
inline constexpr const char* assetsWindow = "Assets";

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

struct ToolsState
{
    ToolsState(platform::Platform& platformLayer, render::Renderer& gpu) noexcept
        : platform(platformLayer)
        , renderer(gpu)
    {
    }

    platform::Platform& platform;
    render::Renderer& renderer;
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

    // Null when the application runs without a project.
    asset::AssetDatabase* database = nullptr;
    std::string assetFilter;

    CommandHistory history;
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
};

void drawHierarchyPanel(ToolsState& state, scene::Scene& scene);
void drawInspectorPanel(ToolsState& state, scene::Scene& scene);
void drawStatisticsPanel(ToolsState& state, const scene::Scene& scene);
void drawConsolePanel(ToolsState& state);
void drawAssetsPanel(ToolsState& state, scene::Scene& scene);

[[nodiscard]] core::Uuid uuidFromBytes(const std::array<std::uint8_t, 16>& bytes) noexcept;

// Makes the last item a drag source for the asset.
void dragAsset(asset::AssetId id, asset::AssetType type, const std::string& label);
// Accepts an asset dropped on the last item, of the given type when one is given.
[[nodiscard]] std::optional<asset::AssetId> acceptDroppedAsset(
    std::optional<asset::AssetType> type = std::nullopt);

// Queues the creation of the model's entities under parent (nil for a root) and selects them.
void requestInstantiateModel(ToolsState& state, asset::AssetId model, core::Uuid parent);

// Queues the creation of an entity under parent (nil for a root) and selects it.
void requestCreateEntity(ToolsState& state, core::Uuid parent);

// Converts a color authored in sRGB, as ImGui colors are, to the linear space of the swapchain.
[[nodiscard]] ImVec4 linearColor(ImVec4 srgb) noexcept;

// "vertical_fov" becomes "Vertical fov".
[[nodiscard]] std::string displayName(std::string_view identifier);

} // namespace devex::tools::detail
