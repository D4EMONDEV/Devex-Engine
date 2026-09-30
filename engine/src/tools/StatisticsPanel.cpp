// The Statistics panel, made with the interface of the engine as Godot's Monitors: the device at the
// top, then a card for each measure of the frames with its curve over the last frames, what it is now
// and the most it reached. The measures are recorded every frame, the panel shown or not.
#include "SettingsUi.hpp"

#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;

namespace {

// The image the panel is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t statisticsSurface = 12;
constexpr double bytesPerMegabyte = 1024.0 * 1024.0;

// A measure the panel draws: where its values are, and how a value reads.
struct Measure
{
    const char* name;
    MonitorHistory Monitors::* history;
    std::string (*format)(float value);
    // Lines across the curve at these values, when they are in view.
    std::array<float, 2> guides{-1.0f, -1.0f};
};

[[nodiscard]] std::string milliseconds(float value)
{
    return std::format("{:.2f} ms", value);
}

[[nodiscard]] std::string count(float value)
{
    return std::format("{:.0f}", value);
}

[[nodiscard]] std::string megabytes(float value)
{
    return std::format("{:.1f} MB", value);
}

const std::array measures{
    Measure{"Frame Time", &Monitors::frameTime, &milliseconds, {1000.0f / 60.0f, 1000.0f / 30.0f}},
    Measure{"Draw Calls", &Monitors::drawCalls, &count},
    Measure{"Culled Instances", &Monitors::culled, &count},
    Measure{"GPU Memory", &Monitors::gpuMemory, &megabytes},
    Measure{"Uploads", &Monitors::uploads, &megabytes},
    Measure{"Entities", &Monitors::entities, &count},
};

} // namespace

void MonitorHistory::record(float value) noexcept
{
    m_values[m_next] = value;
    m_next = (m_next + 1) % m_values.size();
    m_count = std::min(m_count + 1, m_values.size());
}

void MonitorHistory::values(std::vector<float>& out) const
{
    out.clear();
    const std::size_t first = m_count < m_values.size() ? 0 : m_next;
    for (std::size_t index = 0; index < m_count; ++index)
    {
        out.push_back(m_values[(first + index) % m_values.size()]);
    }
}

float MonitorHistory::last() const noexcept
{
    return m_count == 0 ? 0.0f : m_values[(m_next + m_values.size() - 1) % m_values.size()];
}

float MonitorHistory::maximum() const noexcept
{
    float highest = 0.0f;
    for (std::size_t index = 0; index < m_count; ++index)
    {
        highest = std::max(highest, m_values[index]);
    }
    return highest;
}

std::size_t MonitorHistory::count() const noexcept
{
    return m_count;
}

void recordMonitors(ToolsState& state, const scene::Scene& scene, core::Duration frameDelta)
{
    const render::RendererStats stats = state.renderer.stats();
    Monitors& monitors = state.monitors;
    monitors.frameTime.record(static_cast<float>(frameDelta.count() * 1000.0));
    monitors.drawCalls.record(static_cast<float>(stats.drawCalls));
    monitors.culled.record(static_cast<float>(stats.culledInstances));
    monitors.gpuMemory.record(static_cast<float>(static_cast<double>(stats.gpuMemoryUsage) / bytesPerMegabyte));
    monitors.uploads.record(static_cast<float>(static_cast<double>(stats.uploadedBytes) / bytesPerMegabyte));
    monitors.entities.record(static_cast<float>(scene.entityCount()));
}

// The panel and the entities the code reads and changes.
struct StatisticsUi : FormUi
{
    StatisticsUi()
        : FormUi(statisticsSurface)
    {
    }

    struct Monitor
    {
        std::size_t card = 0;
        Entity plot;
        Entity reading;
    };

    bool built = false;
    float builtFont = 0.0f;
    Entity gpu;
    Entity driver;
    Entity presentation;
    Entity swapchain;
    std::vector<Monitor> monitors;
    Entity meshes;
    Entity textures;
    Entity materials;
    Entity undoSteps;
    std::vector<float> scratch;

    void build(EditorUiKit& kit);
    void update(ToolsState& state, EditorUiKit& kit, const scene::Scene& scene, core::Duration delta);
};

void StatisticsUi::build(EditorUiKit& kit)
{
    built = true;
    builtFont = font;
    buildForm(add({}, "Statistics", whole()), whole());
    panel.setKeyboardNavigation(false);
    const auto fact = [&](Section& section, const char* name) {
        const FormRow row = formRow(section, name);
        return text(row.editor, whole(math::Vec4{font * 0.3f, 0.0f, 0.0f, 0.0f}), "", "text");
    };

    Section& device = card(kit, "Device");
    gpu = fact(device, "GPU");
    driver = fact(device, "Driver");
    presentation = fact(device, "Presentation");
    swapchain = fact(device, "Swapchain");

    const ThemeColors& colors = themeColors();
    monitors.clear();
    for (const Measure& measure : measures)
    {
        Monitor made;
        made.card = sections.size();
        Section& section = card(kit, measure.name);
        const float height = std::round(font * 4.5f);
        const Entity box = add(section.card, "Curve", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {font * 0.35f, 0.0f},
                                                            .offsetMax = {0.0f, height}},
                               "list");
        scene().add<scene::UiImage>(box);
        section.lines.push_back(Line{.entity = box});
        made.plot = add(box, "Plot", whole(math::Vec4{font * 0.3f, 3.0f, font * 0.3f, 3.0f}));
        scene().add<scene::UiPlot>(made.plot, scene::UiPlot{.color = linearColor(colors.accent),
                                                            .guideColor = linearColor(ImVec4(colors.textDim.x, colors.textDim.y, colors.textDim.z, 0.45f)),
                                                            .lineWidth = 1.5f});
        // Where the pointer rests, the value of that frame.
        scene().get<scene::UiImage>(box).raycastTarget = true;
        tooltip(made.plot, "");
        scene().add<scene::UiImage>(made.plot, scene::UiImage{.color = {0.0f, 0.0f, 0.0f, 0.0f}});
        made.reading = note(&section, "", "dim");
        monitors.push_back(made);
    }

    Section& resources = card(kit, "Resources");
    meshes = fact(resources, "Meshes");
    textures = fact(resources, "Textures");
    materials = fact(resources, "Materials");
    undoSteps = fact(resources, "Undo Steps");
}

