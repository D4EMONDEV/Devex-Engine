#include "ToolsState.hpp"

#include <devex/asset/import/CurveFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace devex::tools::detail {
namespace {

using asset::CurveData;
using asset::CurveKey;

struct CurvePreset
{
    const char* label;
    const char* tooltip;
    CurveData curve;
};

[[nodiscard]] const std::array<CurvePreset, 6>& curvePresets()
{
    static const std::array<CurvePreset, 6> presets{
        CurvePreset{"Linear", "At the same pace from start to end", asset::linearCurve()},
        CurvePreset{"In", "Slow at the start",
                    {.keys = {{.time = 0.0f, .value = 0.0f}, {.time = 1.0f, .value = 1.0f, .inTangent = 2.0f, .outTangent = 2.0f}}}},
        CurvePreset{"Out", "Slow at the end",
                    {.keys = {{.time = 0.0f, .value = 0.0f, .inTangent = 2.0f, .outTangent = 2.0f}, {.time = 1.0f, .value = 1.0f}}}},
        CurvePreset{"In Out", "Slow at both ends", {.keys = {{.time = 0.0f, .value = 0.0f}, {.time = 1.0f, .value = 1.0f}}}},
        CurvePreset{"Overshoot", "Goes beyond the end, then comes back",
                    {.keys = {{.time = 0.0f, .value = 0.0f, .inTangent = 3.0f, .outTangent = 3.0f},
                              {.time = 0.65f, .value = 1.12f},
                              {.time = 1.0f, .value = 1.0f}}}},
        CurvePreset{"Bounce", "Lands, bounces once, and lands again",
                    {.keys = {{.time = 0.0f, .value = 0.0f},
                              {.time = 0.55f, .value = 1.0f, .inTangent = 3.5f, .outTangent = -2.0f},
                              {.time = 0.78f, .value = 0.82f},
                              {.time = 1.0f, .value = 1.0f, .inTangent = 2.0f, .outTangent = 2.0f}}}},
    };
    return presets;
}

// A new curve eases in and out.
[[nodiscard]] CurveData defaultCurve()
{
    return curvePresets()[3].curve;
}

[[nodiscard]] std::filesystem::file_time_type writeTime(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    return error ? std::filesystem::file_time_type{} : time;
}

// Reads the curve of the selected asset from its file, when another asset is selected or the file
// changed outside the editor.
void loadCurve(ToolsState& state, const asset::SourceFile& source)
{
    CurveEditor& editor = state.curveEditor;
    const std::optional<std::filesystem::path> file = state.database->project().absolutePath(source.path);
    if (!file)
    {
        return;
    }
    const std::filesystem::file_time_type time = writeTime(*file);
    if (editor.asset == state.selectedAsset && editor.file == *file && editor.fileTime == time)
    {
        return;
    }
    const bool sameAsset = editor.asset == state.selectedAsset;
    editor.asset = state.selectedAsset;
    editor.file = *file;
    editor.fileTime = time;
    editor.error.clear();
    const core::Result<std::string> text = core::readTextFile(*file);
    core::Result<CurveData> curve = text ? asset::parseCurveFile(*text) : core::Result<CurveData>(std::unexpected(text.error()));
    if (!curve)
    {
        editor.error = curve.error().message;
        editor.curve = defaultCurve();
    }
    else
    {
        editor.curve = std::move(*curve);
    }
    if (!sameAsset || editor.selectedKey >= static_cast<int>(editor.curve.keys.size()))
    {
        editor.selectedKey = -1;
    }
    editor.draggedKey = -1;
}

void saveCurve(ToolsState& state)
{
    CurveEditor& editor = state.curveEditor;
    if (core::Result<void> valid = asset::validate(editor.curve); !valid)
    {
        DEVEX_LOG_WARNING("The curve is not saved: {}", valid.error());
        return;
    }
    if (core::Result<void> written = core::writeTextFile(editor.file, asset::writeCurveFile(editor.curve)); !written)
    {
        DEVEX_LOG_ERROR("Cannot save the curve: {}", written.error());
        return;
    }
    editor.fileTime = writeTime(editor.file);
    editor.error.clear();
    if (core::Result<void> queued = state.database->reimport(editor.asset); !queued)
    {
        DEVEX_LOG_WARNING("{}", queued.error());
    }
}

// The range of values the graph shows: 0 to 1 and every key, with a margin.
void fitValues(CurveEditor& editor)
{
    float low = 0.0f;
    float high = 1.0f;
    for (const CurveKey& key : editor.curve.keys)
    {
        low = std::min(low, key.value);
        high = std::max(high, key.value);
    }
    // The curve between keys, which overshoots them with steep tangents.
    for (int sample = 0; sample <= 64; ++sample)
    {
        const float value = editor.curve.evaluate(static_cast<float>(sample) / 64.0f);
        low = std::min(low, value);
        high = std::max(high, value);
    }
    const float margin = (high - low) * 0.12f;
    editor.low = low - margin;
    editor.high = high + margin;
}

struct Graph
{
    ImVec2 min;
    ImVec2 max;
    float low = 0.0f;
    float high = 1.0f;

