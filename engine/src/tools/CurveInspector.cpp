// The page of a curve in the inspector: its graph, whose keys and slopes the pointer drags, presets,
// and the numbers of the key chosen. The curve is saved once a change is over.
#include "InspectorUi.hpp"

#include <devex/asset/import/CurveFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

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
using scene::Entity;
using Button = PanelButton;

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

// The graph in the units of its area: time across from 0 to 1, values down from high to low.
struct Graph
{
    math::Vec2 size{1.0f};
    float low = 0.0f;
    float high = 1.0f;

    [[nodiscard]] math::Vec2 toArea(float time, float value) const noexcept
    {
        return {time * size.x, (high - value) / (high - low) * size.y};
    }
    [[nodiscard]] math::Vec2 toCurve(math::Vec2 point) const noexcept
    {
        return {point.x / size.x, high - point.y / size.y * (high - low)};
    }
    // Where the handle of a tangent sits: a fixed length on screen along its slope.
    [[nodiscard]] math::Vec2 handle(const CurveKey& key, bool out, float length) const noexcept
    {
        const math::Vec2 at = toArea(key.time, key.value);
        const float slope = out ? key.outTangent : key.inTangent;
        // The slope on screen: value per time, scaled by the size of the graph.
        const float dx = size.x;
        const float dy = -(slope * size.y / (high - low));
        const float norm = std::sqrt(dx * dx + dy * dy);
        const float side = out ? 1.0f : -1.0f;
        return {at.x + side * dx / norm * length, at.y + side * dy / norm * length};
    }
    // The slope a handle dragged to a point gives, in value per time.
    [[nodiscard]] float slope(const CurveKey& key, math::Vec2 point, bool out) const noexcept
    {
        const math::Vec2 at = toCurve(point);
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

class CurvePage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (!source)
        {
            return {};
        }
        loadCurve(state, *source);
        return std::format("{}|{}", state.curveEditor.curve.keys.size(), state.curveEditor.error.empty());
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const ThemeColors& colors = themeColors();
        CurveEditor& editor = state.curveEditor;
        m_keys.clear();
        m_box = {};
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        ui.heading(kit, icons::Activity, colors.animation, info->name, source->path);
        if (!editor.error.empty())
        {
            ui.note(nullptr, std::format("The file could not be read: {}", editor.error), "error", 2.0f);
            ui.note(nullptr, "Editing the curve writes a new one over it.");
        }

        // The graph: quarters of time, the start and end values of the tween, the curve, and its keys.
        m_box = ui.add(ui.content, "Graph", rects::wide(1.0f), "list");
        ui.scene().add<scene::UiImage>(m_box);
        ui.scene().add<scene::UiButton>(m_box);
        ui.scene().get<scene::UiRect>(m_box).clipChildren = true;
        m_area = ui.add(m_box, "Area", rects::whole(math::Vec4{std::round(ui.font * 0.75f)}));
        const scene::UiRect corner{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {1.0f, 1.0f}};
        const auto lineOf = [&](ImVec4 color) {
            const Entity made = ui.add(m_area, "Line", corner);
            ui.scene().add<scene::UiImage>(made, scene::UiImage{.color = linearColor(color), .raycastTarget = false});
            return made;
        };
        for (Entity& quarter : m_quarters)
        {
            quarter = lineOf(colors.border);
        }
        for (std::size_t index = 0; index < m_levels.size(); ++index)
        {
            m_levels[index] = lineOf(colors.textDim);
            m_levelLabels[index] = ui.text(m_area, corner, index == 0 ? "0" : "1", "dim", false, scene::TextAlign::Left,
                                           std::round(ui.font * 0.85f));
        }
        m_plot = ui.add(m_area, "Curve", rects::whole());
        ui.scene().add<scene::UiPlot>(m_plot, scene::UiPlot{.color = linearColor(colors.animation), .lineWidth = 2.0f});
        for (std::size_t side = 0; side < m_handleLines.size(); ++side)
        {
            m_handleLines[side] = lineOf(colors.text);
        }
        m_keyMenu = ui.pageMenu("Key menu", ui.font * 12.0f);
        m_smooth = ui.menuItem(kit, m_keyMenu, Icon::Activity, "Smooth");
        ui.tooltip(m_smooth.entity, "The slope from the key before to the key after");
        m_flat = ui.menuItem(kit, m_keyMenu, Icon::Minus, "Flat");
        m_linear = ui.menuItem(kit, m_keyMenu, Icon::Move, "Linear");
        ui.menuSeparator(m_keyMenu);
        m_delete = ui.menuItem(kit, m_keyMenu, Icon::Trash, "Delete Key", "Del");
        const auto circle = [&](ImVec4 color) {
            const Entity made = ui.add(m_area, "Key", corner);
            ui.scene().add<scene::UiImage>(made, scene::UiImage{.color = linearColor(color)});
            ui.scene().add<scene::UiButton>(made);
            return made;
        };
        for (std::size_t side = 0; side < m_handles.size(); ++side)
        {
            m_handles[side] = circle(colors.text);
        }
        for (std::size_t index = 0; index < editor.curve.keys.size(); ++index)
        {
            const Entity key = circle(colors.text);
            ui.scene().add<scene::UiContextMenu>(key, scene::UiContextMenu{.popup = ui.scene().reference(m_keyMenu)});
            m_keys.push_back(key);
        }
        // Below rather than over the graph, which a tooltip would hide while keys move.
        ui.note(nullptr, "Drag keys and slope handles. Double-click adds a key, right-click one for more. Shift turns one side of a "
                         "slope, Ctrl snaps values to tenths.",
                "dim", 2.0f);

        Section& presets = ui.card(kit, "Presets");
        const Entity grid = ui.add(presets.card, "Presets", rects::wide(ui.line * 2.0f + ui.gap * 3.0f));
        ui.scene().add<scene::UiLayout>(grid, scene::UiLayout{.kind = scene::UiLayoutKind::Grid,
                                                              .spacing = ui.gap * 3.0f,
                                                              .padding = {ui.font * 0.35f, 0.0f, 0.0f, 0.0f},
                                                              .columns = 3,
                                                              .equalSize = true});
        presets.lines.push_back(Line{.entity = grid});
        m_presets.clear();
        for (const CurvePreset& preset : curvePresets())
        {
            const Button made = ui.button(kit, grid, std::nullopt, preset.label, "button", 0.0f, ui.line - 4.0f);
            ui.tooltip(made.entity, preset.tooltip);
            m_presets.push_back(made);
        }

        ui.card(kit, "Key");
        m_card = ui.sections.size() - 1;
        const scene::UiNumberField values{.dragSpeed = 0.005f, .decimals = 3};
        const scene::UiNumberField slopes{.dragSpeed = 0.02f, .decimals = 3};
        const std::array<std::string_view, 1> one{""};
        const auto field = [&](const char* name, const scene::UiNumberField& settings, const char* tooltip) {
            const FormRow row = ui.formRow(ui.sections[m_card], name);
            ui.tooltip(row.editor, tooltip);
            m_fieldRows.push_back(row.row);
            return ui.numbers(row.editor, one, settings).front();
        };
        m_fieldRows.clear();
        m_time = field("Time", values, "From 0 at the start of the tween to 1 at its end");
        m_value = field("Value", values, "0 at the start value of the tween, 1 at its end value");
        m_in = field("Slope In", slopes, "The slope the curve arrives with");
        m_out = field("Slope Out", slopes, "The slope the curve leaves with");
        m_unselected = ui.note(&ui.sections[m_card], "");
        ui.note(nullptr, "Tweens and Tweeners that name this curve ease along it in place of their ease.", "dim", 2.0f);
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const ThemeColors& colors = themeColors();
        CurveEditor& editor = state.curveEditor;
        if (!m_box.isValid() || m_keys.size() != editor.curve.keys.size())
        {
            return;
        }
        // As wide as the panel, about two thirds as tall.
        const float width = std::max(ui.panel.size().x - ui.font * 0.6f - 8.0f, 50.0f);
        const float height = std::clamp(width * 0.62f, ui.font * 8.0f, ui.font * 18.0f);
        scene::UiRect& box = ui.scene().get<scene::UiRect>(m_box);
        box.offsetMax.y = box.offsetMin.y + height;
        const float inset = std::round(ui.font * 0.75f);
        if (editor.draggedKey < 0)
        {
            fitValues(editor);
        }
        const Graph graph{.size = {std::max(width - inset * 2.0f, 1.0f), std::max(height - inset * 2.0f, 1.0f)},
                          .low = editor.low,
                          .high = editor.high};
        const auto place = [&](Entity entity, math::Vec2 min, math::Vec2 max) {
            scene::UiRect& rect = ui.scene().get<scene::UiRect>(entity);
            rect.offsetMin = min;
            rect.offsetMax = max;
        };
        for (std::size_t quarter = 0; quarter < m_quarters.size(); ++quarter)
        {
            const float x = graph.toArea(static_cast<float>(quarter) * 0.25f, 0.0f).x;
            place(m_quarters[quarter], {x - 0.5f, 0.0f}, {x + 0.5f, graph.size.y});
        }
        for (std::size_t index = 0; index < m_levels.size(); ++index)
        {
            const float y = graph.toArea(0.0f, static_cast<float>(index)).y;
            place(m_levels[index], {0.0f, y - 0.5f}, {graph.size.x, y + 0.5f});
            place(m_levelLabels[index], {3.0f, y - ui.font * 1.3f}, {ui.font * 3.0f, y});
        }
        // The curve, a value every few units.
        const int samples = std::clamp(static_cast<int>(graph.size.x / 3.0f), 16, 256);
        scene::UiPlot& plot = ui.scene().get<scene::UiPlot>(m_plot);
        plot.values.resize(static_cast<std::size_t>(samples) + 1);
        for (int sample = 0; sample <= samples; ++sample)
        {
            plot.values[static_cast<std::size_t>(sample)] = editor.curve.evaluate(static_cast<float>(sample) / static_cast<float>(samples));
        }
        plot.minValue = graph.low;
        plot.maxValue = graph.high;

        // The keys over the curve, the selected one with the handles of its slopes.
        const ui::UiWorld& world = ui.panel.world();
        const float radius = std::round(ui.font * 0.3f);
        const float length = ui.font * 2.2f;
        const int selected = editor.selectedKey;
        for (std::size_t index = 0; index < m_keys.size(); ++index)
        {
            const CurveKey& key = editor.curve.keys[index];
            const math::Vec2 at = graph.toArea(key.time, key.value);
            const bool chosen = static_cast<int>(index) == selected;
            const bool lit = world.hovered() == m_keys[index] || (editor.draggedKey == static_cast<int>(index) && editor.draggedPart == 0);
            const float size = radius * (lit || chosen ? 1.25f : 1.0f);
            place(m_keys[index], at - math::Vec2{size}, at + math::Vec2{size});
            scene::UiImage& image = ui.scene().get<scene::UiImage>(m_keys[index]);
            image.cornerRadius = size;
            image.color = linearColor(chosen ? colors.accent : colors.text);
        }
        for (std::size_t side = 0; side < m_handles.size(); ++side)
        {
            const bool out = side == 1;
            const bool shown = selected >= 0 && selected < static_cast<int>(m_keys.size()) &&
                               (out ? selected + 1 < static_cast<int>(m_keys.size()) : selected > 0);
            ui.scene().get<scene::UiRect>(m_handles[side]).visible = shown;
            ui.scene().get<scene::UiRect>(m_handleLines[side]).visible = shown;
            if (!shown)
            {
                continue;
            }
            const CurveKey& key = editor.curve.keys[static_cast<std::size_t>(selected)];
            const math::Vec2 at = graph.toArea(key.time, key.value);
            const math::Vec2 end = graph.handle(key, out, length);
            const bool lit = world.hovered() == m_handles[side] || (editor.draggedKey == selected && editor.draggedPart == (out ? 1 : -1));
            const float size = radius * (lit ? 0.95f : 0.75f);
            place(m_handles[side], end - math::Vec2{size}, end + math::Vec2{size});
            ui.scene().get<scene::UiImage>(m_handles[side]).cornerRadius = size;
            // A thin line from the key to the handle, turned along it.
            const math::Vec2 middle = (at + end) * 0.5f;
            const math::Vec2 along = end - at;
            const float span = std::sqrt(along.x * along.x + along.y * along.y);
            scene::UiRect& lineRect = ui.scene().get<scene::UiRect>(m_handleLines[side]);
            lineRect.offsetMin = middle - math::Vec2{span * 0.5f, 0.75f};
            lineRect.offsetMax = middle + math::Vec2{span * 0.5f, 0.75f};
            lineRect.rotation = std::atan2(along.y, along.x);
        }

        // The key chosen, as numbers.
        const bool hasKey = selected >= 0 && selected < static_cast<int>(m_keys.size());
        for (const Entity row : m_fieldRows)
        {
            ui.showLine(ui.sections[m_card], row, hasKey);
        }
        ui.showLine(ui.sections[m_card], m_unselected, !hasKey);
        ui.scene().get<scene::UiText>(m_unselected).text = std::format("{} keys. Click one to edit it.", m_keys.size());
        ui.scene().get<scene::UiText>(ui.sections[m_card].title).text = hasKey ? std::format("Key {}", selected + 1) : std::string("Key");
        if (hasKey)
        {
            const CurveKey& key = editor.curve.keys[static_cast<std::size_t>(selected)];
            const bool middleKey = !isEndKey(editor, selected);
            const auto number = [&](Entity box, float value, bool open) {
                scene::UiNumberField& field = ui.scene().get<scene::UiNumberField>(box);
                field.interactable = open;
                ui.scene().get<scene::UiRect>(box).opacity = open ? 1.0f : 0.5f;
                if (world.editedField() != box && world.held() != box)
                {
                    field.value = value;
                }
            };
            number(m_time, key.time, middleKey);
            number(m_value, key.value, true);
            number(m_in, key.inTangent, selected > 0);
            number(m_out, key.outTangent, middleKey || selected == 0);
        }
        ui.enable(m_delete, hasKey && !isEndKey(editor, selected));
        ui.fitMenu(m_keyMenu, ui.font * 12.0f);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        CurveEditor& editor = state.curveEditor;
        if (!m_box.isValid() || m_keys.size() != editor.curve.keys.size())
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        const ui::UiInput& input = ui.panel.input();
        const ui::LaidOutRect* const area = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(m_area);
        if (area == nullptr)
        {
            return;
        }
        const Graph graph{.size = math::max(area->size(), math::Vec2{1.0f}), .low = editor.low, .high = editor.high};
        const math::Vec2 pointer = input.pointer - area->min;
        const auto keyUnder = [&](Entity entity) -> int {
            const auto found = std::ranges::find(m_keys, entity);
            return found != m_keys.end() ? static_cast<int>(found - m_keys.begin()) : -1;
        };

        // A key or a handle pressed is dragged; a press on the graph elsewhere chooses nothing.
        if (input.pointerPressed && ui.panel.hovered())
        {
            const Entity hovered = world.hovered();
            if (const int key = keyUnder(hovered); key >= 0)
            {
                editor.draggedKey = key;
                editor.draggedPart = 0;
                editor.selectedKey = key;
            }
            else if (hovered.isValid() && (hovered == m_handles[0] || hovered == m_handles[1]))
            {
                editor.draggedKey = editor.selectedKey;
                editor.draggedPart = hovered == m_handles[1] ? 1 : -1;
            }
            else if (hovered == m_box)
            {
                editor.selectedKey = -1;
            }
        }
        // A double click adds a key.
        if (world.wasDoubleClicked(m_box))
        {
            const math::Vec2 at = graph.toCurve(pointer);
            addKey(editor, at.x, at.y);
            m_dirty = true;
        }
        if (editor.draggedKey >= 0 && editor.draggedKey < static_cast<int>(editor.curve.keys.size()) && input.pointerDown &&
            input.pointerMoved)
        {
            editor.moved = true;
            const auto index = static_cast<std::size_t>(editor.draggedKey);
            CurveKey& key = editor.curve.keys[index];
            const ImGuiIO& io = ImGui::GetIO();
            if (editor.draggedPart == 0)
            {
                const math::Vec2 at = graph.toCurve(pointer);
                // The first and last keys stay at the start and end of the tween.
                if (!isEndKey(editor, editor.draggedKey))
                {
                    const float before = editor.curve.keys[index - 1].time + 1e-3f;
                    const float after = editor.curve.keys[index + 1].time - 1e-3f;
                    key.time = std::clamp(at.x, before, after);
                }
                key.value = at.y;
                if (io.KeyCtrl)
                {
                    // Snaps to tenths.
                    key.value = std::round(key.value * 10.0f) / 10.0f;
                }
            }
            else
            {
                const float slope = graph.slope(key, pointer, editor.draggedPart > 0);
                // Both sides turn together, for a smooth curve; Shift turns one side only.
                if (editor.draggedPart > 0 || !io.KeyShift)
                {
                    key.outTangent = slope;
                }
                if (editor.draggedPart < 0 || !io.KeyShift)
                {
                    key.inTangent = slope;
                }
            }
        }
        if (editor.draggedKey >= 0 && !input.pointerDown)
        {
            editor.draggedKey = -1;
            m_dirty |= std::exchange(editor.moved, false);
        }

        // Right-click on a key: what can be done to it.
        if (input.secondaryPressed && world.isPopupOpen(ui.scene(), m_keyMenu))
        {
            if (const int key = keyUnder(world.contextTarget()); key >= 0)
            {
                editor.selectedKey = key;
            }
        }
        const int selected = editor.selectedKey;
        const bool hasKey = selected >= 0 && selected < static_cast<int>(editor.curve.keys.size());
        if (hasKey)
        {
            CurveKey& key = editor.curve.keys[static_cast<std::size_t>(selected)];
            if (world.wasClicked(m_smooth.entity))
            {
                asset::smoothTangents(editor.curve, static_cast<std::size_t>(selected));
                m_dirty = true;
            }
            else if (world.wasClicked(m_flat.entity))
            {
                key.inTangent = 0.0f;
                key.outTangent = 0.0f;
                m_dirty = true;
            }
            else if (world.wasClicked(m_linear.entity))
            {
                // Straight towards each neighbour.
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
                m_dirty = true;
            }
            const bool deleteKey = world.wasClicked(m_delete.entity) ||
                                   (ui.panel.focused() && !world.isEditing() && ImGui::IsKeyPressed(ImGuiKey_Delete, false));
            if (deleteKey && !isEndKey(editor, selected))
            {
                editor.curve.keys.erase(editor.curve.keys.begin() + selected);
                editor.selectedKey = -1;
                m_dirty = true;
                saveCurve(state);
                m_dirty = false;
                return;
            }
            // The numbers of the key.
            const auto value = [&](Entity box) { return ui.scene().get<scene::UiNumberField>(box).value; };
            if (world.wasChanged(m_time) && !isEndKey(editor, selected))
            {
                const float before = editor.curve.keys[static_cast<std::size_t>(selected) - 1].time + 1e-3f;
                const float after = editor.curve.keys[static_cast<std::size_t>(selected) + 1].time - 1e-3f;
                key.time = std::clamp(value(m_time), before, after);
                m_dirty = true;
            }
            if (world.wasChanged(m_value))
            {
                key.value = value(m_value);
                m_dirty = true;
            }
            if (world.wasChanged(m_in))
            {
                key.inTangent = value(m_in);
                m_dirty = true;
            }
            if (world.wasChanged(m_out))
            {
                key.outTangent = value(m_out);
                m_dirty = true;
            }
        }
        for (std::size_t index = 0; index < m_presets.size(); ++index)
        {
            if (world.wasClicked(m_presets[index].entity))
            {
                editor.curve = curvePresets()[index].curve;
                editor.selectedKey = -1;
                m_dirty = true;
            }
        }
        // Saved once a change is over: nothing dragged, held or typed any more.
        const bool busy = editor.draggedKey >= 0 || world.held().isValid() || world.isEditing();
        if (m_dirty && !busy)
        {
            saveCurve(state);
            m_dirty = false;
        }
    }

private:
    Entity m_box;
    Entity m_area;
    std::array<Entity, 5> m_quarters{};
    std::array<Entity, 2> m_levels{};
    std::array<Entity, 2> m_levelLabels{};
    Entity m_plot;
    std::vector<Entity> m_keys;
    std::array<Entity, 2> m_handles{};
    std::array<Entity, 2> m_handleLines{};
    Entity m_keyMenu;
    Button m_smooth;
    Button m_flat;
    Button m_linear;
    Button m_delete;
    std::vector<Button> m_presets;
    std::size_t m_card = 0;
    std::vector<Entity> m_fieldRows;
    Entity m_time;
    Entity m_value;
    Entity m_in;
    Entity m_out;
    Entity m_unselected;
    bool m_dirty = false;
};

} // namespace

std::unique_ptr<InspectorPage> makeCurvePage()
{
    return std::make_unique<CurvePage>();
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
