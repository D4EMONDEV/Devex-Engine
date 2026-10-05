// The Profiler panel, made with the interface of the engine: a bar per recorded frame, what it spent
// working in full colour and what it spent waiting faint above it; the zones of the frame looked at on
// a timeline, a lane per thread, zoomed with the wheel and moved by dragging; and its zones as a tree
// of times, the passes of the GPU, and the memory of the loaded assets, in tabs.
#include "SettingsUi.hpp"

#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

using FramePtr = std::shared_ptr<const core::ProfileFrame>;

// The image the panel is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t profilerSurface = 13;
constexpr double nanosecondsPerMillisecond = 1'000'000.0;
// A frame on time for a display at 60 Hz, then at 30 Hz, with some slack for the jitter of the
// vertical sync.
constexpr double onTimeMilliseconds = 1000.0 / 60.0 * 1.05;
constexpr double lateMilliseconds = 1000.0 / 30.0 * 1.05;
// While recording, the frame shown changes this often, so that its numbers can be read.
constexpr double followSeconds = 0.5;
// The memory of the assets is read this often while its tab is shown.
constexpr double memorySeconds = 0.5;
// The frames the bars show at most, as many as the profiler keeps.
constexpr std::size_t barCount = 300;
// Where the thread that runs the frames waits rather than works.
constexpr std::array<std::string_view, 4> waitingZones{"Frame limit", "Wait for the GPU", "Acquire the image", "Present"};

[[nodiscard]] double milliseconds(std::uint64_t nanoseconds) noexcept
{
    return static_cast<double>(nanoseconds) / nanosecondsPerMillisecond;
}

[[nodiscard]] ImVec4 withAlpha(ImVec4 color, float alpha) noexcept
{
    color.w = alpha;
    return color;
}

[[nodiscard]] bool isWaiting(const core::ProfileZone& zone) noexcept
{
    return zone.thread == 0 && std::ranges::contains(waitingZones, std::string_view(zone.name));
}

[[nodiscard]] std::uint64_t waitingTime(const core::ProfileFrame& frame) noexcept
{
    std::uint64_t waiting = 0;
    for (const core::ProfileZone& zone : frame.cpu)
    {
        if (isWaiting(zone))
        {
            waiting += zone.duration();
        }
    }
    return std::min(waiting, frame.duration());
}

[[nodiscard]] ImVec4 frameColor(double frameMilliseconds) noexcept
{
    const ThemeColors& colors = themeColors();
    if (frameMilliseconds <= onTimeMilliseconds)
    {
        return colors.success;
    }
    return frameMilliseconds <= lateMilliseconds ? colors.warning : colors.error;
}

// The same name always has the same colour, among those of the kinds of objects.
[[nodiscard]] ImVec4 zoneColor(const core::ProfileZone& zone)
{
    const ThemeColors& colors = themeColors();
    if (isWaiting(zone))
    {
        return colors.neutral;
    }
    const std::array palette{colors.gameCode, colors.physics,     colors.animation, colors.audio,    colors.interface,
                             colors.light,    colors.environment, colors.camera,    colors.material, colors.texture};
    return palette[std::hash<std::string_view>{}(zone.name) % palette.size()];
}

[[nodiscard]] std::string formatBytes(std::size_t bytes)
{
    constexpr double kilobyte = 1024.0;
    const auto value = static_cast<double>(bytes);
    if (bytes == 0)
    {
        return "-";
    }
    if (value < kilobyte)
    {
        return std::format("{} B", bytes);
    }
    if (value < kilobyte * kilobyte)
    {
        return std::format("{:.1f} KB", value / kilobyte);
    }
    if (value < kilobyte * kilobyte * kilobyte)
    {
        return std::format("{:.1f} MB", value / (kilobyte * kilobyte));
    }
    return std::format("{:.2f} GB", value / (kilobyte * kilobyte * kilobyte));
}