    [[nodiscard]] ImVec2 toScreen(float time, float value) const noexcept
    {
        return {min.x + time * (max.x - min.x), max.y - (value - low) / (high - low) * (max.y - min.y)};
    }
    [[nodiscard]] ImVec2 toCurve(ImVec2 point) const noexcept
    {
        return {(point.x - min.x) / (max.x - min.x), low + (max.y - point.y) / (max.y - min.y) * (high - low)};
    }
    // Where the handle of a tangent sits: a fixed length on screen along its slope.
    [[nodiscard]] ImVec2 handle(const CurveKey& key, bool out, float length) const noexcept
    {
        const ImVec2 at = toScreen(key.time, key.value);
        const float slope = out ? key.outTangent : key.inTangent;
        // The slope in pixels: value per time, scaled by the size of the graph.
        const float dx = max.x - min.x;
        const float dy = -(slope * (max.y - min.y) / (high - low));
        const float norm = std::sqrt(dx * dx + dy * dy);
        const float side = out ? 1.0f : -1.0f;
        return {at.x + side * dx / norm * length, at.y + side * dy / norm * length};
    }
    // The slope a handle dragged to a point gives, in value per time.
    [[nodiscard]] float slope(const CurveKey& key, ImVec2 point, bool out) const noexcept
    {
        const ImVec2 at = toCurve(point);
        float dt = at.x - key.time;
        const float dv = at.y - key.value;
        if (!out)
        {
            dt = -dt;
        }
        dt = std::max(dt, 1e-3f);
        return (out ? dv : -dv) / dt;
    }
};

// Adds a key where the pointer is, between the two it falls between.
void addKey(CurveEditor& editor, float time, float value)
{
    time = std::clamp(time, 0.0f, 1.0f);
    const auto next = std::ranges::upper_bound(editor.curve.keys, time, {}, &CurveKey::time);
    if (next == editor.curve.keys.begin() || next == editor.curve.keys.end() ||
        std::abs(std::prev(next)->time - time) < 1e-3f || std::abs(next->time - time) < 1e-3f)
    {
        return;
    }
    const auto index = static_cast<std::size_t>(next - editor.curve.keys.begin());
    editor.curve.keys.insert(next, CurveKey{.time = time, .value = value});
    asset::smoothTangents(editor.curve, index);
    editor.selectedKey = static_cast<int>(index);
}

[[nodiscard]] bool isEndKey(const CurveEditor& editor, int key)
{
    return key == 0 || key + 1 == static_cast<int>(editor.curve.keys.size());
}

// Draws the curve and moves its keys and tangents. True once a change is over, to be saved.
[[nodiscard]] bool drawGraph(CurveEditor& editor)
{
    const ThemeColors& colors = themeColors();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 50.0f);
    const float height = std::clamp(width * 0.62f, ImGui::GetFontSize() * 8.0f, ImGui::GetFontSize() * 18.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float padding = ImGui::GetFontSize() * 0.75f;
    ImGui::InvisibleButton("##graph", ImVec2(width, height),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (editor.draggedKey < 0)
    {
        fitValues(editor);
    }
    const Graph graph{.min = ImVec2(origin.x + padding, origin.y + padding),
                      .max = ImVec2(origin.x + width - padding, origin.y + height - padding),
                      .low = editor.low,
                      .high = editor.high};
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), uiColorU32(colors.field),
                        ImGui::GetStyle().FrameRounding);
    // Quarters of time, and the start and end values of the tween.
    const ImU32 grid = uiColorU32(colors.border);
    for (int quarter = 0; quarter <= 4; ++quarter)
    {
        const float x = graph.toScreen(static_cast<float>(quarter) * 0.25f, 0.0f).x;
        draw->AddLine(ImVec2(x, graph.min.y), ImVec2(x, graph.max.y), grid);
    }
    for (const float value : {0.0f, 1.0f})
    {
        const float y = graph.toScreen(0.0f, value).y;
        draw->AddLine(ImVec2(graph.min.x, y), ImVec2(graph.max.x, y), uiColorU32(colors.textDim));
        const std::string label = std::format("{:g}", value);
        draw->AddText(ImVec2(graph.min.x + 3.0f, y - ImGui::GetFontSize() - 1.0f), uiColorU32(colors.textDim), label.c_str());
    }

