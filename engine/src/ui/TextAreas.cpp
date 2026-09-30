// The areas of text of the interface world: what each keeps from a frame to the next, the keys and the
// pointer that edit it, and what it lets a tool ask and change.
#include <devex/ui/UiWorld.hpp>

#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/TextArea.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace devex::ui {
namespace {

// How many changes an area goes back on.
constexpr std::size_t maxHistory = 512;
// Seconds between two presses that count as one gesture: a word, then a line.
constexpr double repeatedPress = 0.4;

// The spaces and the tabs a line starts with.
[[nodiscard]] std::string_view indentationOf(std::string_view line) noexcept
{
    std::size_t end = 0;
    while (end < line.size() && (line[end] == ' ' || line[end] == '\t'))
    {
        ++end;
    }
    return line.substr(0, end);
}

// What is written into an area: neither the keys that steer nor the returns of another system.
[[nodiscard]] std::string written(std::string_view added)
{
    std::string kept;
    kept.reserve(added.size());
    for (const char letter : added)
    {
        const auto byte = static_cast<unsigned char>(letter);
        if (byte == '\n' || byte == '\t' || (byte >= 0x20 && byte != 0x7F))
        {
            kept.push_back(letter);
        }
    }
    return kept;
}

void clampScroll(scene::UiTextArea& area, const TextAreaBars& bars) noexcept
{
    area.scroll.x = std::clamp(area.scroll.x, 0.0f, bars.maxScroll.x);
    area.scroll.y = std::clamp(area.scroll.y, 0.0f, bars.maxScroll.y);
}

} // namespace

UiWorld::TextAreaState* UiWorld::areaState(scene::Entity entity) noexcept
{
    const auto found = std::ranges::find(m_areas, entity, &TextAreaState::entity);
    return found != m_areas.end() ? &*found : nullptr;
}

const UiWorld::TextAreaState* UiWorld::areaState(scene::Entity entity) const noexcept
{
    const auto found = std::ranges::find(m_areas, entity, &TextAreaState::entity);
    return found != m_areas.end() ? &*found : nullptr;
}

UiWorld::TextAreaState& UiWorld::ensureAreaState(scene::Entity entity, const std::string& text)
{
    if (TextAreaState* const state = areaState(entity))
    {
        return *state;
    }
    TextAreaState& made = m_areas.emplace_back();
    made.entity = entity;
    made.known = text;
    indexLines(text, made.lineStarts);
    // Measured once the area is laid out.
    made.contentWidth = -1.0f;
    return made;
}

const TextAreaView* UiWorld::textAreaView(scene::Entity entity) const noexcept
{
    const TextAreaState* const state = areaState(entity);
    if (state == nullptr)
    {
        return nullptr;
    }
    const bool focused = entity == m_areaFocus;
    state->view = TextAreaView{.lineStarts = state->lineStarts,
                               .caret = state->caret,
                               .selectionMin = std::min(state->caret, state->anchor),
                               .selectionMax = std::max(state->caret, state->anchor),
                               .focused = focused,
                               // Shown a little longer than it is hidden, which reads better.
                               .caretVisible = focused && std::fmod(m_areaBlink, 1.0f) < 0.6f,
                               .contentWidth = std::max(state->contentWidth, 0.0f),
                               .spans = state->spans,
                               .highlights = state->highlights,
                               .marks = state->marks};
    return &state->view;
}

void UiWorld::indexArea(const scene::Scene& scene, TextAreaState& state, const std::string& text)
{
    indexLines(text, state.lineStarts);
    // The longest line, measured alone: how far the area scrolls across.
    std::size_t longest = 0;
    std::size_t length = 0;
    for (std::size_t line = 0; line < state.lineStarts.size(); ++line)
    {
        const std::size_t end = line + 1 < state.lineStarts.size() ? state.lineStarts[line + 1] - 1 : text.size();
        if (end - state.lineStarts[line] > length)
        {
            length = end - state.lineStarts[line];
            longest = line;
        }
    }
    state.contentWidth = 0.0f;
    const auto* const shown = scene.tryGet<scene::UiText>(state.entity);
    const auto* const area = scene.tryGet<scene::UiTextArea>(state.entity);
    if (shown == nullptr || area == nullptr || !m_fonts || length == 0)
    {
        return;
    }
    const FontRef font = m_fonts(shown->font.isValid() ? shown->font : m_defaultFont);
    if (font.data == nullptr)
    {
        return;
    }
    TextAreaLine placed;
    layoutAreaLine(*font.data, lineText(text, state.lineStarts, longest), shown->size, area->tabSize, math::Vec2{0.0f}, placed);
    state.contentWidth = placed.layout.size.x;
}