// A time of the ruler, with as many decimals as the step between two marks needs.
[[nodiscard]] std::string formatTime(double nanoseconds, double step)
{
    if (step >= 1'000'000.0)
    {
        return std::format("{:.0f} ms", nanoseconds / nanosecondsPerMillisecond);
    }
    if (step >= 100'000.0)
    {
        return std::format("{:.1f} ms", nanoseconds / nanosecondsPerMillisecond);
    }
    if (step >= 10'000.0)
    {
        return std::format("{:.2f} ms", nanoseconds / nanosecondsPerMillisecond);
    }
    return std::format("{:.0f} us", nanoseconds / 1000.0);
}

[[nodiscard]] std::string threadName(const std::vector<std::string>& threads, std::uint16_t thread)
{
    return thread < threads.size() ? threads[thread] : std::format("Thread {}", thread);
}

// While recording, the newest frame the GPU has finished, taken again twice a second; once paused,
// the frame chosen in the bars.
[[nodiscard]] FramePtr shownFrame(ProfilerView& view, std::span<const FramePtr> frames, double now)
{
    const auto found = std::ranges::find(frames, view.frame, [](const FramePtr& frame) { return frame->index; });
    if (!core::profiler::isPaused() && (found == frames.end() || now - view.frameTaken >= followSeconds))
    {
        if (frames.empty())
        {
            view.frame = 0;
            return nullptr;
        }
        // The GPU reports a frame a few frames late: one of the last few has both halves.
        const std::span<const FramePtr> recent = frames.last(std::min<std::size_t>(frames.size(), 8));
        const auto measured = std::ranges::find_if(recent.rbegin(), recent.rend(), [](const FramePtr& frame) { return frame->gpuMeasured; });
        const FramePtr chosen = measured != recent.rend() ? *measured : frames.back();
        view.frame = chosen->index;
        view.frameTaken = now;
        return chosen;
    }
    return found != frames.end() ? *found : nullptr;
}

struct Lane
{
    std::uint16_t thread = 0;
    int rows = 1;
    bool gpu = false;
};

} // namespace

// The panel and the entities the code reads and changes.
struct ProfilerUi : FormUi
{
    ProfilerUi()
        : FormUi(profilerSurface)
    {
    }

    // A zone on the timeline, taken from a reserve and placed at every frame, and what it shows now.
    struct ZoneView
    {
        Entity box;
        Entity label;
        const core::ProfileZone* zone = nullptr;
        bool gpu = false;
        double laneLength = 0.0;
    };
    // A line of the tree of the CPU times that folds.
    struct Fold
    {
        Entity arrow;
        std::string path;
    };
    enum class Tab : std::uint8_t
    {
        Cpu,
        Gpu,
        Memory,
    };

    bool built = false;
    float builtFont = 0.0f;
    bool sideBySide = false;

    Button record;
    Button forget;
    Entity frameText;
    Entity cpuText;
    Entity workingText;
    Entity gpuText;

    Entity bars;
    std::array<Entity, 2> guideLabels{};
    std::vector<std::uint64_t> barFrames;
    // What each recorded frame spent waiting, found once: a frame does not change once recorded.
    std::unordered_map<std::uint64_t, std::uint64_t> waited;

    Entity timeline;
    Entity lanesLayer;
    Entity marksLayer;
    Entity zonesLayer;
    Entity namesBack;
    Entity namesLayer;
    Entity emptyText;
    std::vector<Entity> laneBacks;
    std::vector<Entity> laneNames;
    std::vector<std::pair<Entity, Entity>> marks;
    std::vector<ZoneView> zones;
    bool dragging = false;
    float dragFrom = 0.0f;

    std::array<Button, 3> tabs{};
    Tab tab = Tab::Cpu;
    Entity tableScroll;
    Entity tableContent;
    std::string tableSignature;
    std::vector<Fold> folds;
    // The lines of the tree folded or unfolded against their default: open for threads and the first
    // level, closed deeper.
    std::unordered_set<std::string> toggled;

    void build(EditorUiKit& kit, float width, float height);
    void clearAll();
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
    void syncToolbar(EditorUiKit& kit, const core::ProfileFrame* frame);
    void syncBars(std::span<const FramePtr> frames, const core::ProfileFrame* shown);
    void syncTimeline(EditorUiKit& kit, ProfilerView& view, const core::ProfileFrame* frame, const std::vector<std::string>& threads);
    void answerTimeline(ToolsState& state, ProfilerView& view, const core::ProfileFrame* frame);
    void buildTable(ToolsState& state, EditorUiKit& kit, const core::ProfileFrame* frame, const std::vector<std::string>& threads);
    // A line of a table: its text at the left, indented, and cells at the right.
    Entity tableRow(float height = 0.0f);
    Entity cell(Entity row, float right, float width, std::string value, std::string_view style, bool bold = false);
    [[nodiscard]] bool isOpen(const std::string& path, bool openByDefault) const;
};

void ProfilerUi::clearAll()
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    laneBacks.clear();
    laneNames.clear();
    marks.clear();
    zones.clear();
    folds.clear();
    tableSignature.clear();
    built = false;
}

void ProfilerUi::build(EditorUiKit& kit, float width, float height)
{
    built = true;
    builtFont = font;
    const float margin = std::round(font * 0.4f);
    const float tool = line - 6.0f;
    const Entity root = add({}, "Profiler", whole());
    panel.setKeyboardNavigation(false);

    // The buttons that pause and forget, and what the frame looked at took.
    const Entity toolbar = add(root, "Toolbar", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {margin, margin},
                                                       .offsetMax = {-margin, margin + line}});
    scene().add<scene::UiLayout>(toolbar, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.5f, .align = scene::TextAlign::Left});
    record = toolButton(kit, toolbar, Icon::Pause, middle({tool, tool}));
    forget = toolButton(kit, toolbar, Icon::Trash, middle({tool, tool}));
    tooltip(forget.entity, "Forget the recorded frames");
    const Entity separator = add(toolbar, "Separator", middle({1.0f, std::round(line * 0.6f)}), "separator");
    scene().add<scene::UiImage>(separator, scene::UiImage{.raycastTarget = false});
    frameText = text(toolbar, middle({1.0f, line}), "", "text");
    cpuText = text(toolbar, middle({1.0f, line}), "", {});
    workingText = text(toolbar, middle({1.0f, line}), "", "dim");
    gpuText = text(toolbar, middle({1.0f, line}), "", "text");

    // The frames and their timeline beside the tables in a wide panel, above them in a tall one.
    sideBySide = width >= height * 1.6f;
    const Entity body = add(root, "Body", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {margin, margin * 2.0f + line},
                                                 .offsetMax = {-margin, -margin}});
    scene().add<scene::UiSplitter>(body, scene::UiSplitter{.vertical = !sideBySide,
                                                           .position = std::max(std::round(sideBySide ? width * 0.6f : height * 0.5f), font * 8.0f),
                                                           .minSize = font * 8.0f});
    const Entity framesPane = add(body, "Frames", whole());
    const Entity tablesPane = add(body, "Tables", whole());

    const float barsHeight = std::round(line * 2.6f);
    const Entity barsBox = add(framesPane, "Bars", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f},
                                                          .offsetMax = {0.0f, barsHeight}},
                               "list");
    scene().add<scene::UiImage>(barsBox);
    bars = add(barsBox, "Plot", whole(math::Vec4{2.0f}));
    scene().add<scene::UiImage>(bars, scene::UiImage{.color = {0.0f, 0.0f, 0.0f, 0.0f}});
    scene().add<scene::UiButton>(bars);
    scene().add<scene::UiPlot>(bars, scene::UiPlot{.kind = scene::UiPlotKind::Bars});
    tooltip(bars, "");
    for (Entity& label : guideLabels)
    {
        label = text(barsBox, fixed({font * 5.0f, font * 1.2f}), "", "dim", false, scene::TextAlign::Left, std::round(font * 0.85f));
    }

    timeline = add(framesPane, "Timeline", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, barsHeight + margin},
                                                  .offsetMax = {0.0f, 0.0f}, .clipChildren = true},
                   "list");
    scene().add<scene::UiImage>(timeline);
    lanesLayer = add(timeline, "Lanes", whole());
    marksLayer = add(timeline, "Marks", whole());
    zonesLayer = add(timeline, "Zones", whole());
    namesBack = add(timeline, "Names", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {font * 6.0f, 0.0f}},
                    "list");
    scene().add<scene::UiImage>(namesBack, scene::UiImage{.raycastTarget = false});
    namesLayer = add(timeline, "Names", whole());
    emptyText = text(timeline, whole(), "", "dim", false, scene::TextAlign::Center);

    // The tabs of the tables, and the lines of the one shown.
    const Entity tabRow = add(tablesPane, "Tabs", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f},
                                                         .offsetMax = {0.0f, line}});
    scene().add<scene::UiLayout>(tabRow, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = 2.0f, .align = scene::TextAlign::Left});
    const std::array<const char*, 3> names{"CPU", "GPU", "Memory"};
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
        tabs[index] = button(kit, tabRow, std::nullopt, names[index], "row", std::round(font * 5.5f), line - 2.0f);
    }
    tableScroll = add(tablesPane, "Table", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, line + margin},
                                                  .offsetMax = {0.0f, 0.0f}},
                      "scroll");
    scene().add<scene::UiScroll>(tableScroll, scene::UiScroll{.speed = line * 3.0f});
    tableContent = add(tableScroll, "Lines", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f},
                                                    .offsetMax = {-10.0f, 0.0f}});
    scene().add<scene::UiLayout>(tableContent, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = 1.0f, .align = scene::TextAlign::Left});
}