    // The curve, one segment every few pixels.
    const ImU32 curveColor = uiColorU32(colors.animation);
    const int samples = std::max(16, static_cast<int>((graph.max.x - graph.min.x) / 3.0f));
    ImVec2 previous = graph.toScreen(0.0f, editor.curve.evaluate(0.0f));
    for (int sample = 1; sample <= samples; ++sample)
    {
        const float time = static_cast<float>(sample) / static_cast<float>(samples);
        const ImVec2 point = graph.toScreen(time, editor.curve.evaluate(time));
        draw->AddLine(previous, point, curveColor, 2.0f);
        previous = point;
    }

    const float keyRadius = ImGui::GetFontSize() * 0.3f;
    const float handleLength = ImGui::GetFontSize() * 2.2f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto near = [&](ImVec2 point) {
        const ImVec2 delta(mouse.x - point.x, mouse.y - point.y);
        return delta.x * delta.x + delta.y * delta.y <= (keyRadius * 2.0f) * (keyRadius * 2.0f);
    };
    // What the pointer is over: a key (0) or a handle of the selected key (-1 in, 1 out).
    int hoveredKey = -1;
    int hoveredPart = 0;
    if (hovered && editor.selectedKey >= 0 && editor.selectedKey < static_cast<int>(editor.curve.keys.size()))
    {
        const CurveKey& key = editor.curve.keys[static_cast<std::size_t>(editor.selectedKey)];
        if (editor.selectedKey > 0 && near(graph.handle(key, false, handleLength)))
        {
            hoveredKey = editor.selectedKey;
            hoveredPart = -1;
        }
        else if (editor.selectedKey + 1 < static_cast<int>(editor.curve.keys.size()) && near(graph.handle(key, true, handleLength)))
        {
            hoveredKey = editor.selectedKey;
            hoveredPart = 1;
        }
    }
    for (int index = 0; hovered && hoveredKey < 0 && index < static_cast<int>(editor.curve.keys.size()); ++index)
    {
        const CurveKey& key = editor.curve.keys[static_cast<std::size_t>(index)];
        if (near(graph.toScreen(key.time, key.value)))
        {
            hoveredKey = index;
        }
    }