void UiWorld::syncArea(TextAreaState& state, std::string& text)
{
    if (text == state.known)
    {
        return;
    }
    // What a tool or a script wrote between two frames is one change, from the first byte that
    // differs to the last one.
    const std::string& before = state.known;
    std::size_t prefix = 0;
    const std::size_t shortest = std::min(before.size(), text.size());
    while (prefix < shortest && before[prefix] == text[prefix])
    {
        ++prefix;
    }
    std::size_t suffix = 0;
    while (suffix < shortest - prefix && before[before.size() - 1 - suffix] == text[text.size() - 1 - suffix])
    {
        ++suffix;
    }
    TextChange change{.offset = prefix,
                      .removed = before.substr(prefix, before.size() - prefix - suffix),
                      .inserted = text.substr(prefix, text.size() - prefix - suffix),
                      .caretBefore = state.caret,
                      .anchorBefore = state.anchor};
    state.caret = std::min(state.caret, text.size());
    state.anchor = std::min(state.anchor, text.size());
    change.caretAfter = state.caret;
    state.undo.push_back(std::move(change));
    if (state.undo.size() > maxHistory)
    {
        state.undo.erase(state.undo.begin());
    }
    state.redo.clear();
    state.known = text;
    state.wantedX = -1.0f;
    // Indexed by the caller, which has the scene.
    state.contentWidth = -1.0f;
}

void UiWorld::notifyArea(const scene::Scene& scene, scene::Entity entity)
{
    m_changed.push_back(entity);
    if (const auto* const area = scene.tryGet<scene::UiTextArea>(entity); area != nullptr && !area->action.empty())
    {
        m_changedActions.push_back(area->action);
    }
}

void UiWorld::changeArea(scene::Scene& scene, TextAreaState& state, std::string& text, std::size_t offset, std::size_t removed,
                         std::string_view inserted, bool typing)
{
    offset = std::min(offset, text.size());
    removed = std::min(removed, text.size() - offset);
    if (removed == 0 && inserted.empty())
    {
        return;
    }
    TextChange change{.offset = offset,
                      .removed = text.substr(offset, removed),
                      .inserted = std::string(inserted),
                      .caretBefore = state.caret,
                      .anchorBefore = state.anchor,
                      .caretAfter = offset + inserted.size(),
                      .typing = typing};
    text.replace(offset, removed, inserted);

    // Letters typed one after the other are one change, up to the end of a word.
    TextChange* const last = state.undo.empty() ? nullptr : &state.undo.back();
    const bool joins = typing && last != nullptr && last->typing && change.removed.empty() && !last->inserted.empty() &&
                       last->offset + last->inserted.size() == offset && inserted.find('\n') == std::string_view::npos &&
                       !((last->inserted.back() == ' ' || last->inserted.back() == '\n') && inserted.front() != ' ');
    if (joins)
    {
        last->inserted += inserted;
        last->caretAfter = change.caretAfter;
    }
    else
    {
        state.undo.push_back(std::move(change));
        if (state.undo.size() > maxHistory)
        {
            state.undo.erase(state.undo.begin());
        }
    }
    state.redo.clear();
    state.caret = offset + inserted.size();
    state.anchor = state.caret;
    state.wantedX = -1.0f;
    state.known = text;
    indexArea(scene, state, text);
    notifyArea(scene, state.entity);
}

void UiWorld::applyTextChange(scene::Scene& scene, TextAreaState& state, std::string& text, const TextChange& change, bool forward)
{
    const std::size_t offset = std::min(change.offset, text.size());
    if (forward)
    {
        text.replace(offset, std::min(change.removed.size(), text.size() - offset), change.inserted);
        state.caret = std::min(change.caretAfter, text.size());
        state.anchor = state.caret;
    }
    else
    {
        text.replace(offset, std::min(change.inserted.size(), text.size() - offset), change.removed);
        state.caret = std::min(change.caretBefore, text.size());
        state.anchor = std::min(change.anchorBefore, text.size());
    }
    state.wantedX = -1.0f;
    state.known = text;
    indexArea(scene, state, text);
    notifyArea(scene, state.entity);
}

bool UiWorld::areaPlace(const scene::Scene& scene, scene::Entity entity, const asset::FontData*& font, TextAreaMetrics& metrics,
                        const LaidOutRect*& rect, float& scale) const
{
    const CanvasLayout* const canvas = canvasOf(entity);
    rect = canvas != nullptr ? canvas->layout.find(entity) : nullptr;
    const auto* const shown = scene.tryGet<scene::UiText>(entity);
    const auto* const area = scene.tryGet<scene::UiTextArea>(entity);
    if (rect == nullptr || shown == nullptr || area == nullptr || !m_fonts)
    {
        return false;
    }
    font = m_fonts(shown->font.isValid() ? shown->font : m_defaultFont).data;
    if (font == nullptr)
    {
        return false;
    }
    const TextAreaState* const state = areaState(entity);
    metrics = textAreaMetrics(*font, *shown, *area, *rect, state != nullptr ? state->lineStarts.size() : 1);
    scale = canvas->layout.scale > 0.0f ? canvas->layout.scale : 1.0f;
    return true;
}