void ProfilerUi::syncToolbar(EditorUiKit& kit, const core::ProfileFrame* frame)
{
    const ThemeColors& colors = themeColors();
    const bool paused = core::profiler::isPaused();
    scene().get<scene::UiImage>(record.icon).texture = kit.icon(paused ? Icon::Play : Icon::Pause);
    tooltip(record.entity, paused ? "Resume recording" : "Pause recording");
    if (frame == nullptr)
    {
        fitText(kit, frameText, paused ? "Recording is paused" : "Waiting for frames");
        scene().get<UiRect>(frameText).style = "dim";
        fitText(kit, cpuText, "");
        fitText(kit, workingText, "");
        fitText(kit, gpuText, "");
        return;
    }
    const double total = milliseconds(frame->duration());
    const double working = milliseconds(frame->duration() - waitingTime(*frame));
    scene().get<UiRect>(frameText).style = "text";
    fitText(kit, frameText, std::format("Frame {}", frame->index));
    fitText(kit, cpuText, std::format("CPU {:.2f} ms", total));
    scene().get<scene::UiText>(cpuText).color = linearColor(frameColor(total));
    fitText(kit, workingText, std::format("(working {:.2f} ms)", working));
    fitText(kit, gpuText, frame->gpuMeasured ? std::format("GPU {:.2f} ms", milliseconds(frame->gpuDuration)) : std::string("GPU -"));
    static_cast<void>(colors);
}

void ProfilerUi::syncBars(std::span<const FramePtr> frames, const core::ProfileFrame* shown)
{
    const ThemeColors& colors = themeColors();
    const std::span<const FramePtr> drawn = frames.last(std::min(frames.size(), barCount));
    scene::UiPlot& plot = scene().get<scene::UiPlot>(bars);
    plot.values.clear();
    plot.backValues.clear();
    plot.colors.clear();
    barFrames.clear();
    // As tall as the longest frame shown, so that fast frames still show how they vary; a hitch of a
    // second does not flatten the others.
    double top = 0.0;
    for (const FramePtr& frame : drawn)
    {
        const double total = milliseconds(frame->duration());
        top = std::max(top, total);
        auto known = waited.find(frame->index);
        if (known == waited.end())
        {
            known = waited.emplace(frame->index, waitingTime(*frame)).first;
        }
        plot.backValues.push_back(static_cast<float>(total));
        plot.values.push_back(static_cast<float>(total - milliseconds(known->second)));
        plot.colors.push_back(linearColor(frameColor(total)));
        barFrames.push_back(frame->index);
    }
    // The frames the profiler let go are forgotten.
    const std::uint64_t oldest = drawn.empty() ? 0 : drawn.front()->index;
    std::erase_if(waited, [&](const auto& entry) { return entry.first < oldest; });
    top = std::clamp(top * 1.15, 1.0, lateMilliseconds * 3.0);
    plot.minValue = 0.0f;
    plot.maxValue = static_cast<float>(top);
    plot.backColor = math::Vec4{1.0f, 1.0f, 1.0f, 0.3f};
    plot.guideColor = linearColor(withAlpha(colors.textDim, 0.45f));
    plot.highlightColor = linearColor(withAlpha(colors.text, 0.22f));
    const auto found = shown != nullptr ? std::ranges::find(barFrames, shown->index) : barFrames.end();
    plot.highlighted = found != barFrames.end() ? static_cast<std::int32_t>(found - barFrames.begin()) : -1;

    // What the frames are measured against, when the frames shown come near, named at the left.
    const std::array<std::pair<double, const char*>, 2> guides{{{1000.0 / 60.0, "60 FPS"}, {1000.0 / 30.0, "30 FPS"}}};
    plot.guides.clear();
    const ui::LaidOutRect* const area = panel.world().canvases().empty() ? nullptr : panel.world().canvases().front().layout.find(bars);
    const float height = area != nullptr ? area->size().y : 0.0f;
    for (std::size_t index = 0; index < guides.size(); ++index)
    {
        const bool shownGuide = guides[index].first <= top && height > 0.0f;
        UiRect& label = scene().get<UiRect>(guideLabels[index]);
        label.visible = shownGuide;
        if (!shownGuide)
        {
            continue;
        }
        plot.guides.push_back(static_cast<float>(guides[index].first));
        const float y = 2.0f + height * (1.0f - static_cast<float>(guides[index].first / top));
        label.offsetMin = {font * 0.3f, y - font * 1.2f};
        label.offsetMax = {font * 5.3f, y};
        scene().get<scene::UiText>(guideLabels[index]).text = guides[index].second;
    }
}