void StatisticsUi::update(ToolsState& state, EditorUiKit& kit, const scene::Scene& scene, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    setFont(state.theme.fontSize);
    if (!built || builtFont != font)
    {
        if (built)
        {
            clearForm();
            std::vector<Entity> children;
            for (Entity child = this->scene().firstChild(panel.canvas()); child.isValid(); child = this->scene().nextSibling(child))
            {
                children.push_back(child);
            }
            for (const Entity child : children)
            {
                this->scene().destroyEntity(child);
            }
        }
        build(kit);
    }
    styleTooltips(colors);
    labelWidth = std::clamp(std::round(panel.size().x * 0.3f), font * 6.0f, font * 11.0f);

    const render::RendererStats stats = state.renderer.stats();
    const render::GpuInfo& device = state.renderer.gpu();
    this->scene().get<scene::UiText>(gpu).text = std::format("{} ({})", device.name, render::toString(device.type));
    this->scene().get<scene::UiText>(driver).text = std::format("{} {}", device.driverName, device.driverVersion);
    this->scene().get<scene::UiText>(presentation).text = std::string(render::toString(state.renderer.presentMode()));
    this->scene().get<scene::UiText>(swapchain).text = std::format("{} x {}", stats.swapchainExtent.width, stats.swapchainExtent.height);

    for (std::size_t index = 0; index < monitors.size() && index < measures.size(); ++index)
    {
        const Measure& measure = measures[index];
        const MonitorHistory& history = state.monitors.*measure.history;
        scene::UiPlot& plot = this->scene().get<scene::UiPlot>(monitors[index].plot);
        history.values(plot.values);
        // As tall as the highest value shown, so that small changes still show.
        const float top = std::max(history.maximum() * 1.2f, 1.0f);
        plot.minValue = 0.0f;
        plot.maxValue = top;
        plot.guides.clear();
        for (const float guide : measure.guides)
        {
            if (guide > 0.0f && guide <= top)
            {
                plot.guides.push_back(guide);
            }
        }
        std::string reading = std::format("{} now, {} at most", measure.format(history.last()), measure.format(history.maximum()));
        if (measure.history == &Monitors::frameTime && history.last() > 0.0f)
        {
            reading = std::format("{} now ({:.0f} FPS), {} at most", measure.format(history.last()), 1000.0f / history.last(),
                                  measure.format(history.maximum()));
        }
        this->scene().get<scene::UiText>(monitors[index].reading).text = std::move(reading);
    }
    this->scene().get<scene::UiText>(meshes).text = std::format("{}", stats.meshCount);
    this->scene().get<scene::UiText>(textures).text = std::format("{}", stats.textureCount);
    this->scene().get<scene::UiText>(materials).text = std::format("{}", stats.materialCount);
    this->scene().get<scene::UiText>(undoSteps).text = std::format("{}", state.history.undoCount());
    layoutCards();
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();

    // The value of the frame under the pointer, in the tooltip of its curve.
    const ui::UiWorld& world = panel.world();
    for (std::size_t index = 0; index < monitors.size() && index < measures.size(); ++index)
    {
        const std::int32_t at = world.plotValueAt(this->scene(), monitors[index].plot);
        const scene::UiPlot& plot = this->scene().get<scene::UiPlot>(monitors[index].plot);
        if (at >= 0 && static_cast<std::size_t>(at) < plot.values.size())
        {
            const std::size_t ago = plot.values.size() - 1 - static_cast<std::size_t>(at);
            tooltip(monitors[index].plot, ago == 0 ? std::format("{}, this frame", measures[index].format(plot.values[static_cast<std::size_t>(at)]))
                                                   : std::format("{}, {} frames ago", measures[index].format(plot.values[static_cast<std::size_t>(at)]), ago));
        }
    }
    static_cast<void>(scene);
}

void drawStatisticsPanel(ToolsState& state, const scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Statistics");
    if (!beginDockedPanel(state, statisticsWindow))
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.statisticsUi)
    {
        state.statisticsUi = std::make_shared<StatisticsUi>();
    }
    state.statisticsUi->update(state, kit, scene, core::Duration(ImGui::GetIO().DeltaTime));
    ImGui::End();
}

void renderStatistics(ToolsState& state, render::RenderWorld& world)
{
    if (state.statisticsUi && state.uiKit)
    {
        state.statisticsUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