std::size_t UiWorld::areaOffsetAt(const scene::Scene& scene, const TextAreaState& state, math::Vec2 point) const
{
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    if (!areaPlace(scene, state.entity, font, metrics, rect, scale) || state.lineStarts.empty())
    {
        return state.caret;
    }
    const auto& shown = scene.get<scene::UiText>(state.entity);
    const auto& area = scene.get<scene::UiTextArea>(state.entity);
    const float down = (point.y - metrics.origin.y + area.scroll.y) / metrics.lineHeight;
    const auto line = static_cast<std::size_t>(std::clamp(std::floor(down), 0.0f, static_cast<float>(state.lineStarts.size() - 1)));
    TextAreaLine placed;
    layoutAreaLine(*font, lineText(shown.text, state.lineStarts, line), shown.size, area.tabSize, math::Vec2{metrics.origin.x - area.scroll.x, 0.0f},
                   placed);
    return state.lineStarts[line] + placed.offsetAt(point.x);
}

void UiWorld::revealCaret(scene::Scene& scene, TextAreaState& state)
{
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    if (!areaPlace(scene, state.entity, font, metrics, rect, scale) || state.lineStarts.empty())
    {
        return;
    }
    const auto& shown = scene.get<scene::UiText>(state.entity);
    auto& area = scene.get<scene::UiTextArea>(state.entity);
    const math::Vec2 view = metrics.viewSize();
    const std::size_t line = lineOfOffset(state.lineStarts, state.caret);
    const float top = static_cast<float>(line) * metrics.lineHeight;
    if (top < area.scroll.y)
    {
        area.scroll.y = top;
    }
    else if (top + metrics.lineHeight > area.scroll.y + view.y)
    {
        area.scroll.y = top + metrics.lineHeight - view.y;
    }
    TextAreaLine placed;
    layoutAreaLine(*font, lineText(shown.text, state.lineStarts, line), shown.size, area.tabSize, math::Vec2{0.0f}, placed);
    const float across = placed.xOf(state.caret - state.lineStarts[line]);
    // A few letters of room ahead of the cursor, so that it never rides the edge.
    const float room = std::min(shown.size * 2.0f, view.x * 0.25f);
    if (across < area.scroll.x + room * 0.5f)
    {
        area.scroll.x = across - room;
    }
    else if (across > area.scroll.x + view.x - room * 0.5f)
    {
        area.scroll.x = across - view.x + room;
    }
    if (state.contentWidth < 0.0f)
    {
        indexArea(scene, state, shown.text);
    }
    clampScroll(area, textAreaBars(metrics, shown, area, *rect, std::max(state.contentWidth, across)));
}

bool UiWorld::scrollTextArea(scene::Scene& scene, const UiInput& input)
{
    if (input.wheel == 0.0f)
    {
        return false;
    }
    const std::optional<std::pair<std::size_t, std::size_t>> under = hit(scene, input.pointer, false);
    if (!under)
    {
        return false;
    }
    const scene::Entity entity = m_canvases[under->first].layout.rects[under->second].entity;
    auto* const area = scene.tryGet<scene::UiTextArea>(entity);
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    if (area == nullptr || !areaPlace(scene, entity, font, metrics, rect, scale))
    {
        return false;
    }
    const TextAreaState* const state = areaState(entity);
    area->scroll.y -= input.wheel * metrics.lineHeight * 3.0f;
    clampScroll(*area, textAreaBars(metrics, scene.get<scene::UiText>(entity), *area, *rect, state != nullptr ? std::max(state->contentWidth, 0.0f) : 0.0f));
    return true;
}

