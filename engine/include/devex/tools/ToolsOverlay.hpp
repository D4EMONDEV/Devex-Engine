#pragma once

#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/tools/CommandHistory.hpp>

#include <filesystem>
#include <memory>

namespace devex::tools {

namespace detail {
struct ToolsState;
} // namespace detail

// Docked Dear ImGui panels drawn over the game: hierarchy, inspector, assets, statistics and
// console.
// Only one overlay may exist at a time, since it owns the ImGui context.
class ToolsOverlay
{
public:
    // Creates the ImGui context and connects it to the platform and the renderer, which must
    // outlive the overlay. The panel layout is saved to settingsFile.
    [[nodiscard]] static core::Result<std::unique_ptr<ToolsOverlay>> create(
        platform::Platform& platform, platform::Window& window, render::Renderer& renderer,
        const std::filesystem::path& settingsFile);

    ~ToolsOverlay();

    ToolsOverlay(const ToolsOverlay&) = delete;
    ToolsOverlay& operator=(const ToolsOverlay&) = delete;

    [[nodiscard]] bool isVisible() const noexcept;
    void setVisible(bool visible) noexcept;

    // True when the panels use the keyboard or the mouse, which gameplay should then ignore.
    [[nodiscard]] bool capturesKeyboard() const noexcept;
    [[nodiscard]] bool capturesMouse() const noexcept;

    // Builds this frame's panels for the scene and queues them for the renderer's next endFrame.
    // While hidden, only the frame time statistics are recorded.
    void update(scene::Scene& scene, core::Duration frameDelta);

    // Lists the project assets in the panels; null when there is no project. The database must
    // outlive the overlay or be replaced first.
    void setAssetDatabase(asset::AssetDatabase* database) noexcept;

    [[nodiscard]] CommandHistory& history() noexcept;

private:
    explicit ToolsOverlay(std::unique_ptr<detail::ToolsState> state) noexcept;

    std::unique_ptr<detail::ToolsState> m_state;
};

} // namespace devex::tools
