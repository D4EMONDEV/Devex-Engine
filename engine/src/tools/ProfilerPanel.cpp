#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace devex::tools::detail {
namespace {

using FramePtr = std::shared_ptr<const core::ProfileFrame>;

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
constexpr std::array<std::string_view, 4> waitingZones{"Frame limit", "Wait for the GPU", "Acquire the image",
                                                       "Present"};

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

// The same name always has the same color, among those of the kinds of objects.
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
    return std::format("{:.0f} µs", nanoseconds / 1000.0);
}

void textRight(const std::string& text, bool dim = false)
{
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
    if (dim)
    {
        ImGui::TextDisabled("%s", text.c_str());
    }
    else
    {
        ImGui::TextUnformatted(text.c_str());
    }
}

[[nodiscard]] std::string threadName(const std::vector<std::string>& threads, std::uint16_t thread)
{
    return thread < threads.size() ? threads[thread] : std::format("Thread {}", thread);
}

// While recording, the newest frame the GPU has finished, taken again twice a second; once paused,
// the frame chosen in the bars.
[[nodiscard]] FramePtr shownFrame(ProfilerView& view, std::span<const FramePtr> frames)
{
    const auto found = std::ranges::find(frames, view.frame, [](const FramePtr& frame) { return frame->index; });
    const double now = ImGui::GetTime();
    if (!core::profiler::isPaused() && (found == frames.end() || now - view.frameTaken >= followSeconds))
    {
        if (frames.empty())
        {
            view.frame = 0;
            return nullptr;
        }
        // The GPU reports a frame a few frames late: one of the last few has both halves.
        const std::span<const FramePtr> recent = frames.last(std::min<std::size_t>(frames.size(), 8));
        const auto measured = std::ranges::find_if(recent.rbegin(), recent.rend(),
                                                   [](const FramePtr& frame) { return frame->gpuMeasured; });
        const FramePtr chosen = measured != recent.rend() ? *measured : frames.back();
        view.frame = chosen->index;
        view.frameTaken = now;
        return chosen;
    }
    return found != frames.end() ? *found : nullptr;
}

void drawToolbar(ProfilerView& view, const core::ProfileFrame* frame)
{
    const bool paused = core::profiler::isPaused();
    if (toolButton("record", paused ? icons::Play : icons::Pause, paused ? "Resume recording" : "Pause recording",
                   paused))
    {
        core::profiler::setPaused(!paused);
        view.frameTaken = -1.0;
    }
    ImGui::SameLine();
    if (toolButton("clear", icons::Trash, "Forget the recorded frames"))
    {
        core::profiler::clear();
        view.frame = 0;
    }
    toolbarSeparator();
    ImGui::AlignTextToFramePadding();
    if (frame == nullptr)
    {
        ImGui::TextDisabled("%s", paused ? "Recording is paused" : "Waiting for frames");
        return;
    }
    const double total = milliseconds(frame->duration());
    const double working = milliseconds(frame->duration() - waitingTime(*frame));
    const std::string gpu = frame->gpuMeasured ? std::format("{:.2f} ms", milliseconds(frame->gpuDuration)) : "-";
    ImGui::Text("Frame %llu", static_cast<unsigned long long>(frame->index));
    ImGui::SameLine();
    ImGui::TextColored(uiColor(frameColor(total)), "CPU %.2f ms", total);
    ImGui::SameLine();
    ImGui::TextDisabled("(working %.2f ms)", working);
    ImGui::SameLine();
    ImGui::Text("GPU %s", gpu.c_str());
}