void UiWorld::updateTextAreas(scene::Scene& scene, const UiInput& input, core::Duration delta)
{
    // What each area keeps: made for the new ones, read again for the texts that changed outside,
    // and dropped with the areas that are gone.
    for (TextAreaState& state : m_areas)
    {
        state.seen = false;
    }
    for (auto [entity, shown, area] : scene.view<scene::UiText, scene::UiTextArea>())
    {
        TextAreaState& state = ensureAreaState(entity, shown.text);
        state.seen = true;
        syncArea(state, shown.text);
        if (state.contentWidth < 0.0f)
        {
            indexArea(scene, state, shown.text);
        }
        static_cast<void>(area);
    }
    std::erase_if(m_areas, [](const TextAreaState& state) { return !state.seen; });
    if (m_areaFocus.isValid())
    {
        const auto* const area = scene.isAlive(m_areaFocus) ? scene.tryGet<scene::UiTextArea>(m_areaFocus) : nullptr;
        if (area == nullptr || !area->interactable || areaState(m_areaFocus) == nullptr)
        {
            m_areaFocus = scene::Entity{};
        }
    }
    if (m_areas.empty())
    {
        return;
    }

    const auto place = [&](TextAreaState& state, const asset::FontData*& font, TextAreaMetrics& metrics, const LaidOutRect*& rect, math::Vec2& point) {
        float scale = 1.0f;
        if (!areaPlace(scene, state.entity, font, metrics, rect, scale))
        {
            return false;
        }
        point = math::Vec2{input.pointer.x / scale, input.pointer.y / scale};
        return true;
    };

    if (input.pointerPressed)
    {
        const std::optional<std::pair<std::size_t, std::size_t>> under = hit(scene, input.pointer, false);
        const scene::Entity pressed = under ? m_canvases[under->first].layout.rects[under->second].entity : scene::Entity{};
        const auto* const pressedArea = pressed.isValid() ? scene.tryGet<scene::UiTextArea>(pressed) : nullptr;
        TextAreaState* const state = pressedArea != nullptr && pressedArea->interactable ? areaState(pressed) : nullptr;
        m_areaFocus = state != nullptr ? pressed : scene::Entity{};
        m_areaSelecting = false;
        m_areaBar = AreaBar::None;
        const asset::FontData* font = nullptr;
        TextAreaMetrics metrics;
        const LaidOutRect* rect = nullptr;
        math::Vec2 point{0.0f};
        if (state != nullptr && place(*state, font, metrics, rect, point))
        {
            m_editing = EditingField{};
            m_areaBlink = 0.0f;
            const auto& shown = scene.get<scene::UiText>(pressed);
            auto& area = scene.get<scene::UiTextArea>(pressed);
            const TextAreaBars bars = textAreaBars(metrics, shown, area, *rect, std::max(state->contentWidth, 0.0f));
            const float bar = std::max(area.scrollbarSize, 0.0f);
            if (bars.vertical() && point.x >= rect->max.x - bar)
            {
                // On its thumb, the bar is dragged from where it was taken; beside it, the thumb
                // comes under the pointer.
                m_areaBar = AreaBar::Vertical;
                const bool onThumb = point.y >= bars.verticalMin.y && point.y <= bars.verticalMax.y;
                m_areaGrab = onThumb ? point.y - bars.verticalMin.y : (bars.verticalMax.y - bars.verticalMin.y) * 0.5f;
            }
            else if (bars.horizontal() && point.y >= rect->max.y - bar)
            {
                m_areaBar = AreaBar::Horizontal;
                const bool onThumb = point.x >= bars.horizontalMin.x && point.x <= bars.horizontalMax.x;
                m_areaGrab = onThumb ? point.x - bars.horizontalMin.x : (bars.horizontalMax.x - bars.horizontalMin.x) * 0.5f;
            }
            else
            {
                const std::size_t offset = areaOffsetAt(scene, *state, point);
                // A second press on the same place takes the word, a third one the line.
                m_areaPresses = m_clock - m_areaPressTime < repeatedPress && offset == m_areaPressOffset ? m_areaPresses + 1 : 1;
                m_areaPressTime = m_clock;
                m_areaPressOffset = offset;
                if (m_areaPresses == 2)
                {
                    const auto [begin, end] = wordAround(shown.text, offset);
                    state->anchor = begin;
                    state->caret = end;
                }
                else if (m_areaPresses >= 3)
                {
                    const std::size_t line = lineOfOffset(state->lineStarts, offset);
                    state->anchor = state->lineStarts[line];
                    state->caret = line + 1 < state->lineStarts.size() ? state->lineStarts[line + 1] : shown.text.size();
                }
                else
                {
                    state->caret = offset;
                    if (!input.selecting)
                    {
                        state->anchor = offset;
                    }
                    m_areaSelecting = true;
                }
                state->wantedX = -1.0f;
            }
        }
    }
    if (input.pointerReleased || !input.pointerDown)
    {
        m_areaSelecting = false;
        m_areaBar = AreaBar::None;
    }

    TextAreaState* const focused = m_areaFocus.isValid() ? areaState(m_areaFocus) : nullptr;
    if (focused == nullptr)
    {
        return;
    }
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    math::Vec2 point{0.0f};
    if ((m_areaSelecting || m_areaBar != AreaBar::None) && input.pointerDown && place(*focused, font, metrics, rect, point))
    {
        const auto& shown = scene.get<scene::UiText>(m_areaFocus);
        auto& area = scene.get<scene::UiTextArea>(m_areaFocus);
        const TextAreaBars bars = textAreaBars(metrics, shown, area, *rect, std::max(focused->contentWidth, 0.0f));
        if (m_areaBar == AreaBar::Vertical && bars.travel.y > 0.0f)
        {
            area.scroll.y = (point.y - m_areaGrab - rect->min.y) / bars.travel.y * bars.maxScroll.y;
        }
        else if (m_areaBar == AreaBar::Horizontal && bars.travel.x > 0.0f)
        {
            area.scroll.x = (point.x - m_areaGrab - rect->min.x - metrics.gutter) / bars.travel.x * bars.maxScroll.x;
        }
        else if (m_areaSelecting && !input.pointerPressed)
        {
            // Dragging through the letters takes them in, and past an edge the text follows.
            const std::size_t offset = areaOffsetAt(scene, *focused, point);
            if (offset != focused->caret)
            {
                focused->caret = offset;
                focused->wantedX = -1.0f;
                m_areaBlink = 0.0f;
            }
            revealCaret(scene, *focused);
        }
        clampScroll(area, bars);
    }

    editTextArea(scene, *focused, input);
    m_areaBlink += std::chrono::duration<float>(delta).count();
}