void ProfilerUi::syncTimeline(EditorUiKit& kit, ProfilerView& view, const core::ProfileFrame* frame, const std::vector<std::string>& threads)
{
    const ThemeColors& colors = themeColors();
    const ui::LaidOutRect* const area = panel.world().canvases().empty() ? nullptr : panel.world().canvases().front().layout.find(timeline);
    const math::Vec2 size = area != nullptr ? area->size() : math::Vec2{0.0f};
    std::size_t lanesUsed = 0;
    std::size_t marksUsed = 0;
    std::size_t zonesUsed = 0;

    std::vector<Lane> lanes;
    double length = 0.0;
    if (frame != nullptr)
    {
        for (const core::ProfileZone& zone : frame->cpu)
        {
            auto lane = std::ranges::find(lanes, zone.thread, &Lane::thread);
            if (lane == lanes.end())
            {
                lanes.push_back({.thread = zone.thread});
                lane = lanes.end() - 1;
            }
            lane->rows = std::max(lane->rows, zone.depth + 1);
        }
        std::ranges::sort(lanes, {}, &Lane::thread);
        if (!frame->gpu.empty())
        {
            lanes.push_back({.gpu = true});
        }
        length = static_cast<double>(std::max(frame->duration(), frame->gpuDuration));
    }
    scene::UiText& empty = scene().get<scene::UiText>(emptyText);
    empty.text = frame == nullptr ? std::string{} : lanes.empty() || length <= 0.0 ? std::string("No zone in this frame") : std::string{};

    float names = 0.0f;
    for (const Lane& lane : lanes)
    {
        names = std::max(names, kit.textWidth(EditorUiKit::regularFont(), lane.gpu ? "GPU" : threadName(threads, lane.thread), font));
    }
    names = std::round(names + font * 1.2f);
    const float left = names;
    const float areaWidth = size.x - left;
    const bool drawable = frame != nullptr && !lanes.empty() && length > 0.0 && areaWidth > 16.0f;
    scene().get<UiRect>(namesBack).offsetMax.x = names;
    scene().get<UiRect>(namesBack).visible = drawable;

    if (drawable)
    {
        // The part of the frame shown, kept within it.
        double begin = 0.0;
        double end = length;
        if (view.visibleEnd > view.visibleBegin)
        {
            const double span = std::min(view.visibleEnd - view.visibleBegin, length);
            begin = std::clamp(view.visibleBegin, 0.0, length - span);
            end = begin + span;
        }
        const auto xOf = [&](double time) { return left + static_cast<float>((time - begin) / (end - begin)) * areaWidth; };

        // Marks at least 70 units apart, at 1, 2 or 5 times a power of ten.
        const double minimumStep = (end - begin) * 70.0 / static_cast<double>(areaWidth);
        const double power = std::pow(10.0, std::floor(std::log10(minimumStep)));
        double step = power * 10.0;
        for (const double factor : {1.0, 2.0, 5.0})
        {
            if (power * factor >= minimumStep)
            {
                step = power * factor;
                break;
            }
        }
        const float rulerHeight = std::round(font * 1.4f);
        for (double time = std::ceil(begin / step) * step; time <= end && marksUsed < 64; time += step)
        {
            if (marksUsed == marks.size())
            {
                const Entity mark = add(marksLayer, "Mark", fixed({1.0f, 1.0f}), "separator");
                scene().add<scene::UiImage>(mark, scene::UiImage{.raycastTarget = false});
                const Entity label = text(marksLayer, fixed({1.0f, rulerHeight}), "", "dim", false, scene::TextAlign::Left, std::round(font * 0.85f));
                marks.emplace_back(mark, label);
            }
            const auto& [mark, label] = marks[marksUsed++];
            const float x = xOf(time);
            UiRect& rule = scene().get<UiRect>(mark);
            rule.visible = true;
            rule.offsetMin = {x, 0.0f};
            rule.offsetMax = {x + 1.0f, size.y};
            UiRect& labelRect = scene().get<UiRect>(label);
            labelRect.visible = true;
            labelRect.offsetMin = {x + 3.0f, 0.0f};
            labelRect.offsetMax = {x + 3.0f + font * 5.0f, rulerHeight};
            scene().get<scene::UiText>(label).text = formatTime(time, step);
        }

        const float rowHeight = std::round(font * 1.45f);
        const float laneGap = 3.0f;
        float y = rulerHeight + 2.0f;
        for (const Lane& lane : lanes)
        {
            const float laneHeight = static_cast<float>(lane.rows) * rowHeight;
            if (lanesUsed == laneBacks.size())
            {
                const Entity back = add(lanesLayer, "Lane", fixed({1.0f, 1.0f}));
                scene().add<scene::UiImage>(back, scene::UiImage{.color = linearColor(withAlpha(colors.panel, 0.35f)), .raycastTarget = false});
                laneBacks.push_back(back);
                laneNames.push_back(text(namesLayer, fixed({1.0f, rowHeight}), "", "dim"));
            }
            UiRect& back = scene().get<UiRect>(laneBacks[lanesUsed]);
            back.visible = true;
            back.offsetMin = {0.0f, y};
            back.offsetMax = {size.x, y + laneHeight};
            UiRect& name = scene().get<UiRect>(laneNames[lanesUsed]);
            name.visible = true;
            name.offsetMin = {font * 0.5f, y};
            name.offsetMax = {names, y + rowHeight};
            scene().get<scene::UiText>(laneNames[lanesUsed]).text = lane.gpu ? std::string("GPU") : threadName(threads, lane.thread);
            ++lanesUsed;

            // Zones in nanoseconds since the start of the frame; those of the GPU since it started it.
            const std::span<const core::ProfileZone> laneZones = lane.gpu ? std::span(frame->gpu) : std::span(frame->cpu);
            const double base = lane.gpu ? 0.0 : static_cast<double>(frame->begin);
            const double laneLength = milliseconds(lane.gpu ? frame->gpuDuration : frame->duration());
            for (const core::ProfileZone& zone : laneZones)
            {
                if (!lane.gpu && zone.thread != lane.thread)
                {
                    continue;
                }
                float x0 = xOf(static_cast<double>(zone.begin) - base);
                float x1 = xOf(static_cast<double>(zone.end) - base);
                if (x1 < left || x0 > size.x)
                {
                    continue;
                }
                x0 = std::max(x0, left);
                x1 = std::max(std::min(x1, size.x), x0 + 1.0f);
                const float y0 = y + static_cast<float>(lane.gpu ? 0 : zone.depth) * rowHeight;
                if (zonesUsed == zones.size())
                {
                    ZoneView made;
                    made.box = add(zonesLayer, "Zone", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .clipChildren = true});
                    scene().add<scene::UiImage>(made.box, scene::UiImage{.cornerRadius = 2.0f});
                    made.label = text(made.box, whole(math::Vec4{4.0f, 0.0f, 2.0f, 0.0f}), "", "text", false, scene::TextAlign::Left,
                                      std::round(font * 0.9f));
                    tooltip(made.box, "");
                    zones.push_back(made);
                }
                ZoneView& shown = zones[zonesUsed++];
                shown.zone = &zone;
                shown.gpu = lane.gpu;
                shown.laneLength = laneLength;
                UiRect& box = scene().get<UiRect>(shown.box);
                box.visible = true;
                box.offsetMin = {x0, y0};
                box.offsetMax = {x1, y0 + rowHeight - 1.0f};
                scene().get<scene::UiImage>(shown.box).color = linearColor(withAlpha(zoneColor(zone), 0.55f));
                const double duration = milliseconds(zone.duration());
                scene::UiText& label = scene().get<scene::UiText>(shown.label);
                label.text = x1 - x0 > font * 2.0f ? std::format("{} {:.2f}", zone.name, duration) : std::string{};
            }
            y += laneHeight + laneGap;
        }
    }
    for (std::size_t index = lanesUsed; index < laneBacks.size(); ++index)
    {
        scene().get<UiRect>(laneBacks[index]).visible = false;
        scene().get<UiRect>(laneNames[index]).visible = false;
    }
    for (std::size_t index = marksUsed; index < marks.size(); ++index)
    {
        scene().get<UiRect>(marks[index].first).visible = false;
        scene().get<UiRect>(marks[index].second).visible = false;
    }
    for (std::size_t index = zonesUsed; index < zones.size(); ++index)
    {
        scene().get<UiRect>(zones[index].box).visible = false;
        zones[index].zone = nullptr;
    }
}