// One bar per frame, the newest on the right: what the frame spent working in full color, what it
// spent waiting faint above it. A click shows that frame and pauses the recording.
void drawFrameBars(ProfilerView& view, std::span<const FramePtr> frames, const core::ProfileFrame* shown)
{
    const ThemeColors& colors = themeColors();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 2.6f);
    if (size.x < 8.0f)
    {
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("frames", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const ImVec2 corner(origin.x + size.x, origin.y + size.y);
    draw.AddRectFilled(origin, corner, uiColorU32(colors.field), ImGui::GetStyle().FrameRounding);
    draw.PushClipRect(origin, corner, true);

    const float barWidth = std::max(2.0f, std::floor(size.x / static_cast<float>(barCount)));
    const std::span<const FramePtr> drawn =
        frames.last(std::min(frames.size(), static_cast<std::size_t>(size.x / barWidth)));
    // As tall as the longest frame shown, so that fast frames still show how they vary; a hitch of
    // a second does not flatten the others.
    double top = 0.0;
    for (const FramePtr& frame : drawn)
    {
        top = std::max(top, milliseconds(frame->duration()));
    }
    top = std::clamp(top * 1.15, 1.0, lateMilliseconds * 3.0);
    const auto heightOf = [&](double frameMilliseconds) {
        return static_cast<float>(std::min(frameMilliseconds / top, 1.0)) * size.y;
    };

    const float left = corner.x - static_cast<float>(drawn.size()) * barWidth;
    const float gap = barWidth >= 3.0f ? 1.0f : 0.0f;
    const float mouseX = ImGui::GetIO().MousePos.x;
    const FramePtr* pointed = nullptr;
    for (std::size_t index = 0; index < drawn.size(); ++index)
    {
        const core::ProfileFrame& frame = *drawn[index];
        const double total = milliseconds(frame.duration());
        const double working = total - milliseconds(waitingTime(frame));
        const ImVec4 color = frameColor(total);
        const float x0 = left + static_cast<float>(index) * barWidth;
        const float x1 = x0 + barWidth - gap;
        if (shown != nullptr && frame.index == shown->index)
        {
            draw.AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, corner.y), uiColorU32(withAlpha(colors.text, 0.22f)));
        }
        draw.AddRectFilled(ImVec2(x0, corner.y - heightOf(total)), ImVec2(x1, corner.y),
                           uiColorU32(withAlpha(color, 0.3f)));
        draw.AddRectFilled(ImVec2(x0, corner.y - heightOf(working)), ImVec2(x1, corner.y), uiColorU32(color));
        if (hovered && mouseX >= x0 && mouseX < x0 + barWidth)
        {
            pointed = &drawn[index];
            draw.AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, corner.y), uiColorU32(withAlpha(colors.text, 0.12f)));
        }
    }

    // What the frames are measured against, when the frames shown come near.
    for (const auto& [frameMilliseconds, label] : {std::pair{1000.0 / 60.0, "60 FPS"}, std::pair{1000.0 / 30.0, "30 FPS"}})
    {
        if (frameMilliseconds > top)
        {
            continue;
        }
        const float y = corner.y - heightOf(frameMilliseconds);
        draw.AddLine(ImVec2(origin.x, y), ImVec2(corner.x, y), uiColorU32(withAlpha(colors.textDim, 0.45f)));
        draw.AddText(ImVec2(origin.x + 4.0f, y - ImGui::GetTextLineHeight()), uiColorU32(colors.textDim), label);
    }
    draw.PopClipRect();

    if (pointed != nullptr)
    {
        const core::ProfileFrame& frame = **pointed;
        if (ImGui::BeginTooltip())
        {
            ImGui::Text("Frame %llu", static_cast<unsigned long long>(frame.index));
            ImGui::Text("CPU %.2f ms, %.2f ms of them waiting", milliseconds(frame.duration()),
                        milliseconds(waitingTime(frame)));
            if (frame.gpuMeasured)
            {
                ImGui::Text("GPU %.2f ms", milliseconds(frame.gpuDuration));
            }
            ImGui::TextDisabled("Click to look at this frame");
            ImGui::EndTooltip();
        }
        if (clicked)
        {
            view.frame = frame.index;
            core::profiler::setPaused(true);
        }
    }
}

struct Lane
{
    std::uint16_t thread = 0;
    int rows = 1;
    bool gpu = false;
};