void UiWorld::editTextArea(scene::Scene& scene, TextAreaState& state, const UiInput& input)
{
    auto* const shown = scene.tryGet<scene::UiText>(state.entity);
    auto* const area = scene.tryGet<scene::UiTextArea>(state.entity);
    if (shown == nullptr || area == nullptr)
    {
        return;
    }
    std::string& text = shown->text;
    state.caret = std::min(state.caret, text.size());
    state.anchor = std::min(state.anchor, text.size());
    const bool writable = !area->readOnly;
    const std::size_t caretBefore = state.caret;
    const std::size_t anchorBefore = state.anchor;
    const std::size_t lengthBefore = text.size();
    bool keepColumn = false;

    const auto selectionMin = [&] { return std::min(state.caret, state.anchor); };
    const auto selectionMax = [&] { return std::max(state.caret, state.anchor); };
    const auto hasSelection = [&] { return state.caret != state.anchor; };
    const auto moveTo = [&](std::size_t offset, bool keepAnchor) {
        state.caret = std::min(offset, text.size());
        if (!keepAnchor)
        {
            state.anchor = state.caret;
        }
    };
    const auto replaceSelection = [&](std::string_view inserted, bool typing) {
        const std::size_t from = selectionMin();
        changeArea(scene, state, text, from, selectionMax() - from, inserted, typing);
    };
    const auto lineStartOf = [&](std::size_t offset) { return static_cast<std::size_t>(state.lineStarts[lineOfOffset(state.lineStarts, offset)]); };
    const auto lineEndOf = [&](std::size_t offset) {
        const std::size_t line = lineOfOffset(state.lineStarts, offset);
        return line + 1 < state.lineStarts.size() ? static_cast<std::size_t>(state.lineStarts[line + 1]) - 1 : text.size();
    };
    const FontRef fontRef = m_fonts ? m_fonts(shown->font.isValid() ? shown->font : m_defaultFont) : FontRef{};
    // Where the cursor stands across in a line, and the byte of a line nearest to a place across.
    const auto acrossOf = [&](std::size_t offset) {
        const std::size_t line = lineOfOffset(state.lineStarts, offset);
        if (fontRef.data == nullptr)
        {
            return static_cast<float>(offset - state.lineStarts[line]);
        }
        TextAreaLine placed;
        layoutAreaLine(*fontRef.data, lineText(text, state.lineStarts, line), shown->size, area->tabSize, math::Vec2{0.0f}, placed);
        return placed.xOf(offset - state.lineStarts[line]);
    };
    const auto offsetIn = [&](std::size_t line, float across) {
        const std::string_view content = lineText(text, state.lineStarts, line);
        if (fontRef.data == nullptr)
        {
            return state.lineStarts[line] + std::min(static_cast<std::size_t>(std::max(across, 0.0f)), content.size());
        }
        TextAreaLine placed;
        layoutAreaLine(*fontRef.data, content, shown->size, area->tabSize, math::Vec2{0.0f}, placed);
        return state.lineStarts[line] + placed.offsetAt(across);
    };
    // Up or down by lines, the cursor keeping the place across it started from.
    const auto moveLines = [&](std::ptrdiff_t lines) {
        if (state.wantedX < 0.0f)
        {
            state.wantedX = acrossOf(state.caret);
        }
        const auto line = static_cast<std::ptrdiff_t>(lineOfOffset(state.lineStarts, state.caret));
        const std::ptrdiff_t target = line + lines;
        if (target < 0)
        {
            moveTo(0, input.selecting);
        }
        else if (target >= static_cast<std::ptrdiff_t>(state.lineStarts.size()))
        {
            moveTo(text.size(), input.selecting);
        }
        else
        {
            moveTo(offsetIn(static_cast<std::size_t>(target), state.wantedX), input.selecting);
        }
        keepColumn = true;
    };

    if (writable && input.undoPressed)
    {
        undoText(scene, state.entity);
    }
    if (writable && input.redoPressed)
    {
        redoText(scene, state.entity);
    }
    if (writable && !input.typed.empty())
    {
        if (const std::string added = written(input.typed); !added.empty())
        {
            replaceSelection(added, true);
        }
    }
    if (writable && input.submitPressed)
    {
        // A new line, under the first letter of the one it leaves when the area indents.
        std::string added = "\n";
        if (area->autoIndent)
        {
            const std::size_t from = selectionMin();
            const std::size_t start = lineStartOf(from);
            const std::string_view before = std::string_view(text).substr(start, from - start);
            added += indentationOf(before);
            const std::size_t last = before.find_last_not_of(" \t");
            if (last != std::string_view::npos && area->indentAfter.find(before[last]) != std::string::npos)
            {
                added.append(static_cast<std::size_t>(std::max(area->tabSize, 0)), ' ');
            }
        }
        replaceSelection(added, false);
    }
    if (writable && input.tabPressed && area->tabSize > 0)
    {
        const auto width = static_cast<std::size_t>(area->tabSize);
        const std::size_t from = selectionMin();
        const std::size_t to = selectionMax();
        const bool several = lineOfOffset(state.lineStarts, from) != lineOfOffset(state.lineStarts, to);
        if (!several && !input.selecting)
        {
            // To the next stop, in spaces.
            const std::size_t column = from - lineStartOf(from);
            replaceSelection(std::string(width - column % width, ' '), false);
        }
        else
        {
            // Every line the selection touches moves in, or out with Shift; a line the selection only
            // reaches the start of is left alone.
            const std::size_t begin = lineStartOf(from);
            const std::size_t lastTouched = several && to == lineStartOf(to) ? to - 1 : to;
            const std::size_t end = lineEndOf(lastTouched);
            std::string block;
            std::size_t at = begin;
            while (at <= end)
            {
                const std::size_t stop = std::min(lineEndOf(at), end);
                std::string_view line = std::string_view(text).substr(at, stop - at);
                if (input.selecting)
                {
                    std::size_t taken = 0;
                    while (taken < width && taken < line.size() && line[taken] == ' ')
                    {
                        ++taken;
                    }
                    if (taken == 0 && !line.empty() && line.front() == '\t')
                    {
                        taken = 1;
                    }
                    line.remove_prefix(taken);
                }
                else if (!line.empty())
                {
                    block.append(width, ' ');
                }
                block += line;
                if (stop >= end)
                {
                    break;
                }
                block += '\n';
                at = stop + 1;
            }
            if (block != std::string_view(text).substr(begin, end - begin))
            {
                const std::size_t size = block.size();
                changeArea(scene, state, text, begin, end - begin, block, false);
                state.anchor = begin;
                state.caret = begin + size;
            }
        }
    }
    if (writable && input.backspacePressed)
    {
        if (hasSelection())
        {
            replaceSelection({}, false);
        }
        else if (state.caret > 0)
        {
            std::size_t from = input.wordModifier ? previousWord(text, state.caret) : previousOffset(text, state.caret);
            // In the indentation, a press goes back a whole stop.
            const std::size_t start = lineStartOf(state.caret);
            const std::string_view before = std::string_view(text).substr(start, state.caret - start);
            if (!input.wordModifier && area->tabSize > 0 && !before.empty() && before.find_first_not_of(' ') == std::string_view::npos)
            {
                const auto width = static_cast<std::size_t>(area->tabSize);
                from = state.caret - ((before.size() - 1) % width + 1);
            }
            changeArea(scene, state, text, from, state.caret - from, {}, false);
        }
    }
    if (writable && input.deletePressed)
    {
        if (hasSelection())
        {
            replaceSelection({}, false);
        }
        else if (state.caret < text.size())
        {
            std::size_t next = state.caret;
            if (input.wordModifier)
            {
                next = nextWord(text, state.caret);
            }
            else
            {
                static_cast<void>(nextCodepoint(text, next));
            }
            changeArea(scene, state, text, state.caret, next - state.caret, {}, false);
        }
    }

    if (input.leftPressed)
    {
        // Without a modifier the arrows leave a selection by its side rather than walking into it.
        if (hasSelection() && !input.selecting && !input.wordModifier)
        {
            moveTo(selectionMin(), false);
        }
        else
        {
            moveTo(input.wordModifier ? previousWord(text, state.caret) : previousOffset(text, state.caret), input.selecting);
        }
    }
    if (input.rightPressed)
    {
        if (hasSelection() && !input.selecting && !input.wordModifier)
        {
            moveTo(selectionMax(), false);
        }
        else
        {
            std::size_t next = state.caret;
            if (input.wordModifier)
            {
                next = nextWord(text, state.caret);
            }
            else if (next < text.size())
            {
                static_cast<void>(nextCodepoint(text, next));
            }
            moveTo(next, input.selecting);
        }
    }
    if (input.upPressed)
    {
        moveLines(-1);
    }
    if (input.downPressed)
    {
        moveLines(1);
    }
    if (input.pageUpPressed || input.pageDownPressed)
    {
        const asset::FontData* font = nullptr;
        TextAreaMetrics metrics;
        const LaidOutRect* rect = nullptr;
        float scale = 1.0f;
        std::ptrdiff_t page = 10;
        if (areaPlace(scene, state.entity, font, metrics, rect, scale))
        {
            page = std::max<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(metrics.viewSize().y / metrics.lineHeight) - 1, 1);
        }
        moveLines(input.pageUpPressed ? -page : page);
    }
    if (input.homePressed)
    {
        // The first letter of the line, then its very start; with the modifier, the start of the text.
        const std::size_t start = lineStartOf(state.caret);
        const std::size_t letters = start + indentationOf(std::string_view(text).substr(start, lineEndOf(state.caret) - start)).size();
        moveTo(input.wordModifier ? 0 : state.caret == letters ? start : letters, input.selecting);
    }
    if (input.endPressed)
    {
        moveTo(input.wordModifier ? text.size() : lineEndOf(state.caret), input.selecting);
    }
    if (input.selectAllPressed)
    {
        state.anchor = 0;
        state.caret = text.size();
    }

    if ((input.copyPressed || input.cutPressed) && hasSelection())
    {
        m_clipboardRequest = text.substr(selectionMin(), selectionMax() - selectionMin());
        if (input.cutPressed && writable)
        {
            replaceSelection({}, false);
        }
    }
    if (writable && input.pastePressed && !input.clipboard.empty())
    {
        // The returns of another system go: a line ends with one character here.
        std::string pasted;
        pasted.reserve(input.clipboard.size());
        for (const char letter : input.clipboard)
        {
            if (letter != '\r')
            {
                pasted.push_back(letter);
            }
        }
        if (const std::string added = written(pasted); !added.empty())
        {
            replaceSelection(added, false);
        }
    }

    if (state.caret != caretBefore || state.anchor != anchorBefore || text.size() != lengthBefore)
    {
        if (!keepColumn)
        {
            state.wantedX = -1.0f;
        }
        m_areaBlink = 0.0f;
        revealCaret(scene, state);
    }
}

