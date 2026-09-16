#include "ToolsState.hpp"

namespace devex::tools::detail {
namespace {

[[nodiscard]] bool isShown(const ToolsState& state, core::LogLevel level) noexcept
{
    switch (level)
    {
    case core::LogLevel::Trace:
    case core::LogLevel::Debug:
        return state.consoleShowDebug;
    case core::LogLevel::Info:
        return state.consoleShowInfo;
    case core::LogLevel::Warning:
        return state.consoleShowWarnings;
    case core::LogLevel::Error:
    case core::LogLevel::Fatal:
    case core::LogLevel::Off:
        return state.consoleShowErrors;
    }
    return true;
}

[[nodiscard]] ImVec4 colorOf(core::LogLevel level) noexcept
{
    switch (level)
    {
    case core::LogLevel::Trace:
    case core::LogLevel::Debug:
        return linearColor({0.55f, 0.58f, 0.62f, 1.0f});
    case core::LogLevel::Warning:
        return linearColor({0.95f, 0.78f, 0.35f, 1.0f});
    case core::LogLevel::Error:
    case core::LogLevel::Fatal:
        return linearColor({0.95f, 0.45f, 0.42f, 1.0f});
    case core::LogLevel::Info:
    case core::LogLevel::Off:
        break;
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

} // namespace

void drawConsolePanel(ToolsState& state)
{
    if (ImGui::Begin(consoleWindow, &state.showConsole))
    {
        if (ImGui::Button("Clear"))
        {
            state.log.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Debug", &state.consoleShowDebug);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &state.consoleShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &state.consoleShowWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &state.consoleShowErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &state.consoleAutoScroll);
        ImGui::Separator();

        if (ImGui::BeginChild("log lines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            state.log.forEach([&state](const LogEntry& entry) {
                if (!isShown(state, entry.level))
                {
                    return;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, colorOf(entry.level));
                ImGui::TextUnformatted(entry.message.data(),
                                       entry.message.data() + entry.message.size());
                ImGui::PopStyleColor();
            });
            if (state.consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace devex::tools::detail