// The zones of the frame on a time axis: a lane per thread, nested zones below the zone they sit
// in, then the passes of the GPU from the moment it started the frame. The wheel zooms around the
// pointer, a drag moves along, a double click shows the whole frame again.
void drawTimeline(ProfilerView& view, const core::ProfileFrame& frame, const std::vector<std::string>& threads)
{
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();

    std::vector<Lane> lanes;
    for (const core::ProfileZone& zone : frame.cpu)
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
    if (!frame.gpu.empty())
    {
        lanes.push_back({.gpu = true});
    }
    const double length = static_cast<double>(std::max(frame.duration(), frame.gpuDuration));
    if (lanes.empty() || length <= 0.0)
    {
        ImGui::TextDisabled("No zone in this frame");
        return;
    }

    float labelWidth = 0.0f;
    for (const Lane& lane : lanes)
    {
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(lane.gpu ? "GPU" : threadName(threads, lane.thread).c_str()).x);
    }
    labelWidth += style.ItemSpacing.x * 2.0f;
    const float rowHeight = ImGui::GetTextLineHeight() + 4.0f;
    const float rulerHeight = ImGui::GetTextLineHeight() + 6.0f;
    const float laneGap = 4.0f;
    float lanesHeight = 0.0f;
    for (const Lane& lane : lanes)
    {
        lanesHeight += static_cast<float>(lane.rows) * rowHeight + laneGap;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, std::max(rulerHeight + lanesHeight, ImGui::GetContentRegionAvail().y));
    const float left = origin.x + labelWidth;
    const float right = origin.x + size.x;
    const float areaWidth = right - left;
    if (areaWidth < 16.0f)
    {
        return;
    }
    ImGui::InvisibleButton("timeline", size);
    const bool hovered = ImGui::IsItemHovered();

    // The part of the frame shown, kept within it.
    double begin = 0.0;
    double end = length;
    if (view.visibleEnd > view.visibleBegin)
    {
        const double span = std::min(view.visibleEnd - view.visibleBegin, length);
        begin = std::clamp(view.visibleBegin, 0.0, length - span);
        end = begin + span;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.0f)
    {
        const double pointer = std::clamp(static_cast<double>((io.MousePos.x - left) / areaWidth), 0.0, 1.0);
        const double at = begin + pointer * (end - begin);
        // No closer than a microsecond across the whole width.
        const double span = std::clamp((end - begin) * std::pow(0.8, static_cast<double>(io.MouseWheel)), 1000.0, length);
        begin = std::clamp(at - pointer * span, 0.0, length - span);
        end = begin + span;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        const double span = end - begin;
        begin = std::clamp(begin - static_cast<double>(io.MouseDelta.x / areaWidth) * span, 0.0, length - span);
        end = begin + span;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        begin = 0.0;
        end = length;
    }
    const bool whole = begin <= 0.0 && end >= length;
    view.visibleBegin = whole ? 0.0 : begin;
    view.visibleEnd = whole ? 0.0 : end;
    const auto xOf = [&](double time) { return left + static_cast<float>((time - begin) / (end - begin)) * areaWidth; };

    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const ImVec2 corner(right, origin.y + size.y);
    draw.PushClipRect(origin, corner, true);

    // Marks at least 70 points apart, at 1, 2 or 5 times a power of ten.
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
    for (double time = std::ceil(begin / step) * step; time <= end; time += step)
    {
        const float x = xOf(time);
        draw.AddLine(ImVec2(x, origin.y), ImVec2(x, corner.y), uiColorU32(withAlpha(colors.border, 0.6f)));
        draw.AddText(ImVec2(x + 3.0f, origin.y + 2.0f), uiColorU32(colors.textDim), formatTime(time, step).c_str());
    }

    const ImVec2 mouse = io.MousePos;
    const core::ProfileZone* pointed = nullptr;
    bool pointedGpu = false;
    std::vector<std::pair<float, const Lane*>> labels;
    float y = origin.y + rulerHeight;
    for (const Lane& lane : lanes)
    {
        const float laneHeight = static_cast<float>(lane.rows) * rowHeight;
        labels.emplace_back(y, &lane);
        draw.AddRectFilled(ImVec2(origin.x, y), ImVec2(right, y + laneHeight), uiColorU32(withAlpha(colors.field, 0.5f)));
        const std::span<const core::ProfileZone> zones = lane.gpu ? frame.gpu : frame.cpu;
        // Zones in nanoseconds since the start of the frame; those of the GPU since it started it.
        const double base = lane.gpu ? 0.0 : static_cast<double>(frame.begin);
        for (const core::ProfileZone& zone : zones)
        {
            if (!lane.gpu && zone.thread != lane.thread)
            {
                continue;
            }
            float x0 = xOf(static_cast<double>(zone.begin) - base);
            float x1 = xOf(static_cast<double>(zone.end) - base);
            if (x1 < left || x0 > right)
            {
                continue;
            }
            x0 = std::max(x0, left);
            x1 = std::max(x1, x0 + 1.0f);
            const float y0 = y + static_cast<float>(lane.gpu ? 0 : zone.depth) * rowHeight;
            const float y1 = y0 + rowHeight - 1.0f;
            const ImVec4 color = zoneColor(zone);
            draw.AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), uiColorU32(withAlpha(color, 0.45f)), 2.0f);
            draw.AddRect(ImVec2(x0, y0), ImVec2(x1, y1), uiColorU32(withAlpha(color, 0.9f)), 2.0f);
            if (x1 - x0 > ImGui::GetFontSize() * 1.5f)
            {
                const std::string label = std::format("{} {:.2f}", zone.name, milliseconds(zone.duration()));
                draw.PushClipRect(ImVec2(x0 + 2.0f, y0), ImVec2(x1 - 2.0f, y1), true);
                draw.AddText(ImVec2(x0 + 4.0f, y0 + 2.0f), uiColorU32(colors.text), label.c_str());
                draw.PopClipRect();
            }
            if (hovered && mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 && mouse.y <= y1)
            {
                pointed = &zone;
                pointedGpu = lane.gpu;
            }
        }
        y += laneHeight + laneGap;
    }

    // The names of the lanes, over the zones that start before the part shown.
    draw.AddRectFilled(origin, ImVec2(left, corner.y), ImGui::GetColorU32(ImGuiCol_WindowBg));
    for (const auto& [top, lane] : labels)
    {
        const std::string name = lane->gpu ? "GPU" : threadName(threads, lane->thread);
        draw.AddText(ImVec2(origin.x + style.ItemSpacing.x, top + 2.0f), uiColorU32(colors.textDim), name.c_str());
    }
    draw.PopClipRect();

    if (pointed != nullptr && ImGui::BeginTooltip())
    {
        boldText(pointed->name);
        const double duration = milliseconds(pointed->duration());
        ImGui::Text("%.3f ms, %.1f%% of the frame", duration,
                    100.0 * duration / milliseconds(pointedGpu ? frame.gpuDuration : frame.duration()));
        ImGui::TextDisabled("%s", pointedGpu ? "A pass of the render graph, on the GPU"
                                             : threadName(threads, pointed->thread).c_str());
        ImGui::EndTooltip();
    }
}

