#include "ToolsState.hpp"

#include <format>

namespace devex::tools::detail {
namespace {

constexpr double bytesPerMegabyte = 1024.0 * 1024.0;

void row(const char* label, const std::string& value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

} // namespace

void drawStatisticsPanel(ToolsState& state, const scene::Scene& scene)
{
    if (ImGui::Begin(statisticsWindow, &state.showStatistics))
    {
        const float averageMilliseconds = state.frameTimes.average();
        const float framesPerSecond = averageMilliseconds > 0.0f ? 1000.0f / averageMilliseconds : 0.0f;
        const std::string overlay =
            std::format("{:.0f} FPS, {:.2f} ms", framesPerSecond, averageMilliseconds);
        ImGui::PlotLines("##frame times", state.frameTimes.values(), state.frameTimes.count(),
                         state.frameTimes.offset(), overlay.c_str(), 0.0f,
                         std::max(state.frameTimes.maximum() * 1.2f, 1.0f),
                         ImVec2(-1.0f, 64.0f));

        const render::RendererStats stats = state.renderer.stats();
        const render::GpuInfo& gpu = state.renderer.gpu();
        if (ImGui::BeginTable("statistics", 2, ImGuiTableFlags_SizingStretchProp))
        {
            row("GPU", std::format("{} ({})", gpu.name, render::toString(gpu.type)));
            row("Driver", std::format("{} {}", gpu.driverName, gpu.driverVersion));
            row("Presentation", std::string(render::toString(state.renderer.presentMode())));
            row("Swapchain",
                std::format("{} x {}", stats.swapchainExtent.width, stats.swapchainExtent.height));
            row("Draw calls", std::format("{}", stats.drawCalls));
            row("Meshes", std::format("{}", stats.meshCount));
            row("GPU memory", std::format("{:.1f} / {:.0f} MB",
                                          static_cast<double>(stats.gpuMemoryUsage) / bytesPerMegabyte,
                                          static_cast<double>(stats.gpuMemoryBudget) / bytesPerMegabyte));
            row("Entities", std::format("{}", scene.entityCount()));
            row("Undo steps", std::format("{}", state.history.undoCount()));
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace devex::tools::detail
