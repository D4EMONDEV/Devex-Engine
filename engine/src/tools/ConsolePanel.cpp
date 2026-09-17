#include "ToolsState.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace devex::tools::detail {
namespace {

enum class Severity : std::uint8_t
{
    Debug,
    Info,
    Warning,
    Error,
};

[[nodiscard]] Severity severityOf(core::LogLevel level) noexcept
{
    switch (level)
    {
    case core::LogLevel::Trace:
    case core::LogLevel::Debug:
        return Severity::Debug;
    case core::LogLevel::Info:
        return Severity::Info;
    case core::LogLevel::Warning:
        return Severity::Warning;
    case core::LogLevel::Error:
    case core::LogLevel::Fatal:
    case core::LogLevel::Off:
        return Severity::Error;
    }
    return Severity::Info;
}

[[nodiscard]] bool& shownFlag(ToolsState& state, Severity severity) noexcept
{
    switch (severity)
    {
    case Severity::Debug:
        return state.consoleShowDebug;
    case Severity::Info:
        return state.consoleShowInfo;
    case Severity::Warning:
        return state.consoleShowWarnings;
    case Severity::Error:
        return state.consoleShowErrors;
    }
    return state.consoleShowInfo;
}

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return part.empty() || !std::ranges::search(text, part, [](char left, char right) {
                                return std::tolower(static_cast<unsigned char>(left)) ==
                                       std::tolower(static_cast<unsigned char>(right));
                            }).empty();
}

} // namespace

void drawConsolePanel(ToolsState& state)
{
    // A new layout shows the output rather than the statistics docked with it.
    if (state.selectOutputTabFrames > 0 && --state.selectOutputTabFrames == 0)
    {
        ImGui::SetNextWindowFocus();
    }
    if (ImGui::Begin(consoleWindow))
    {
        const ThemeColors& colors = themeColors();
        const ImGuiStyle& style = ImGui::GetStyle();
        std::array<std::size_t, 4> counts{};
        state.log.forEach([&counts](const LogEntry& entry) { ++counts[static_cast<std::size_t>(severityOf(entry.level))]; });

        // Filter on the left; severity toggles with their counts, clear and auto-scroll on the right.
        struct Toggle
        {
            Severity severity;
            IconText icon;
            ImVec4 color;
            const char* name;
        };
        const std::array<Toggle, 4> toggles{{
            {Severity::Error, icons::CircleX, colors.error, "errors"},
            {Severity::Warning, icons::TriangleAlert, colors.warning, "warnings"},
            {Severity::Info, icons::Info, colors.text, "messages"},
            {Severity::Debug, icons::Bug, colors.textDim, "debug messages"},
        }};
        std::array<std::string, 4> labels;
        float togglesWidth = toolButtonWidth() * 2.0f + style.ItemSpacing.x * 3.0f;
        for (std::size_t index = 0; index < toggles.size(); ++index)
        {
            labels[index] = std::format("{}  {}", std::string_view(toggles[index].icon),
                                        counts[static_cast<std::size_t>(toggles[index].severity)]);
            togglesWidth += ImGui::CalcTextSize(labels[index].c_str()).x + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x;
        }
        searchField("##filter", state.consoleFilter, "Filter Messages",
                    std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - togglesWidth));
        for (std::size_t index = 0; index < toggles.size(); ++index)
        {
            const Toggle& toggle = toggles[index];
            bool& shown = shownFlag(state, toggle.severity);
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::PushStyleColor(ImGuiCol_Button, shown ? ImGui::GetStyleColorVec4(ImGuiCol_Button) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, uiColor(shown ? toggle.color : colors.textDim));
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Button(labels[index].c_str()))
            {
                shown = !shown;
            }
            ImGui::PopID();
            ImGui::PopStyleColor(2);
            ImGui::SetItemTooltip("%s %s", shown ? "Hide" : "Show", toggle.name);
        }
        ImGui::SameLine();
        if (toolButton("clear", icons::BrushCleaning, "Clear the output"))
        {
            state.log.clear();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (toolButton("scroll", icons::ArrowDownToLine, "Follow new messages", state.consoleAutoScroll))
        {
            state.consoleAutoScroll = !state.consoleAutoScroll;
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColor(colors.field));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
        if (ImGui::BeginChild("log lines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::PushFont(editorFonts().mono, monoFontPixels(state.theme.codeFontSize));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 2.0f));
            state.log.forEach([&](const LogEntry& entry) {
                const Severity severity = severityOf(entry.level);
                if (!shownFlag(state, severity) || !containsIgnoringCase(entry.message, state.consoleFilter))
                {
                    return;
                }
                const Toggle& toggle = toggles[3 - static_cast<std::size_t>(severity)];
                const ImVec4 color = severity == Severity::Info ? colors.text : toggle.color;
                if (severity == Severity::Warning || severity == Severity::Error)
                {
                    iconLabel(toggle.icon, toggle.color);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, uiColor(color));
                ImGui::TextUnformatted(entry.message.data(), entry.message.data() + entry.message.size());
                ImGui::PopStyleColor();
            });
            ImGui::PopStyleVar();
            ImGui::PopFont();
            if (state.consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

} // namespace devex::tools::detail