void ProfilerUi::answerTimeline(ToolsState& state, ProfilerView& view, const core::ProfileFrame* frame)
{
    const ui::LaidOutRect* const area = panel.world().canvases().empty() ? nullptr : panel.world().canvases().front().layout.find(timeline);
    if (frame == nullptr || area == nullptr)
    {
        dragging = false;
        return;
    }
    const double length = static_cast<double>(std::max(frame->duration(), frame->gpuDuration));
    const float left = area->min.x + scene().get<UiRect>(namesBack).offsetMax.x;
    const float areaWidth = area->max.x - left;
    if (length <= 0.0 || areaWidth <= 16.0f)
    {
        return;
    }
    const ui::UiInput& input = panel.input();
    const bool inside = input.pointer.x >= area->min.x && input.pointer.x <= area->max.x && input.pointer.y >= area->min.y &&
                        input.pointer.y <= area->max.y;
    double begin = 0.0;
    double end = length;
    if (view.visibleEnd > view.visibleBegin)
    {
        const double span = std::min(view.visibleEnd - view.visibleBegin, length);
        begin = std::clamp(view.visibleBegin, 0.0, length - span);
        end = begin + span;
    }
    // The wheel zooms around the pointer, no closer than a microsecond across the whole width.
    if (inside && input.wheel != 0.0f)
    {
        const double pointer = std::clamp(static_cast<double>((input.pointer.x - left) / areaWidth), 0.0, 1.0);
        const double at = begin + pointer * (end - begin);
        const double span = std::clamp((end - begin) * std::pow(0.8, static_cast<double>(input.wheel)), 1000.0, length);
        begin = std::clamp(at - pointer * span, 0.0, length - span);
        end = begin + span;
    }
    // A drag moves along the frame.
    if (input.pointerPressed && inside)
    {
        dragging = true;
        dragFrom = input.pointer.x;
    }
    if (dragging && input.pointerDown)
    {
        const double span = end - begin;
        begin = std::clamp(begin - static_cast<double>((input.pointer.x - dragFrom) / areaWidth) * span, 0.0, length - span);
        end = begin + span;
        dragFrom = input.pointer.x;
    }
    if (!input.pointerDown)
    {
        dragging = false;
    }
    // A double click shows the whole frame again.
    if (inside && panel.hovered() && state.input.doubleClicked(Mouse::Left))
    {
        begin = 0.0;
        end = length;
    }
    const bool entire = begin <= 0.0 && end >= length;
    view.visibleBegin = entire ? 0.0 : begin;
    view.visibleEnd = entire ? 0.0 : end;
}