    bool changed = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        editor.draggedKey = hoveredKey;
        editor.draggedPart = hoveredPart;
        if (hoveredKey >= 0)
        {
            editor.selectedKey = hoveredKey;
        }
        else if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            editor.selectedKey = -1;
        }
    }
    // A double click adds a key.
    if (hovered && hoveredKey < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 at = graph.toCurve(mouse);
        addKey(editor, at.x, at.y);
        changed = true;
    }
    if (active && editor.draggedKey >= 0 && editor.draggedKey < static_cast<int>(editor.curve.keys.size()) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        editor.moved = true;
        const auto index = static_cast<std::size_t>(editor.draggedKey);
        CurveKey& key = editor.curve.keys[index];
        if (editor.draggedPart == 0)
        {
            const ImVec2 at = graph.toCurve(mouse);
            // The first and last keys stay at the start and end of the tween.
            if (!isEndKey(editor, editor.draggedKey))
            {
                const float before = editor.curve.keys[index - 1].time + 1e-3f;
                const float after = editor.curve.keys[index + 1].time - 1e-3f;
                key.time = std::clamp(at.x, before, after);
            }
            key.value = at.y;
            if (ImGui::GetIO().KeyCtrl)
            {
                // Snaps to tenths.
                key.value = std::round(key.value * 10.0f) / 10.0f;
            }
        }
        else
        {
            const float slope = graph.slope(key, mouse, editor.draggedPart > 0);
            // Both sides turn together, for a smooth curve; Shift turns one side only.
            if (editor.draggedPart > 0 || !ImGui::GetIO().KeyShift)
            {
                key.outTangent = slope;
            }
            if (editor.draggedPart < 0 || !ImGui::GetIO().KeyShift)
            {
                key.inTangent = slope;
            }
        }
    }
    if (editor.draggedKey >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        editor.draggedKey = -1;
        changed = std::exchange(editor.moved, false);
    }
    // Right-click on a key: what can be done to it.
    if (hovered && hoveredKey >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        editor.selectedKey = hoveredKey;
        ImGui::OpenPopup("key menu");
    }
    if (ImGui::BeginPopup("key menu"))
    {
        const int selected = editor.selectedKey;
        if (selected >= 0 && selected < static_cast<int>(editor.curve.keys.size()))
        {
            CurveKey& key = editor.curve.keys[static_cast<std::size_t>(selected)];
            if (ImGui::MenuItemEx("Smooth", icons::Activity.c_str()))
            {
                asset::smoothTangents(editor.curve, static_cast<std::size_t>(selected));
                changed = true;
            }
            ImGui::SetItemTooltip("The slope from the key before to the key after");
            if (ImGui::MenuItemEx("Flat", icons::Minus.c_str()))
            {
                key.inTangent = 0.0f;
                key.outTangent = 0.0f;
                changed = true;
            }
            if (ImGui::MenuItemEx("Linear", icons::Move.c_str()))
            {
                // Straight towards each neighbor.
                if (selected > 0)
                {
                    const CurveKey& before = editor.curve.keys[static_cast<std::size_t>(selected) - 1];
                    key.inTangent = (key.value - before.value) / (key.time - before.time);
                }
                if (selected + 1 < static_cast<int>(editor.curve.keys.size()))
                {
                    const CurveKey& after = editor.curve.keys[static_cast<std::size_t>(selected) + 1];
                    key.outTangent = (after.value - key.value) / (after.time - key.time);
                }
                changed = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItemEx("Delete Key", icons::Trash.c_str(), "Del", false, !isEndKey(editor, selected)))
            {
                editor.curve.keys.erase(editor.curve.keys.begin() + selected);
                editor.selectedKey = -1;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false) && editor.selectedKey >= 0 && !isEndKey(editor, editor.selectedKey) &&
        editor.selectedKey < static_cast<int>(editor.curve.keys.size()))
    {
        editor.curve.keys.erase(editor.curve.keys.begin() + editor.selectedKey);
        editor.selectedKey = -1;
        changed = true;
    }

    // The keys over the curve, the selected one with the handles of its slopes.
    for (int index = 0; index < static_cast<int>(editor.curve.keys.size()); ++index)
    {
        const CurveKey& key = editor.curve.keys[static_cast<std::size_t>(index)];
        const ImVec2 at = graph.toScreen(key.time, key.value);
        const bool selected = index == editor.selectedKey;
        if (selected)
        {
            const ImU32 handleColor = uiColorU32(colors.text);
            for (const bool out : {false, true})
            {
                if ((!out && index == 0) || (out && index + 1 == static_cast<int>(editor.curve.keys.size())))
                {
                    continue;
                }
                const ImVec2 end = graph.handle(key, out, handleLength);
                draw->AddLine(at, end, handleColor);
                const bool lit = hoveredKey == index && hoveredPart == (out ? 1 : -1);
                draw->AddCircleFilled(end, keyRadius * (lit ? 0.95f : 0.75f), handleColor);
            }
        }
        const bool lit = hoveredKey == index && hoveredPart == 0;
        draw->AddCircleFilled(at, keyRadius * (lit || selected ? 1.25f : 1.0f),
                              uiColorU32(selected ? colors.accent : colors.text));
    }
    return changed;
}

} // namespace