// The zones of the frame by thread, summed where they nest the same way: the time in each, the time
// left once the zones inside are taken out, and how many times it ran.
void drawCpuTable(const core::ProfileFrame& frame, const std::vector<std::string>& threads)
{
    const std::vector<core::ProfileSummary> lines = core::profiler::summarize(frame.cpu);
    if (lines.empty())
    {
        ImGui::TextDisabled("No zone in this frame");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("cpu", 4, flags))
    {
        return;
    }
    const float numberWidth = ImGui::CalcTextSize("Total ms").x + ImGui::GetFontSize();
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_WidthFixed, numberWidth);
    ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_WidthFixed, numberWidth);
    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Calls").x + ImGui::GetFontSize());
    ImGui::TableHeadersRow();

    const float indent = ImGui::GetTreeNodeToLabelSpacing();
    const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    // A line is identified by its path, which stays when the order of the lines changes.
    std::vector<ImGuiID> ids(lines.size());
    std::vector<bool> childrenShown(lines.size(), false);
    std::uint16_t thread = 0xFFFF;
    ImGuiID threadId = 0;
    bool threadOpen = false;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const core::ProfileSummary& line = lines[index];
        if (line.parent < 0 && line.thread != thread)
        {
            // The lines of a thread follow each other, under its name and the time it was busy.
            thread = line.thread;
            const std::string name = threadName(threads, thread);
            std::uint64_t busy = 0;
            for (std::size_t next = index; next < lines.size() && lines[next].thread == thread; ++next)
            {
                busy += lines[next].parent < 0 ? lines[next].inclusive : 0;
            }
            threadId = ImHashStr(name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushFont(editorFonts().bold, 0.0f);
            threadOpen = ImGui::TreeNodeEx(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(threadId)),
                                           nodeFlags | ImGuiTreeNodeFlags_DefaultOpen, "%s", name.c_str());
            ImGui::PopFont();
            ImGui::TableNextColumn();
            textRight(std::format("{:.3f}", milliseconds(busy)));
        }
        ids[index] = ImHashStr(line.name, 0, line.parent < 0 ? threadId : ids[static_cast<std::size_t>(line.parent)]);
        if (line.parent < 0 ? !threadOpen : !childrenShown[static_cast<std::size_t>(line.parent)])
        {
            continue;
        }
        const bool leaf = index + 1 >= lines.size() || lines[index + 1].parent != static_cast<std::int32_t>(index);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const float depthIndent = indent * static_cast<float>(line.depth + 1);
        ImGui::Indent(depthIndent);
        const bool open = ImGui::TreeNodeEx(
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(ids[index])),
            nodeFlags | (leaf ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_None) |
                (line.depth == 0 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None),
            "%s", line.name);
        ImGui::Unindent(depthIndent);
        childrenShown[index] = open && !leaf;
        ImGui::TableNextColumn();
        textRight(std::format("{:.3f}", milliseconds(line.inclusive)));
        ImGui::TableNextColumn();
        textRight(std::format("{:.3f}", milliseconds(line.exclusive)), line.exclusive * 20 < line.inclusive);
        ImGui::TableNextColumn();
        textRight(std::format("{}", line.calls));
    }
    ImGui::EndTable();
}

