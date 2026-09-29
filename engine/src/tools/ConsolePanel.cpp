// The Output, made with the interface of the engine as Godot's is with its nodes: the messages of the
// engine and of the game, which read like a document. Its text is chosen with the mouse across lines
// and copied, and only the lines on screen have entities.
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/TextLayout.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

// The image the panel is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t outputSurface = 3;
// Two presses closer than this on the same place of the text choose a word.
constexpr double doubleClickSeconds = 0.4;

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

// A message as the panel keeps it: its text with tabs made spaces, and where each of its lines is.
struct Entry
{
    std::uint64_t sequence = 0;
    Severity severity = Severity::Info;
    std::string text;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> lines;
    // The widest of its lines, at the size it was measured at.
    float width = 0.0f;
    float measuredAt = 0.0f;
};

// A line on screen: a line of an entry.
struct Line
{
    std::uint32_t entry = 0;
    std::uint32_t line = 0;
};

// A place in the text, which stays put as messages come and go: a line of a message, and a byte of it.
struct TextPoint
{
    std::uint64_t sequence = 0;
    std::uint32_t line = 0;
    std::uint32_t byte = 0;
};

// A place in the lines shown, which orders two places.
struct Shown
{
    std::size_t index = 0;
    std::uint32_t byte = 0;

    [[nodiscard]] auto operator<=>(const Shown&) const = default;
};

// One line on screen and what it shows.
struct Row
{
    Entity row;
    Entity selection;
    Entity icon;
    Entity text;
};

[[nodiscard]] Entry makeEntry(const LogEntry& logged)
{
    Entry entry{.sequence = logged.sequence, .severity = severityOf(logged.level)};
    entry.text.reserve(logged.message.size());
    for (const char character : logged.message)
    {
        if (character == '\t')
        {
            entry.text += "    ";
        }
        else if (character != '\r')
        {
            entry.text.push_back(character);
        }
    }
    std::size_t begin = 0;
    for (;;)
    {
        const std::size_t end = entry.text.find('\n', begin);
        entry.lines.emplace_back(static_cast<std::uint32_t>(begin),
                                 static_cast<std::uint32_t>(end == std::string::npos ? entry.text.size() : end));
        if (end == std::string::npos)
        {
            break;
        }
        begin = end + 1;
    }
    return entry;
}

[[nodiscard]] bool isWordCharacter(char character) noexcept
{
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' ||
           (static_cast<unsigned char>(character) & 0x80) != 0;
}

} // namespace

// The panel and the entities the code reads and changes.
struct OutputUi : PanelBuilder
{
    OutputUi()
        : PanelBuilder(outputSurface)
    {
    }

    bool built = false;
    float monoSize = 13.0f;
    float lineHeight = 19.0f;
    float gutter = 20.0f;

    Entity filter;
    std::array<Button, 4> toggles;
    Button clear;
    Button follow;
    Entity list;
    Entity scroll;
    Entity lines;
    std::vector<Row> rows;
    Entity menu;
    Button menuCopy;
    Button menuSelectAll;
    Button menuClear;

    // What the log held, copied so that the panel reads it without its lock.
    std::vector<Entry> entries;
    std::uint64_t last = 0;
    std::vector<Line> shown;
    std::array<std::size_t, 4> counts{};
    bool dirty = true;
    std::string shownFilter;
    std::array<bool, 4> shownFlags{};
    float contentWidth = 0.0f;

    std::optional<TextPoint> anchor;
    std::optional<TextPoint> caret;
    bool selecting = false;
    double clock = 0.0;
    double lastPress = -1.0;
    std::optional<TextPoint> lastPressPoint;
    ui::TextLayoutResult letters;

