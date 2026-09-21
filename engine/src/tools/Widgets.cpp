#include "Widgets.hpp"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>

namespace devex::tools::detail {
namespace {

EditorFonts g_fonts;

} // namespace

float toolButtonWidth() noexcept
{
    return ImGui::GetFrameHeight();
}

bool iconTreeNode(const char* id, ImGuiTreeNodeFlags flags)
{
    ImGuiContext& context = *ImGui::GetCurrentContext();
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float nodeX = ImGui::GetCursorScreenPos().x;
    float parentX = FLT_MAX;
    // Capture the parent before TreeNodeEx potentially pushes the leaf itself.
    if (window->DC.TreeDepth > 0 &&
        (window->DC.TreeHasStackDataDepthMask & (1u << (window->DC.TreeDepth - 1))) != 0)
    {
        parentX = context.TreeNodeStack.back().DrawLinesX1;
    }
    const bool open = ImGui::TreeNodeEx(id, flags);
    const ImGuiTreeNodeFlags lines = (flags & ImGuiTreeNodeFlags_DrawLinesMask_) != 0
                                        ? flags : style.TreeLinesFlags;
    if ((flags & ImGuiTreeNodeFlags_Leaf) != 0 && parentX != FLT_MAX &&
        (lines & (ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DrawLinesToNodes)) != 0 &&
        style.TreeLinesSize > 0.0f && ImGui::IsItemVisible())
    {
        // ImGui already draws the branch up to the arrow slot and tracks the last
        // child for its vertical guide. Only fill the remaining gap on leaf rows.
        const float x1 = ImTrunc(std::max(parentX, nodeX + style.FramePadding.x - style.ItemInnerSpacing.x));
        const float x2 = ImTrunc(nodeX + ImGui::GetTreeNodeToLabelSpacing() - style.ItemInnerSpacing.x);
        const float y = ImTrunc((ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        window->DrawList->AddLineH(x1, x2, y, ImGui::GetColorU32(ImGuiCol_TreeLines), style.TreeLinesSize);
    }
    return open;
}

bool toolButton(const char* id, IconText icon, const char* tooltip, bool selected, bool enabled,
                std::optional<ImVec4> iconColor)
{
    const ThemeColors& colors = themeColors();
    const float size = toolButtonWidth();
    ImGui::BeginDisabled(!enabled);
    ImGui::PushID(id);
    const ImVec4 hovered = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? uiColor(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.28f))
                                                    : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          selected ? uiColor(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.36f)) : hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, uiColor(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.45f)));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(iconColor.value_or(selected ? colors.accent : colors.text)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
    const bool pressed = ImGui::Button(icon.c_str(), ImVec2(size, size));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    ImGui::EndDisabled();
    if (tooltip != nullptr)
    {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    return pressed && enabled;
}

bool labelButton(IconText icon, const char* label, float width, bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    const std::string text = withIcon(icon, label);
    const bool pressed = ImGui::Button(text.c_str(), ImVec2(width, 0.0f));
    ImGui::EndDisabled();
    return pressed && enabled;
}

bool primaryButton(IconText icon, const char* label, float width, bool enabled)
{
    const ImVec4 accent = themeColors().accent;
    const auto shade = [&](float amount) {
        return uiColor(ImVec4(accent.x * amount, accent.y * amount, accent.z * amount, 1.0f));
    };
    ImGui::PushStyleColor(ImGuiCol_Button, shade(0.78f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, shade(0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, shade(0.68f));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
    const bool pressed = labelButton(icon, label, width, enabled);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags)
{
    // The chevron goes on the window's draw list: once open, the combo makes its popup current.
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const bool open = ImGui::BeginCombo(id, preview, flags | ImGuiComboFlags_NoArrowButton);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float chevronWidth = ImGui::CalcTextSize(icons::ChevronDown.c_str()).x;
    draw->AddText(ImVec2(position.x + width - chevronWidth - style.FramePadding.x,
                         position.y + (height - ImGui::GetFontSize()) * 0.5f),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), icons::ChevronDown.c_str());
    return open;
}

bool searchField(const char* id, std::string& text, const char* hint, float width)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconWidth = ImGui::CalcTextSize(icons::Search.c_str()).x;
    ImGui::SetNextItemWidth(width < 0.0f ? ImGui::GetContentRegionAvail().x : width);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x * 2.0f + iconWidth, style.FramePadding.y));
    const bool changed = ImGui::InputTextWithHint(id, hint, &text);
    ImGui::PopStyleVar();
    const ImVec2 min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y),
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled), icons::Search.c_str());
    return changed;
}