Entity ProfilerUi::tableRow(float height)
{
    const Entity row = add(tableContent, "Line", wide(height > 0.0f ? height : std::round(font * 1.6f)), "flat");
    scene().add<scene::UiImage>(row);
    return row;
}

Entity ProfilerUi::cell(Entity row, float right, float width, std::string value, std::string_view style, bool bold)
{
    return text(row, UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-right - width, 0.0f}, .offsetMax = {-right, 0.0f}},
                std::move(value), style, bold, scene::TextAlign::Right);
}

bool ProfilerUi::isOpen(const std::string& path, bool openByDefault) const
{
    return toggled.contains(path) != openByDefault;
}

void ProfilerUi::buildTable(ToolsState& state, EditorUiKit& kit, const core::ProfileFrame* frame, const std::vector<std::string>& threads)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(tableContent); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    folds.clear();
    const float number = std::round(font * 5.0f);
    const float gapWidth = std::round(font * 0.6f);
    const auto message = [&](std::string value, std::string_view style = "dim") {
        const Entity row = tableRow();
        text(row, whole(math::Vec4{font * 0.4f, 0.0f, 0.0f, 0.0f}), std::move(value), style);
    };
    const auto titles = [&](std::span<const std::pair<const char*, float>> columns, const char* first) {
        const Entity row = tableRow();
        text(row, whole(math::Vec4{font * 0.4f, 0.0f, 0.0f, 0.0f}), first, "dim", true);
        float right = gapWidth;
        for (auto column = columns.rbegin(); column != columns.rend(); ++column)
        {
            cell(row, right, column->second, column->first, "dim", true);
            right += column->second + gapWidth;
        }
    };

    if (tab == Tab::Cpu)
    {
        if (frame == nullptr)
        {
            return;
        }
        // The zones of the frame by thread, summed where they nest the same way: the time in each, the
        // time left once the zones inside are taken out, and how many times it ran.
        const std::vector<core::ProfileSummary> lines = core::profiler::summarize(frame->cpu);
        if (lines.empty())
        {
            message("No zone in this frame");
            return;
        }
        const std::array<std::pair<const char*, float>, 3> columns{{{"Total ms", number}, {"Self ms", number}, {"Calls", std::round(font * 3.5f)}}};
        titles(columns, "Zone");
        const float callsRight = gapWidth;
        const float selfRight = callsRight + columns[2].second + gapWidth;
        const float totalRight = selfRight + number + gapWidth;
        const float indent = std::round(font * 1.1f);
        const float arrow = std::round(font * 1.2f);
        std::vector<std::string> paths(lines.size());
        std::vector<bool> shown(lines.size(), false);
        std::uint16_t thread = 0xFFFF;
        std::string threadPath;
        bool threadOpen = false;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            const core::ProfileSummary& summary = lines[index];
            if (summary.parent < 0 && summary.thread != thread)
            {
                // The lines of a thread follow each other, under its name and the time it was busy.
                thread = summary.thread;
                threadPath = threadName(threads, thread);
                std::uint64_t busy = 0;
                for (std::size_t next = index; next < lines.size() && lines[next].thread == thread; ++next)
                {
                    busy += lines[next].parent < 0 ? lines[next].inclusive : 0;
                }
                threadOpen = isOpen(threadPath, true);
                const Entity row = tableRow();
                const Entity fold = add(row, "Arrow", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f},
                                                             .offsetMax = {arrow, 0.0f}});
                scene().add<scene::UiFoldout>(fold, scene::UiFoldout{.expanded = threadOpen, .arrowColor = linearColor(themeColors().textDim)});
                folds.push_back(Fold{.arrow = fold, .path = threadPath});
                text(row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {arrow + 2.0f, 0.0f}, .offsetMax = {-totalRight - number, 0.0f}},
                     threadPath, "text", true);
                cell(row, totalRight, number, std::format("{:.3f}", milliseconds(busy)), "text", true);
            }
            paths[index] = (summary.parent < 0 ? threadPath : paths[static_cast<std::size_t>(summary.parent)]) + "/" + summary.name;
            const bool parentShown = summary.parent < 0 ? threadOpen : shown[static_cast<std::size_t>(summary.parent)];
            if (!parentShown)
            {
                continue;
            }
            const bool leaf = index + 1 >= lines.size() || lines[index + 1].parent != static_cast<std::int32_t>(index);
            const bool open = !leaf && isOpen(paths[index], summary.depth == 0);
            shown[index] = open;
            const float left = indent * static_cast<float>(summary.depth + 1);
            const Entity row = tableRow();
            if (!leaf)
            {
                const Entity fold = add(row, "Arrow", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {left, 0.0f},
                                                             .offsetMax = {left + arrow, 0.0f}});
                scene().add<scene::UiFoldout>(fold, scene::UiFoldout{.expanded = open, .arrowColor = linearColor(themeColors().textDim)});
                folds.push_back(Fold{.arrow = fold, .path = paths[index]});
            }
            const Entity name = text(row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {left + arrow + 2.0f, 0.0f},
                                                 .offsetMax = {-totalRight - number, 0.0f}, .clipChildren = true},
                                     summary.name, "text");
            scene().get<scene::UiText>(name).raycastTarget = true;
            tooltip(name, summary.name);
            cell(row, totalRight, number, std::format("{:.3f}", milliseconds(summary.inclusive)), "text");
            cell(row, selfRight, number, std::format("{:.3f}", milliseconds(summary.exclusive)), summary.exclusive * 20 < summary.inclusive ? "dim" : "text");
            cell(row, callsRight, columns[2].second, std::format("{}", summary.calls), "text");
        }
        return;
    }

    if (tab == Tab::Gpu)
    {
        if (frame == nullptr)
        {
            return;
        }
        if (!frame->gpuMeasured)
        {
            message(core::profiler::isPaused() ? "The GPU did not time this frame" : "Waiting for the GPU to finish this frame");
            return;
        }
        // The passes of the render graph, the longest first; passes of the same name, such as the
        // steps of the bloom, are summed.
        struct Pass
        {
            std::string_view name;
            std::uint64_t duration = 0;
            std::uint32_t count = 0;
        };
        std::vector<Pass> passes;
        for (const core::ProfileZone& zone : frame->gpu)
        {
            auto pass = std::ranges::find(passes, std::string_view(zone.name), &Pass::name);
            if (pass == passes.end())
            {
                passes.push_back({.name = zone.name});
                pass = passes.end() - 1;
            }
            pass->duration += zone.duration();
            ++pass->count;
        }
        std::ranges::stable_sort(passes, std::ranges::greater{}, &Pass::duration);
        const std::array<std::pair<const char*, float>, 2> columns{{{"ms", number}, {"Share", number}}};
        titles(columns, "Pass");
        const double total = std::max(milliseconds(frame->gpuDuration), 1e-9);
        const float shareRight = gapWidth;
        const float msRight = shareRight + number + gapWidth;
        for (const Pass& pass : passes)
        {
            const Entity row = tableRow();
            text(row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.4f, 0.0f}, .offsetMax = {-msRight - number, 0.0f}},
                 pass.count > 1 ? std::format("{}  x{}", pass.name, pass.count) : std::string(pass.name), "text");
            cell(row, msRight, number, std::format("{:.3f}", milliseconds(pass.duration)), "text");
            cell(row, shareRight, number, std::format("{:.0f}%", 100.0 * milliseconds(pass.duration) / total), "dim");
        }
        const Entity row = tableRow();
        text(row, whole(math::Vec4{font * 0.4f, 0.0f, 0.0f, 0.0f}), "Whole frame", "text", true);
        cell(row, msRight, number, std::format("{:.3f}", total), "text", true);
        return;
    }

    // What the loaded assets take in memory and on the GPU, by type and for the heaviest of them.
    ProfilerView& view = state.profiler;
    if (!state.memoryReport)
    {
        message("No assets are loaded here");
        return;
    }
    const asset::MemoryReport& report = view.memory;
    std::size_t count = 0;
    std::size_t cpu = 0;
    std::size_t gpu = 0;
    for (const asset::MemoryByType& type : report.types)
    {
        count += type.count;
        cpu += type.cpuBytes;
        gpu += type.gpuBytes;
    }
    const render::RendererStats stats = state.renderer.stats();
    message(std::format("{} assets loaded: {} in memory, {} on the GPU", count, formatBytes(cpu), formatBytes(gpu)), "text");
    message(std::format("The GPU memory of the engine, buffers of the frame included: {} of {}", formatBytes(stats.gpuMemoryUsage),
                     formatBytes(stats.gpuMemoryBudget)));
    const float bytes = std::round(font * 5.5f);
    const std::array<std::pair<const char*, float>, 3> typeColumns{{{"Count", std::round(font * 3.5f)}, {"Memory", bytes}, {"GPU", bytes}}};
    titles(typeColumns, "Type");
    const float gpuRight = gapWidth;
    const float memoryRight = gpuRight + bytes + gapWidth;
    const float countRight = memoryRight + bytes + gapWidth;
    for (const asset::MemoryByType& type : report.types)
    {
        const Entity row = tableRow();
        text(row, whole(math::Vec4{font * 0.4f, 0.0f, 0.0f, 0.0f}), std::string(asset::toString(type.type)), "text");
        cell(row, countRight, typeColumns[0].second, std::format("{}", type.count), "text");
        cell(row, memoryRight, bytes, formatBytes(type.cpuBytes), "text");
        cell(row, gpuRight, bytes, formatBytes(type.gpuBytes), "text");
    }
    if (!report.largest.empty())
    {
        message("Heaviest assets", "text");
        const std::array<std::pair<const char*, float>, 3> assetColumns{{{"Type", std::round(font * 7.0f)}, {"Memory", bytes}, {"GPU", bytes}}};
        titles(assetColumns, "Asset");
        const float typeRight = memoryRight + bytes + gapWidth;
        for (const asset::AssetMemory& entry : report.largest)
        {
            const Entity row = tableRow();
            text(row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.4f, 0.0f},
                             .offsetMax = {-typeRight - assetColumns[0].second, 0.0f}, .clipChildren = true},
                 entry.name, "text");
            cell(row, typeRight, assetColumns[0].second, std::string(asset::toString(entry.type)), "dim");
            cell(row, memoryRight, bytes, formatBytes(entry.cpuBytes), "text");
            cell(row, gpuRight, bytes, formatBytes(entry.gpuBytes), "text");
        }
    }
    static_cast<void>(kit);
}

void ProfilerUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    setFont(state.theme.fontSize);
    const math::Vec2 size = panel.size();
    const bool wanted = size.x >= size.y * 1.6f;
    if (!built || builtFont != font || (wanted != sideBySide && size.x > 0.0f))
    {
        clearAll();
        build(kit, size.x > 0.0f ? size.x : 800.0f, size.y > 0.0f ? size.y : 400.0f);
    }
    styleTooltips(colors);

    ProfilerView& view = state.profiler;
    const std::vector<FramePtr> frames = core::profiler::history();
    const FramePtr frame = shownFrame(view, frames, state.input.time());
    const std::vector<std::string> threads = core::profiler::threadNames();
    // The memory of the assets, read twice a second while its tab shows.
    const double now = state.input.time();
    std::uint64_t memoryStamp = 0;
    if (tab == Tab::Memory && state.memoryReport)
    {
        if (view.memoryRead < 0.0 || now - view.memoryRead >= memorySeconds)
        {
            view.memory = state.memoryReport();
            view.memoryRead = now;
        }
        memoryStamp = static_cast<std::uint64_t>(view.memoryRead * 1000.0);
    }

    syncToolbar(kit, frame.get());
    syncBars(frames, frame.get());
    syncTimeline(kit, view, frame.get(), threads);
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
        scene().get<UiRect>(tabs[index].entity).style = static_cast<std::size_t>(tab) == index ? "row_selected" : "row";
    }
    const std::string signature = std::format("{}|{}|{}|{}|{}|{}", static_cast<int>(tab), frame != nullptr ? frame->index : 0,
                                              frame != nullptr && frame->gpuMeasured, toggled.size(), memoryStamp, font);
    if (signature != tableSignature)
    {
        tableSignature = signature;
        buildTable(state, kit, frame.get(), threads);
    }

    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();
    const ui::UiWorld& world = panel.world();
    if (world.wasClicked(record.entity))
    {
        core::profiler::setPaused(!core::profiler::isPaused());
        view.frameTaken = -1.0;
    }
    if (world.wasClicked(forget.entity))
    {
        core::profiler::clear();
        view.frame = 0;
    }
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
        if (world.wasClicked(tabs[index].entity))
        {
            tab = static_cast<Tab>(index);
            scene().get<scene::UiScroll>(tableScroll).offset = math::Vec2{0.0f};
        }
    }
    for (const Fold& fold : folds)
    {
        if (world.wasChanged(fold.arrow) && !toggled.erase(fold.path))
        {
            toggled.insert(fold.path);
        }
    }

    // The frame under the pointer, in the tooltip of the bars; a click looks at it and pauses.
    const std::int32_t pointed = world.plotValueAt(scene(), bars);
    if (pointed >= 0 && static_cast<std::size_t>(pointed) < barFrames.size())
    {
        const std::uint64_t index = barFrames[static_cast<std::size_t>(pointed)];
        const auto found = std::ranges::find(frames, index, [](const FramePtr& kept) { return kept->index; });
        if (found != frames.end())
        {
            const core::ProfileFrame& pointedFrame = **found;
            const auto known = waited.find(pointedFrame.index);
            const std::uint64_t waiting = known != waited.end() ? known->second : waitingTime(pointedFrame);
            std::string text = std::format("Frame {}\nCPU {:.2f} ms, {:.2f} ms of them waiting", pointedFrame.index,
                                           milliseconds(pointedFrame.duration()), milliseconds(waiting));
            if (pointedFrame.gpuMeasured)
            {
                text += std::format("\nGPU {:.2f} ms", milliseconds(pointedFrame.gpuDuration));
            }
            tooltip(bars, text + "\nClick to look at this frame");
            if (world.wasClicked(bars))
            {
                view.frame = index;
                core::profiler::setPaused(true);
            }
        }
    }
    answerTimeline(state, view, frame.get());

    // The zone under the pointer tells its time and its share of the frame in its tooltip; the
    // others say nothing, so that only one text is made a frame.
    if (const ui::LaidOutRect* const area = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(timeline))
    {
        const math::Vec2 local = panel.input().pointer - area->min;
        for (ZoneView& shown : zones)
        {
            const UiRect& box = scene().get<UiRect>(shown.box);
            const bool over = shown.zone != nullptr && box.visible && local.x >= box.offsetMin.x && local.x <= box.offsetMax.x &&
                              local.y >= box.offsetMin.y && local.y <= box.offsetMax.y;
            if (!over)
            {
                tooltip(shown.box, "");
                continue;
            }
            const double duration = milliseconds(shown.zone->duration());
            tooltip(shown.box, std::format("{}\n{:.3f} ms, {:.1f}% of the frame\n{}", shown.zone->name, duration,
                                           100.0 * duration / std::max(shown.laneLength, 1e-9),
                                           shown.gpu ? std::string("A pass of the render graph, on the GPU") : threadName(threads, shown.zone->thread)));
        }
    }
}

void drawProfilerPanel(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Profiler panel");
    if (!state.showProfiler)
    {
        return;
    }
    if (std::exchange(state.focusProfiler, false))
    {
        focusPanel(state, profilerWindow);
    }
    if (!beginDockedPanel(state, profilerWindow))
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.profilerUi)
    {
        state.profilerUi = std::make_shared<ProfilerUi>();
    }
    state.profilerUi->update(state, kit, core::Duration(state.input.delta()));
    endDockedPanel(state);
}

void renderProfiler(ToolsState& state, render::RenderWorld& world)
{
    if (state.profilerUi && state.uiKit)
    {
        state.profilerUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