    void build(ToolsState& state, EditorUiKit& kit);
    void sync(ToolsState& state, EditorUiKit& kit);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);

    [[nodiscard]] std::string_view lineText(const Line& line) const
    {
        const Entry& entry = entries[line.entry];
        const auto [begin, end] = entry.lines[line.line];
        return std::string_view(entry.text).substr(begin, end - begin);
    }

    // Where a place of the text stands among the lines shown, if its line is shown.
    [[nodiscard]] std::optional<Shown> shownAt(const TextPoint& point) const
    {
        const auto entry = std::ranges::lower_bound(entries, point.sequence, {}, &Entry::sequence);
        if (entry == entries.end() || entry->sequence != point.sequence)
        {
            return std::nullopt;
        }
        const Line wanted{.entry = static_cast<std::uint32_t>(entry - entries.begin()), .line = point.line};
        const auto found = std::ranges::lower_bound(shown, wanted, [](const Line& left, const Line& right) {
            return left.entry != right.entry ? left.entry < right.entry : left.line < right.line;
        });
        if (found == shown.end() || found->entry != wanted.entry || found->line != wanted.line)
        {
            return std::nullopt;
        }
        return Shown{.index = static_cast<std::size_t>(found - shown.begin()), .byte = point.byte};
    }

    [[nodiscard]] TextPoint pointOf(std::size_t index, std::uint32_t byte) const
    {
        const Line& line = shown[index];
        return TextPoint{.sequence = entries[line.entry].sequence, .line = line.line, .byte = byte};
    }

    // The chosen part of the text, from its start to its end, if it is shown.
    [[nodiscard]] std::optional<std::pair<Shown, Shown>> selection() const
    {
        if (!anchor || !caret)
        {
            return std::nullopt;
        }
        const std::optional<Shown> from = shownAt(*anchor);
        const std::optional<Shown> to = shownAt(*caret);
        if (!from || !to || *from == *to)
        {
            return std::nullopt;
        }
        return *from < *to ? std::pair{*from, *to} : std::pair{*to, *from};
    }

    [[nodiscard]] std::string selectedText() const
    {
        const std::optional<std::pair<Shown, Shown>> chosen = selection();
        if (!chosen)
        {
            return {};
        }
        std::string copied;
        for (std::size_t index = chosen->first.index; index <= chosen->second.index; ++index)
        {
            const std::string_view text = lineText(shown[index]);
            const std::size_t begin = index == chosen->first.index ? std::min<std::size_t>(chosen->first.byte, text.size()) : 0;
            const std::size_t end =
                index == chosen->second.index ? std::min<std::size_t>(chosen->second.byte, text.size()) : text.size();
            copied.append(text.substr(begin, end - std::min(begin, end)));
            if (index != chosen->second.index)
            {
                copied.push_back('\n');
            }
        }
        return copied;
    }

    // Lays out one line in the letters of the panel, for its cursor stops.
    void layLine(EditorUiKit& kit, std::string_view text)
    {
        letters = {};
        if (const asset::FontData* const mono = kit.fontData(EditorUiKit::monoFont()))
        {
            ui::layoutText(*mono, text,
                           ui::TextStyle{.size = monoSize, .verticalAlign = scene::TextVerticalAlign::Top, .wrap = false},
                           math::Vec2{0.0f}, math::Vec2{1.0e6f, lineHeight}, letters);
        }
    }

    [[nodiscard]] float caretX(EditorUiKit& kit, std::string_view text, std::size_t byte)
    {
        layLine(kit, text);
        const ui::CaretStop* const stop = ui::caretAt(letters, byte);
        return stop != nullptr ? stop->position.x : 0.0f;
    }

    // The place of the text under a point of the panel: the nearest line, and the nearest place in it.
    [[nodiscard]] std::optional<TextPoint> pointAt(EditorUiKit& kit, math::Vec2 point)
    {
        const ui::LaidOutRect* const area = panel.world().canvases().empty() ? nullptr : panel.world().canvases().front().layout.find(lines);
        if (area == nullptr || shown.empty())
        {
            return std::nullopt;
        }
        const float y = point.y - area->min.y;
        const std::size_t index = y < 0.0f ? 0
                                            : std::min(static_cast<std::size_t>(y / std::max(lineHeight, 1.0f)),
                                                       shown.size() - 1);
        const std::string_view text = lineText(shown[index]);
        if (y >= static_cast<float>(shown.size()) * lineHeight)
        {
            return pointOf(index, static_cast<std::uint32_t>(text.size()));
        }
        layLine(kit, text);
        const std::size_t byte = ui::offsetAt(letters, math::Vec2{point.x - area->min.x - gutter, lineHeight * 0.5f});
        return pointOf(index, static_cast<std::uint32_t>(std::min(byte, text.size())));
    }
};