UiWorld::TextAreaState* UiWorld::areaStateOf(const scene::Scene& scene, scene::Entity entity)
{
    if (TextAreaState* const state = areaState(entity))
    {
        return state;
    }
    const auto* const shown = scene.isAlive(entity) && scene.has<scene::UiTextArea>(entity) ? scene.tryGet<scene::UiText>(entity) : nullptr;
    return shown != nullptr ? &ensureAreaState(entity, shown->text) : nullptr;
}

void UiWorld::setTextSpans(const scene::Scene& scene, scene::Entity area, std::vector<TextSpan> spans)
{
    if (TextAreaState* const state = areaStateOf(scene, area))
    {
        state->spans = std::move(spans);
    }
}

void UiWorld::setTextHighlights(const scene::Scene& scene, scene::Entity area, std::vector<TextSpan> highlights)
{
    if (TextAreaState* const state = areaStateOf(scene, area))
    {
        state->highlights = std::move(highlights);
    }
}

void UiWorld::setTextMarks(const scene::Scene& scene, scene::Entity area, std::vector<TextLineMark> marks)
{
    if (TextAreaState* const state = areaStateOf(scene, area))
    {
        state->marks = std::move(marks);
    }
}

std::pair<std::size_t, std::size_t> UiWorld::visibleTextLines(const scene::Scene& scene, scene::Entity area) const
{
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    const TextAreaState* const state = areaState(area);
    if (state == nullptr || !areaPlace(scene, area, font, metrics, rect, scale))
    {
        return {0, 0};
    }
    const float moved = std::max(scene.get<scene::UiTextArea>(area).scroll.y, 0.0f);
    // A hair more, so that a line moved to the very top counts as the first.
    const std::size_t first = std::min(static_cast<std::size_t>((moved + 0.01f) / metrics.lineHeight), state->lineStarts.size() - 1);
    const auto count = static_cast<std::size_t>(std::ceil(metrics.viewSize().y / metrics.lineHeight)) + 1;
    return {first, std::min(count, state->lineStarts.size() - first)};
}