// The passes of the render graph on the GPU, the longest first; passes of the same name, such as
// the steps of the bloom, are summed.
void drawGpuTable(const core::ProfileFrame& frame)
{
    if (!frame.gpuMeasured)
    {
        ImGui::TextDisabled("%s", core::profiler::isPaused() ? "The GPU did not time this frame"
                                                             : "Waiting for the GPU to finish this frame");
        return;
    }
    struct Pass
    {
        std::string_view name;
        std::uint64_t duration = 0;
        std::uint32_t count = 0;
    };
    std::vector<Pass> passes;
    for (const core::ProfileZone& zone : frame.gpu)
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
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("gpu", 3, flags))
    {
        return;
    }
    const float numberWidth = ImGui::CalcTextSize("Share").x + ImGui::GetFontSize() * 2.0f;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, numberWidth);
    ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, numberWidth);
    ImGui::TableHeadersRow();
    const double total = std::max(milliseconds(frame.gpuDuration), 1e-9);
    for (const Pass& pass : passes)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(pass.name.data(), pass.name.data() + pass.name.size());
        if (pass.count > 1)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("x%u", pass.count);
        }
        ImGui::TableNextColumn();
        textRight(std::format("{:.3f}", milliseconds(pass.duration)));
        ImGui::TableNextColumn();
        textRight(std::format("{:.0f}%", 100.0 * milliseconds(pass.duration) / total));
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    boldText("Whole frame");
    ImGui::TableNextColumn();
    textRight(std::format("{:.3f}", total));
    ImGui::EndTable();
}