void OutputUi::build(ToolsState& state, EditorUiKit& kit)
{
    built = true;
    const float line = font * 2.0f;
    const Entity root = add({}, "Output", whole());
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = font * 0.4f,
                                                       .align = scene::TextAlign::Left});

    // The filter on the left; the kinds of message with their counts, clear and follow on the right.
    const Entity toolbar = add(root, "Toolbar", wide(line));
    scene().add<scene::UiLayout>(toolbar, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                          .spacing = font * 0.25f,
                                                          .align = scene::TextAlign::Left});
    filter = searchField(kit, toolbar, grow(line), state.consoleFilter, "Filter Messages");
    const std::array<Icon, 4> glyphs{Icon::CircleX, Icon::TriangleAlert, Icon::Info, Icon::Bug};
    for (std::size_t index = 0; index < toggles.size(); ++index)
    {
        toggles[index] = button(kit, toolbar, glyphs[index], "0", "button", 0.0f, line);
    }
    clear = button(kit, toolbar, Icon::BrushCleaning, "", "flat", 0.0f, line);
    tooltip(clear.entity, "Clear the output");
    follow = button(kit, toolbar, Icon::ArrowDownToLine, "", "flat", 0.0f, line);
    tooltip(follow.entity, "Follow new messages");

    // The lines, in a list that scrolls both ways and has entities only for those in view.
    list = add(root, "List", whole(), "list");
    scene().add<scene::UiImage>(list);
    scroll = add(list, "Scroll", whole(math::Vec4{2.0f}), "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{.horizontal = true, .vertical = true});
    lines = add(scroll, "Lines", fixed({1.0f, 1.0f}));
    scene().add<scene::UiVirtualList>(lines);

    menu = PanelBuilder::menu("Output menu", font * 12.0f);
    menuCopy = menuItem(kit, menu, Icon::Copy, "Copy");
    menuSelectAll = menuItem(kit, menu, Icon::Scan, "Select All");
    menuSeparator(menu);
    menuClear = menuItem(kit, menu, Icon::BrushCleaning, "Clear");
    scene().add<scene::UiContextMenu>(list, scene::UiContextMenu{.popup = scene().reference(menu)});
}

void OutputUi::sync(ToolsState& state, EditorUiKit& kit)
{
    // What the log dropped or cleared, then what it received since.
    const std::uint64_t oldest = state.log.oldest();
    if (!entries.empty() && entries.front().sequence < oldest)
    {
        std::erase_if(entries, [oldest](const Entry& entry) { return entry.sequence < oldest; });
        dirty = true;
    }
    state.log.forEachAfter(last, [&](const LogEntry& logged) {
        entries.push_back(makeEntry(logged));
        last = logged.sequence;
        dirty = true;
    });

    // The widths make the list scroll sideways as far as the longest line.
    if (const asset::FontData* const mono = kit.fontData(EditorUiKit::monoFont()))
    {
        for (Entry& entry : entries)
        {
            if (entry.measuredAt == monoSize)
            {
                continue;
            }
            entry.width = 0.0f;
            for (const auto& [begin, end] : entry.lines)
            {
                const std::string_view text = std::string_view(entry.text).substr(begin, end - begin);
                entry.width = std::max(entry.width, ui::measureText(*mono, text, ui::TextStyle{.size = monoSize, .wrap = false}).x);
            }
            entry.measuredAt = monoSize;
            dirty = true;
        }
    }

    const std::array<bool, 4> flags{state.consoleShowDebug, state.consoleShowInfo, state.consoleShowWarnings,
                                    state.consoleShowErrors};
    if (!dirty && flags == shownFlags && state.consoleFilter == shownFilter)
    {
        return;
    }
    dirty = false;
    shownFlags = flags;
    shownFilter = state.consoleFilter;
    shown.clear();
    counts = {};
    contentWidth = 0.0f;
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const Entry& entry = entries[index];
        ++counts[static_cast<std::size_t>(entry.severity)];
        if (!shownFlag(state, entry.severity) || !containsIgnoringCase(entry.text, state.consoleFilter))
        {
            continue;
        }
        for (std::size_t line = 0; line < entry.lines.size(); ++line)
        {
            shown.push_back(Line{.entry = static_cast<std::uint32_t>(index), .line = static_cast<std::uint32_t>(line)});
        }
        contentWidth = std::max(contentWidth, entry.width);
    }
}

void OutputUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    font = state.theme.fontSize;
    monoSize = state.theme.codeFontSize;
    lineHeight = std::round(monoSize * 1.32f) + 2.0f;
    gutter = monoSize * 1.7f;
    if (!built)
    {
        build(state, kit);
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    const float seconds = std::chrono::duration<float>(delta).count();
    clock += static_cast<double>(seconds);

    // The view as the last frame laid it out: whether it showed the end, which new messages follow.
    const ui::LaidOutRect* view = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(scroll);
    scene::UiScroll& scrolled = scene().get<scene::UiScroll>(scroll);
    const float viewHeight = view != nullptr ? view->size().y : 0.0f;
    const auto bottom = [&] { return std::max(static_cast<float>(shown.size()) * lineHeight - viewHeight, 0.0f); };
    const bool atBottom = scrolled.offset.y >= bottom() - lineHeight * 0.5f;
    state.consoleFilter = scene().get<scene::UiText>(filter).text;
    const std::size_t before = shown.size();
    sync(state, kit);
    if (state.consoleAutoScroll && atBottom && shown.size() != before && view != nullptr)
    {
        scrolled.offset.y = bottom();
    }

    // The kinds of message, lit while they are shown.
    struct Kind
    {
        Severity severity;
        const char* name;
        const char* style;
        const char* iconStyle;
    };
    const std::array<Kind, 4> kinds{{
        {Severity::Error, "errors", "error", "icon_error"},
        {Severity::Warning, "warnings", "warning", "icon_warning"},
        {Severity::Info, "messages", "text", "icon"},
        {Severity::Debug, "debug messages", "dim", "icon_dim"},
    }};
    for (std::size_t index = 0; index < kinds.size(); ++index)
    {
        const Kind& kind = kinds[index];
        const bool on = shownFlag(state, kind.severity);
        relabel(kit, toggles[index], std::format("{}", counts[static_cast<std::size_t>(kind.severity)]));
        UiRect& rect = scene().get<UiRect>(toggles[index].entity);
        rect.style = on ? "button" : "flat";
        // The button keeps the width of its count.
        const float width = font * 1.4f + font * 1.1f + font * 0.45f +
                            kit.textWidth(EditorUiKit::regularFont(), scene().get<scene::UiText>(toggles[index].label).text, font);
        rect.offsetMax.x = rect.offsetMin.x + width;
        scene().get<UiRect>(toggles[index].icon).style = on ? kind.iconStyle : "icon_dim";
        scene().get<UiRect>(toggles[index].label).style = on ? kind.style : "dim";
        tooltip(toggles[index].entity, std::format("{} {}", on ? "Hide" : "Show", kind.name));
    }
    scene().get<UiRect>(follow.entity).style = state.consoleAutoScroll ? "button" : "flat";

    // As many rows as the view holds, placed by the list at the lines in view.
    scene::UiVirtualList& virtualList = scene().get<scene::UiVirtualList>(lines);
    virtualList.itemCount = static_cast<std::uint32_t>(shown.size());
    virtualList.itemSize = lineHeight;
    UiRect& linesRect = scene().get<UiRect>(lines);
    linesRect.offsetMax.x = std::max(contentWidth + gutter + font, view != nullptr ? view->size().x - 10.0f : 0.0f);
    scrolled.speed = lineHeight * 3.0f;
    const std::size_t needed = static_cast<std::size_t>(std::ceil(std::max(viewHeight, 200.0f) / lineHeight)) + 2;
    while (rows.size() < needed)
    {
        Row row;
        row.row = add(lines, "Line", fixed({1.0f, lineHeight}));
        row.selection = add(row.row, "Selection",
                            UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}, .visible = false},
                            "selection");
        scene().add<scene::UiImage>(row.selection, scene::UiImage{.raycastTarget = false});
        row.icon = icon(kit, row.row, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}}, Icon::CircleX, "icon_error");
        row.text = add(row.row, "Text", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}}, "text");
        scene().add<scene::UiText>(row.text, scene::UiText{.text = {},
                                                           .font = EditorUiKit::monoFont(),
                                                           .align = scene::TextAlign::Left,
                                                           .verticalAlign = scene::TextVerticalAlign::Middle,
                                                           .wrap = false});
        rows.push_back(row);
    }

    panel.update(kit, delta, UiPanel::zoomFor(font));

    // What was asked of the panel this frame.
    const ui::UiInput& input = panel.input();
    const bool keys = panel.focused() && !world.isEditing();
    const auto selectAll = [&] {
        if (!shown.empty())
        {
            anchor = pointOf(0, 0);
            caret = pointOf(shown.size() - 1, static_cast<std::uint32_t>(lineText(shown.back()).size()));
        }
    };
    const auto copy = [&] {
        if (const std::string copied = selectedText(); !copied.empty())
        {
            ImGui::SetClipboardText(copied.c_str());
        }
    };
    const auto clearAll = [&] {
        state.log.clear();
        entries.clear();
        anchor.reset();
        caret.reset();
        dirty = true;
    };
    for (std::size_t index = 0; index < kinds.size(); ++index)
    {
        if (world.wasClicked(toggles[index].entity))
        {
            bool& on = shownFlag(state, kinds[index].severity);
            on = !on;
        }
    }
    if (world.wasClicked(clear.entity) || world.wasClicked(menuClear.entity))
    {
        clearAll();
    }
    if (world.wasClicked(follow.entity))
    {
        state.consoleAutoScroll = !state.consoleAutoScroll;
        if (state.consoleAutoScroll)
        {
            scrolled.offset.y = bottom();
        }
    }
    if (world.wasClicked(menuCopy.entity) || (keys && input.copyPressed))
    {
        copy();
    }
    if (world.wasClicked(menuSelectAll.entity) || (keys && input.selectAllPressed))
    {
        selectAll();
    }
    if (keys && input.cancelPressed)
    {
        anchor.reset();
        caret.reset();
    }
    if (world.isPopupOpen(scene(), menu))
    {
        enable(menuCopy, selection().has_value());
        enable(menuClear, !entries.empty());
        fitMenu(menu, font * 12.0f);
    }

    // The mouse chooses text: pressed on a line, dragged across others, a word on a double press.
    view = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(scroll);
    if (view != nullptr)
    {
        const float bar = scrolled.scrollbarSize + 2.0f;
        const bool onText = input.pointer.x >= view->min.x && input.pointer.y >= view->min.y &&
                            input.pointer.x < view->max.x - bar && input.pointer.y < view->max.y - bar;
        if (input.pointerPressed && onText && !world.isPopupOpen(scene(), menu))
        {
            if (const std::optional<TextPoint> point = pointAt(kit, input.pointer))
            {
                const bool again = lastPressPoint && clock - lastPress <= doubleClickSeconds &&
                                   lastPressPoint->sequence == point->sequence && lastPressPoint->line == point->line &&
                                   std::abs(static_cast<int>(lastPressPoint->byte) - static_cast<int>(point->byte)) <= 1;
                if (again)
                {
                    // The word under the pointer.
                    const std::optional<Shown> at = shownAt(*point);
                    const std::string_view text = at ? lineText(shown[at->index]) : std::string_view{};
                    std::size_t begin = std::min<std::size_t>(point->byte, text.size());
                    std::size_t end = begin;
                    while (begin > 0 && isWordCharacter(text[begin - 1]))
                    {
                        --begin;
                    }
                    while (end < text.size() && isWordCharacter(text[end]))
                    {
                        ++end;
                    }
                    anchor = TextPoint{.sequence = point->sequence, .line = point->line, .byte = static_cast<std::uint32_t>(begin)};
                    caret = TextPoint{.sequence = point->sequence, .line = point->line, .byte = static_cast<std::uint32_t>(end)};
                    lastPressPoint.reset();
                }
                else
                {
                    if (!input.selecting || !anchor)
                    {
                        anchor = point;
                    }
                    caret = point;
                    selecting = true;
                    lastPress = clock;
                    lastPressPoint = point;
                }
            }
        }
        if (selecting)
        {
            if (!input.pointerDown || input.pointerReleased)
            {
                selecting = false;
            }
            else
            {
                // Past an edge, the list scrolls towards the text still hidden.
                if (input.pointer.y < view->min.y)
                {
                    scrolled.offset.y = std::max(scrolled.offset.y - lineHeight * 30.0f * seconds, 0.0f);
                }
                else if (input.pointer.y > view->max.y)
                {
                    scrolled.offset.y = std::min(scrolled.offset.y + lineHeight * 30.0f * seconds, bottom());
                }
                if (const std::optional<TextPoint> point = pointAt(kit, input.pointer))
                {
                    caret = point;
                }
            }
        }
    }

    // The rows show the lines the list placed them at, and what is chosen of them.
    const std::optional<std::pair<Shown, Shown>> chosen = selection();
    const std::size_t first = virtualList.first;
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const Row& row = rows[index];
        const std::size_t at = first + index;
        if (at >= shown.size())
        {
            continue;
        }
        const Line& line = shown[at];
        const Entry& entry = entries[line.entry];
        const std::string_view text = lineText(line);
        scene::UiText& shownText = scene().get<scene::UiText>(row.text);
        if (shownText.text != text)
        {
            shownText.text = std::string(text);
        }
        shownText.size = monoSize;
        UiRect& textRect = scene().get<UiRect>(row.text);
        textRect.offsetMin.x = gutter;
        const Kind& kind = kinds[3 - static_cast<std::size_t>(entry.severity)];
        textRect.style = kind.style;

        // Warnings and errors carry their mark in the gutter, on their first line.
        const bool marked = line.line == 0 && (entry.severity == Severity::Warning || entry.severity == Severity::Error);
        UiRect& iconRect = scene().get<UiRect>(row.icon);
        iconRect.visible = marked;
        if (marked)
        {
            const float size = monoSize * 0.95f;
            iconRect.offsetMin = {gutter * 0.15f, -size * 0.5f};
            iconRect.offsetMax = {gutter * 0.15f + size, size * 0.5f};
            iconRect.style = kind.iconStyle;
            scene().get<scene::UiImage>(row.icon).texture =
                kit.icon(entry.severity == Severity::Error ? Icon::CircleX : Icon::TriangleAlert);
        }

        UiRect& selected = scene().get<UiRect>(row.selection);
        selected.visible = false;
        if (chosen && at >= chosen->first.index && at <= chosen->second.index)
        {
            const std::size_t begin = at == chosen->first.index ? std::min<std::size_t>(chosen->first.byte, text.size()) : 0;
            const std::size_t end =
                at == chosen->second.index ? std::min<std::size_t>(chosen->second.byte, text.size()) : text.size();
            const float left = caretX(kit, text, begin);
            // A line chosen up to its end shows the line break as a little more.
            const float right = caretX(kit, text, end) + (at < chosen->second.index ? monoSize * 0.5f : 0.0f);
            if (right > left)
            {
                selected.visible = true;
                selected.offsetMin.x = gutter + left;
                selected.offsetMax.x = gutter + right;
            }
        }
    }
}

void drawConsolePanel(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Output");
    // A new layout shows the output rather than the statistics docked with it.
    if (state.selectOutputTabFrames > 0 && --state.selectOutputTabFrames == 0)
    {
        ImGui::SetNextWindowFocus();
    }
    if (!state.uiKit)
    {
        state.uiKit = std::make_shared<EditorUiKit>(state.renderer, state.icons,
                                                    state.platform.baseDirectory() / "resources" / "fonts");
    }
    if (!state.outputUi)
    {
        state.outputUi = std::make_shared<OutputUi>();
    }
    if (ImGui::Begin(consoleWindow, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        state.outputUi->update(state, *state.uiKit, core::Duration(ImGui::GetIO().DeltaTime));
    }
    ImGui::End();
}

void renderOutput(ToolsState& state, render::RenderWorld& world)
{
    if (state.outputUi && state.uiKit)
    {
        state.outputUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