void drawCurveInspector(ToolsState& state)
{
    const ThemeColors& colors = themeColors();
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(state.selectedAsset) : nullptr;
    const std::optional<asset::SourceFile> source =
        state.database != nullptr ? state.database->sourceOf(state.selectedAsset) : std::nullopt;
    if (info == nullptr || !source)
    {
        state.selectedAsset = {};
        return;
    }
    loadCurve(state, *source);
    CurveEditor& editor = state.curveEditor;

    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Activity, colors.animation);
    boldText(info->name.c_str());
    ImGui::TextDisabled("%s", source->path.c_str());
    ImGui::Spacing();
    if (!editor.error.empty())
    {
        ImGui::TextColored(uiColor(colors.error), "The file could not be read: %s", editor.error.c_str());
        ImGui::TextWrapped("Editing the curve writes a new one over it.");
    }

    bool changed = drawGraph(editor);
    // Below rather than over the graph, which a tooltip would hide while keys move.
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(colors.textDim));
    ImGui::TextWrapped("Drag keys and slope handles. Double-click adds a key, right-click one for more. "
                       "Shift turns one side of a slope, Ctrl snaps values to tenths.");
    ImGui::PopStyleColor();

    ImGui::SeparatorText("Presets");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float lineWidth = 0.0f;
    const float available = ImGui::GetContentRegionAvail().x;
    for (const CurvePreset& preset : curvePresets())
    {
        const float buttonWidth = ImGui::CalcTextSize(preset.label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (lineWidth > 0.0f && lineWidth + spacing + buttonWidth <= available)
        {
            ImGui::SameLine();
            lineWidth += spacing;
        }
        else
        {
            lineWidth = 0.0f;
        }
        lineWidth += buttonWidth;
        if (ImGui::Button(preset.label))
        {
            editor.curve = preset.curve;
            editor.selectedKey = -1;
            changed = true;
        }
        ImGui::SetItemTooltip("%s", preset.tooltip);
    }

    const int selected = editor.selectedKey;
    if (selected >= 0 && selected < static_cast<int>(editor.curve.keys.size()))
    {
        ImGui::SeparatorText(std::format("Key {}", selected + 1).c_str());
        CurveKey& key = editor.curve.keys[static_cast<std::size_t>(selected)];
        if (beginProperties("key"))
        {
            const auto field = [&](const char* name, const char* id, float& value, float speed, bool enabled, const char* tooltip) {
                propertyName(name);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::BeginDisabled(!enabled);
                ImGui::DragFloat(id, &value, speed, -FLT_MAX, FLT_MAX, "%.3f");
                ImGui::EndDisabled();
                ImGui::SetItemTooltip("%s", tooltip);
                changed = changed || ImGui::IsItemDeactivatedAfterEdit();
            };
            const bool middle = !isEndKey(editor, selected);
            field("Time", "##time", key.time, 0.005f, middle, "From 0 at the start of the tween to 1 at its end");
            if (middle)
            {
                const float before = editor.curve.keys[static_cast<std::size_t>(selected) - 1].time + 1e-3f;
                const float after = editor.curve.keys[static_cast<std::size_t>(selected) + 1].time - 1e-3f;
                key.time = std::clamp(key.time, before, after);
            }
            field("Value", "##value", key.value, 0.005f, true, "0 at the start value of the tween, 1 at its end value");
            field("Slope In", "##in", key.inTangent, 0.02f, selected > 0, "The slope the curve arrives with");
            field("Slope Out", "##out", key.outTangent, 0.02f, middle || selected == 0, "The slope the curve leaves with");
            endProperties();
        }
    }
    else
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%zu keys. Click one to edit it.", editor.curve.keys.size());
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Tweens and Tweeners that name this curve ease along it in place of their ease.");

    if (changed)
    {
        saveCurve(state);
    }
}

core::Result<std::filesystem::path> createCurveFile(ToolsState& state, std::string_view folder)
{
    if (state.database == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "no project is open");
    }
    // Only the assets folder is imported.
    const std::string assets = std::string(asset::resourceScheme) + "assets";
    if (!folder.starts_with(assets))
    {
        folder = assets;
    }
    // res://folder/Curve.dvxcurve, then Curve 2, Curve 3...
    const std::string base = std::string(folder) + (folder.ends_with('/') ? "" : "/");
    for (int number = 1; number < 1000; ++number)
    {
        const std::string name = number == 1 ? "Curve" : std::format("Curve {}", number);
        const std::string resource = base + name + std::string(asset::curveExtension);
        const std::optional<std::filesystem::path> file = state.database->project().absolutePath(resource);
        if (!file)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "{} is outside the project", resource);
        }
        std::error_code error;
        if (std::filesystem::exists(*file, error))
        {
            continue;
        }
        if (core::Result<void> written = core::writeTextFile(*file, asset::writeCurveFile(defaultCurve())); !written)
        {
            return std::unexpected(written.error());
        }
        state.database->refresh();
        state.assetToSelect = resource;
        return *file;
    }
    return core::makeError(core::ErrorCode::AlreadyExists, "too many curves are named Curve in {}", folder);
}

} // namespace devex::tools::detail