void iconLabel(IconText icon, ImVec4 color)
{
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(color));
    ImGui::TextUnformatted(icon.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}

void toolbarSeparator()
{
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(position.x, position.y + height * 0.2f),
                                        ImVec2(position.x, position.y + height * 0.8f),
                                        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::Dummy(ImVec2(1.0f, height));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
}

void boldText(const char* text)
{
    ImGui::PushFont(g_fonts.bold, 0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void alignRight(float width)
{
    const float target = ImGui::GetWindowContentRegionMax().x - width;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), target));
}

bool beginProperties(const char* id)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
    {
        return false;
    }
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    return true;
}

void propertyName(const char* name, bool highlighted)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    const float available = ImGui::GetContentRegionAvail().x;
    const ImVec2 position = ImGui::GetCursorScreenPos();
    if (highlighted)
    {
        const float padding = ImGui::GetStyle().CellPadding.x;
        const float barWidth = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.16f));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(position.x - padding, position.y),
                                                  ImVec2(position.x - padding + barWidth, position.y + ImGui::GetFrameHeight()),
                                                  uiColorU32(themeColors().accent));
        ImGui::PushFont(g_fonts.bold, 0.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(highlighted ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), position,
                              ImVec2(position.x + available, position.y + ImGui::GetFrameHeight()),
                              position.x + available, name, nullptr, nullptr);
    ImGui::Dummy(ImVec2(available, ImGui::GetTextLineHeight()));
    ImGui::PopStyleColor();
    if (highlighted)
    {
        ImGui::PopFont();
    }
    if (ImGui::CalcTextSize(name).x > available)
    {
        ImGui::SetItemTooltip("%s", name);
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void endProperties()
{
    ImGui::EndTable();
}

bool dragVector(const char* id, float* values, int count, float speed, const char* format)
{
    static constexpr std::array<const char*, 4> letters{"x", "y", "z", "w"};
    const ThemeColors& colors = themeColors();
    const std::array<ImVec4, 4> axisColors{colors.axisX, colors.axisY, colors.axisZ, colors.textDim};
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemInnerSpacing.x;
    const float total = ImGui::CalcItemWidth();
    const float width = std::max(1.0f, (total - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));
    const float letterWidth = ImGui::CalcTextSize("x").x + 3.0f;

    bool changed = false;
    ImGui::PushID(id);
    ImGui::BeginGroup();
    for (int index = 0; index < count; ++index)
    {
        ImGui::PushID(index);
        if (index > 0)
        {
            ImGui::SameLine(0.0f, spacing);
        }
        // The axis letter sits before its field, in the axis color.
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, uiColor(axisColors[static_cast<std::size_t>(index)]));
        ImGui::TextUnformatted(letters[static_cast<std::size_t>(index)]);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 3.0f);
        ImGui::SetNextItemWidth(std::max(1.0f, width - letterWidth));
        changed |= ImGui::DragFloat("##value", &values[index], speed, 0.0f, 0.0f, format);
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

void setEditorFonts(const EditorFonts& fonts) noexcept
{
    g_fonts = fonts;
}

const EditorFonts& editorFonts() noexcept
{
    return g_fonts;
}

} // namespace devex::tools::detail