std::pair<std::size_t, std::size_t> UiWorld::textSelection(scene::Entity area) const noexcept
{
    const TextAreaState* const state = areaState(area);
    return state != nullptr ? std::pair{state->caret, state->anchor} : std::pair<std::size_t, std::size_t>{0, 0};
}

void UiWorld::selectText(scene::Scene& scene, scene::Entity area, std::size_t anchor, std::size_t caret)
{
    const auto* const shown = scene.isAlive(area) && scene.has<scene::UiTextArea>(area) ? scene.tryGet<scene::UiText>(area) : nullptr;
    if (shown == nullptr)
    {
        return;
    }
    TextAreaState& state = ensureAreaState(area, shown->text);
    state.anchor = std::min(anchor, shown->text.size());
    state.caret = std::min(caret, shown->text.size());
    state.wantedX = -1.0f;
    m_areaBlink = 0.0f;
    revealCaret(scene, state);
}

void UiWorld::undoText(scene::Scene& scene, scene::Entity area)
{
    TextAreaState* const state = areaState(area);
    auto* const shown = scene.tryGet<scene::UiText>(area);
    if (state == nullptr || shown == nullptr || state->undo.empty())
    {
        return;
    }
    TextChange change = std::move(state->undo.back());
    state->undo.pop_back();
    applyTextChange(scene, *state, shown->text, change, false);
    state->redo.push_back(std::move(change));
    revealCaret(scene, *state);
}