// What the loaded assets take in memory and on the GPU, by type and for the heaviest of them.
void drawMemoryTab(ToolsState& state)
{
    ProfilerView& view = state.profiler;
    if (!state.memoryReport)
    {
        ImGui::TextDisabled("No assets are loaded here");
        return;
    }
    const double now = ImGui::GetTime();
    if (view.memoryRead < 0.0 || now - view.memoryRead >= memorySeconds)
    {
        view.memory = state.memoryReport();
        view.memoryRead = now;
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
    ImGui::Text("%zu assets loaded: %s in memory, %s on the GPU", count, formatBytes(cpu).c_str(), formatBytes(gpu).c_str());
    ImGui::TextDisabled("The GPU memory of the engine, buffers of the frame included: %s of %s",
                        formatBytes(stats.gpuMemoryUsage).c_str(), formatBytes(stats.gpuMemoryBudget).c_str());

    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    const float numberWidth = ImGui::CalcTextSize("0000.0 MB").x + ImGui::GetFontSize();
    if (ImGui::BeginChild("memory"))
    {
        if (ImGui::BeginTable("types", 4, flags))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Count").x + ImGui::GetFontSize());
            ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, numberWidth);
            ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, numberWidth);
            ImGui::TableHeadersRow();
            for (const asset::MemoryByType& type : report.types)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(asset::toString(type.type).data());
                ImGui::TableNextColumn();
                textRight(std::format("{}", type.count));
                ImGui::TableNextColumn();
                textRight(formatBytes(type.cpuBytes));
                ImGui::TableNextColumn();
                textRight(formatBytes(type.gpuBytes));
            }
            ImGui::EndTable();
        }
        if (!report.largest.empty())
        {
            ImGui::Spacing();
            boldText("Heaviest assets");
            if (ImGui::BeginTable("largest", 4, flags))
            {
                ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("AnimationClip").x);
                ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, numberWidth);
                ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, numberWidth);
                ImGui::TableHeadersRow();
                for (const asset::AssetMemory& entry : report.largest)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", asset::toString(entry.type).data());
                    ImGui::TableNextColumn();
                    textRight(formatBytes(entry.cpuBytes));
                    ImGui::TableNextColumn();
                    textRight(formatBytes(entry.gpuBytes));
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild();
}

void drawTables(ToolsState& state, const core::ProfileFrame* frame, const std::vector<std::string>& threads)
{
    if (!ImGui::BeginTabBar("profiler tables"))
    {
        return;
    }
    if (ImGui::BeginTabItem("CPU"))
    {
        if (frame != nullptr)
        {
            drawCpuTable(*frame, threads);
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("GPU"))
    {
        if (frame != nullptr)
        {
            drawGpuTable(*frame);
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Memory"))
    {
        drawMemoryTab(state);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void drawFrames(ProfilerView& view, std::span<const FramePtr> frames, const core::ProfileFrame* frame,
                const std::vector<std::string>& threads)
{
    drawFrameBars(view, frames, frame);
    if (ImGui::BeginChild("timeline", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (frame != nullptr)
        {
            drawTimeline(view, *frame, threads);
        }
    }
    ImGui::EndChild();
}

} // namespace

void drawProfilerPanel(ToolsState& state)
{
    if (!state.showProfiler)
    {
        return;
    }
    // A layout saved before this panel existed has no place for it: it opens beside the output.
    if (const ImGuiWindow* const output = ImGui::FindWindowByName(consoleWindow); output != nullptr && output->DockId != 0)
    {
        ImGui::SetNextWindowDockID(output->DockId, ImGuiCond_FirstUseEver);
    }
    if (!ImGui::Begin(profilerWindow, &state.showProfiler))
    {
        ImGui::End();
        return;
    }
    ProfilerView& view = state.profiler;
    const std::vector<FramePtr> frames = core::profiler::history();
    const FramePtr frame = shownFrame(view, frames);
    const std::vector<std::string> threads = core::profiler::threadNames();

    drawToolbar(view, frame.get());
    // Side by side in a wide panel, as docked under the view; one above the other in a tall one.
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x >= available.y * 1.6f)
    {
        if (ImGui::BeginTable("profiler", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("frames", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("tables", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("frames", ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
            {
                drawFrames(view, frames, frame.get(), threads);
            }
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("tables", ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
            {
                drawTables(state, frame.get(), threads);
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }
    else
    {
        if (ImGui::BeginChild("frames", ImVec2(0.0f, available.y * 0.5f), ImGuiChildFlags_ResizeY))
        {
            drawFrames(view, frames, frame.get(), threads);
        }
        ImGui::EndChild();
        drawTables(state, frame.get(), threads);
    }
    ImGui::End();
}

} // namespace devex::tools::detail