void UiWorld::redoText(scene::Scene& scene, scene::Entity area)
{
    TextAreaState* const state = areaState(area);
    auto* const shown = scene.tryGet<scene::UiText>(area);
    if (state == nullptr || shown == nullptr || state->redo.empty())
    {
        return;
    }
    TextChange change = std::move(state->redo.back());
    state->redo.pop_back();
    applyTextChange(scene, *state, shown->text, change, true);
    state->undo.push_back(std::move(change));
    revealCaret(scene, *state);
}

bool UiWorld::canUndoText(scene::Entity area) const noexcept
{
    const TextAreaState* const state = areaState(area);
    return state != nullptr && !state->undo.empty();
}

bool UiWorld::canRedoText(scene::Entity area) const noexcept
{
    const TextAreaState* const state = areaState(area);
    return state != nullptr && !state->redo.empty();
}

void UiWorld::forgetTextHistory(scene::Scene& scene, scene::Entity area)
{
    const auto* const shown = scene.isAlive(area) && scene.has<scene::UiTextArea>(area) ? scene.tryGet<scene::UiText>(area) : nullptr;
    if (shown == nullptr)
    {
        return;
    }
    TextAreaState& state = ensureAreaState(area, shown->text);
    state.undo.clear();
    state.redo.clear();
    state.known = shown->text;
    state.caret = std::min(state.caret, shown->text.size());
    state.anchor = std::min(state.anchor, shown->text.size());
    state.wantedX = -1.0f;
    indexArea(scene, state, shown->text);
}

std::optional<UiWorld::CaretPlace> UiWorld::textCaretPlace(const scene::Scene& scene, scene::Entity area) const
{
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    const TextAreaState* const state = areaState(area);
    if (state == nullptr || state->lineStarts.empty() || !areaPlace(scene, area, font, metrics, rect, scale))
    {
        return std::nullopt;
    }
    const auto& shown = scene.get<scene::UiText>(area);
    const auto& component = scene.get<scene::UiTextArea>(area);
    const std::size_t line = lineOfOffset(state->lineStarts, state->caret);
    const math::Vec2 origin{metrics.origin.x - component.scroll.x, metrics.origin.y - component.scroll.y + static_cast<float>(line) * metrics.lineHeight};
    TextAreaLine placed;
    layoutAreaLine(*font, lineText(shown.text, state->lineStarts, line), shown.size, component.tabSize, origin, placed);
    return CaretPlace{.position = math::Vec2{placed.xOf(state->caret - state->lineStarts[line]), origin.y}, .height = metrics.lineHeight};
}

std::int32_t UiWorld::textLineAt(const scene::Scene& scene, scene::Entity area, math::Vec2 point) const
{
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    const TextAreaState* const state = areaState(area);
    if (state == nullptr || !areaPlace(scene, area, font, metrics, rect, scale))
    {
        return -1;
    }
    const math::Vec2 placed{point.x / scale, point.y / scale};
    if (!contains(*rect, placed))
    {
        return -1;
    }
    const float down = (placed.y - metrics.origin.y + scene.get<scene::UiTextArea>(area).scroll.y) / metrics.lineHeight;
    return down >= 0.0f && down < static_cast<float>(state->lineStarts.size()) ? static_cast<std::int32_t>(down) : -1;
}

scene::Entity UiWorld::editedTextArea() const noexcept
{
    return m_areaFocus;
}

} // namespace devex::ui
