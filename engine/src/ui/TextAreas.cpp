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

// An offset at the end of a row that is not the last of its line would stand at the start of the next one: before its
// last letter, it stays on its row.
[[nodiscard]] std::size_t insideRow(std::span<const TextAreaRow> rows, std::string_view text, std::size_t row, std::size_t offset) noexcept
{
    if (rows.empty() || row + 1 >= rows.size() || rows[row + 1].line != rows[row].line || offset < rows[row].end ||
        rows[row].end <= rows[row].begin)
    {
        return offset;
    }
    std::size_t before = rows[row].end - 1;
    while (before > rows[row].begin && (static_cast<unsigned char>(text[before]) & 0xC0U) == 0x80U)
    {
        --before;
    }
    return before;
}

[[nodiscard]] std::string_view rowText(std::string_view text, const TextAreaRow& row) noexcept
{
    return text.substr(row.begin, row.end - row.begin);
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
                               .rows = state->rows,
                               .foldEnds = state->foldEnds,
                               .folds = state->folds,
                               .caret = state->caret,
                               .selectionMin = std::min(state->caret, state->anchor),
                               .selectionMax = std::max(state->caret, state->anchor),
                               .focused = focused,
                               // Shown a little longer than it is hidden, which reads better.
                               .caretVisible = focused && std::fmod(m_areaBlink, 1.0f) < 0.6f,
                               .contentWidth = std::max(state->contentWidth, 0.0f),
                               .spans = state->spans,
                               .highlights = state->highlights,
                               .marks = state->marks,
                               .carets = state->carets};
    return &state->view;
}

void UiWorld::indexArea(const scene::Scene& scene, TextAreaState& state, const std::string& text)
{
    indexLines(text, state.lineStarts);
    // The blocks, and the folds of those that still start where they were.
    if (const auto* const folding = scene.tryGet<scene::UiTextArea>(state.entity); folding != nullptr && folding->folding)
    {
        foldAreaLines(text, state.lineStarts, state.foldEnds);
        std::erase_if(state.folds, [&](std::uint32_t line) { return line >= state.foldEnds.size() || state.foldEnds[line] <= line; });
    }
    else
    {
        state.foldEnds.clear();
        state.folds.clear();
    }
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
    const FontRef font = shown != nullptr && m_fonts ? m_fonts(shown->font.isValid() ? shown->font : m_defaultFont) : FontRef{};
    if (area != nullptr && font.data != nullptr && length > 0)
    {
        TextAreaLine placed;
        layoutAreaLine(*font.data, lineText(text, state.lineStarts, longest), shown->size, area->tabSize, math::Vec2{0.0f}, placed);
        state.contentWidth = placed.layout.size.x;
    }
    // The rows go with the lines, even when no line has a letter left.
    wrapArea(scene, state, text);
}

void UiWorld::wrapArea(const scene::Scene& scene, TextAreaState& state, const std::string& text)
{
    state.rows.clear();
    state.wrappedWidth = -1.0f;
    const auto* const area = scene.tryGet<scene::UiTextArea>(state.entity);
    const auto* const shown = scene.tryGet<scene::UiText>(state.entity);
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    if (area != nullptr && shown != nullptr && area->wrap && areaPlace(scene, state.entity, font, metrics, rect, scale))
    {
        const float width = metrics.viewSize().x;
        wrapAreaRows(*font, text, state.lineStarts, shown->size, area->tabSize, width, state.rows);
        state.wrappedWidth = width;
        // Nothing passes its right edge.
        state.contentWidth = 0.0f;
    }
    if (state.folds.empty())
    {
        return;
    }
    // The lines folded away are left out of the rows, made one a line when it does not wrap.
    if (state.rows.empty())
    {
        for (std::size_t line = 0; line < state.lineStarts.size(); ++line)
        {
            state.rows.push_back(areaRow({}, state.lineStarts, text.size(), line));
        }
    }
    std::vector<bool> hidden(state.lineStarts.size(), false);
    for (const std::uint32_t line : state.folds)
    {
        const std::size_t end = line < state.foldEnds.size() ? std::min<std::size_t>(state.foldEnds[line], hidden.size() - 1) : line;
        for (std::size_t below = line + 1; below <= end; ++below)
        {
            hidden[below] = true;
        }
    }
    std::erase_if(state.rows, [&](const TextAreaRow& row) { return row.line < hidden.size() && hidden[row.line]; });
}

void UiWorld::moveFolds(TextAreaState& state, std::size_t offset, std::string_view removed, std::string_view inserted)
{
    if (state.folds.empty() || state.lineStarts.empty())
    {
        return;
    }
    const std::size_t first = lineOfOffset(state.lineStarts, offset);
    const auto gone = static_cast<std::size_t>(std::ranges::count(removed, '\n'));
    const auto added = static_cast<std::size_t>(std::ranges::count(inserted, '\n'));
    const std::size_t last = first + gone;
    // Lines put in at the start of a line push it down whole.
    const bool before = gone == 0 && offset == state.lineStarts[first];
    std::vector<std::uint32_t> kept;
    for (const std::uint32_t line : state.folds)
    {
        const std::size_t end = line < state.foldEnds.size() ? state.foldEnds[line] : line;
        if (last < line || (before && line == first))
        {
            kept.push_back(static_cast<std::uint32_t>(line + added - gone));
        }
        else if (first > end || (first == line && last == line && added == 0))
        {
            // Below the block, or within the line that holds it.
            kept.push_back(line);
        }
    }
    state.folds = std::move(kept);
}

bool UiWorld::showCaret(scene::Scene& scene, TextAreaState& state, bool unfold, bool ahead)
{
    const bool selected = state.caret != state.anchor;
    bool changed = false;
    bool opened = false;
    for (std::size_t guard = 0; guard <= state.folds.size() && !state.folds.empty(); ++guard)
    {
        // The outermost fold that hides the line of the cursor.
        const std::size_t line = lineOfOffset(state.lineStarts, state.caret);
        const auto hiding = std::ranges::find_if(state.folds, [&](std::uint32_t fold) {
            return fold < line && fold < state.foldEnds.size() && line <= state.foldEnds[fold];
        });
        if (hiding == state.folds.end())
        {
            break;
        }
        const std::size_t after = static_cast<std::size_t>(state.foldEnds[*hiding]) + 1;
        if (!unfold && ahead && after < state.lineStarts.size())
        {
            state.caret = state.lineStarts[after];
        }
        else if (!unfold && !ahead)
        {
            // The end of the line that holds the block.
            state.caret = state.lineStarts[*hiding + 1] - 1;
        }
        else
        {
            state.folds.erase(hiding);
            opened = true;
        }
        changed = true;
    }
    if (!selected)
    {
        state.anchor = state.caret;
    }
    if (opened)
    {
        wrapArea(scene, state, scene.get<scene::UiText>(state.entity).text);
    }
    return changed;
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
    moveFolds(state, prefix, change.removed, change.inserted);
    state.caret = std::min(state.caret, text.size());
    state.anchor = std::min(state.anchor, text.size());
    for (TextAreaCaret& other : state.carets)
    {
        other.caret = std::min(other.caret, text.size());
        other.anchor = std::min(other.anchor, text.size());
    }
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
                      .typing = typing,
                      .group = m_textGroup};
    moveFolds(state, offset, change.removed, change.inserted);
    text.replace(offset, removed, inserted);

    // Letters typed one after the other are one change, up to the end of a word.
    TextChange* const last = state.undo.empty() ? nullptr : &state.undo.back();
    const bool joins = typing && last != nullptr && last->typing && last->group == m_textGroup && change.removed.empty() && !last->inserted.empty() &&
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
    moveFolds(state, offset, forward ? change.removed : change.inserted, forward ? change.inserted : change.removed);
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
    // Where it goes back to shows, out of any fold.
    showCaret(scene, state, true, true);
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
    metrics = textAreaMetrics(*font, *shown, *area, *rect, state != nullptr ? state->lineStarts.size() : 1,
                              state != nullptr ? areaRowCount(state->rows, state->lineStarts) : 0);
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
    const std::size_t rows = areaRowCount(state.rows, state.lineStarts);
    const auto index = static_cast<std::size_t>(std::clamp(std::floor(down), 0.0f, static_cast<float>(rows - 1)));
    const TextAreaRow row = areaRow(state.rows, state.lineStarts, shown.text.size(), index);
    TextAreaLine placed;
    layoutAreaLine(*font, rowText(shown.text, row), shown.size, area.tabSize, math::Vec2{metrics.origin.x - area.scroll.x, 0.0f}, placed);
    return insideRow(state.rows, shown.text, index, row.begin + placed.offsetAt(point.x));
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
    const std::size_t index = areaRowOf(state.rows, state.lineStarts, state.caret);
    const TextAreaRow row = areaRow(state.rows, state.lineStarts, shown.text.size(), index);
    const float top = static_cast<float>(index) * metrics.lineHeight;
    if (top < area.scroll.y)
    {
        area.scroll.y = top;
    }
    else if (top + metrics.lineHeight > area.scroll.y + view.y)
    {
        area.scroll.y = top + metrics.lineHeight - view.y;
    }
    TextAreaLine placed;
    layoutAreaLine(*font, rowText(shown.text, row), shown.size, area.tabSize, math::Vec2{0.0f}, placed);
    const float across = placed.xOf(state.caret - row.begin);
    // A few letters of room ahead of the cursor, so that it never rides the edge; an area that wraps does not
    // scroll across.
    const float room = std::min(shown.size * 2.0f, view.x * 0.25f);
    if (area.wrap)
    {
        area.scroll.x = 0.0f;
    }
    else if (across < area.scroll.x + room * 0.5f)
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
    clampScroll(area, textAreaBars(metrics, shown, area, *rect, area.wrap ? 0.0f : std::max(state.contentWidth, across)));
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
    // Three lines a notch, from where the last notches sent the text, gliding there.
    const TextAreaState* const state = areaState(entity);
    const auto going = std::ranges::find_if(m_glides, [&](const Glide& glide) { return glide.entity == entity && glide.shown == area->scroll; });
    const TextAreaBars bars =
        textAreaBars(metrics, scene.get<scene::UiText>(entity), *area, *rect, state != nullptr ? std::max(state->contentWidth, 0.0f) : 0.0f);
    math::Vec2 to = going != m_glides.end() ? going->target : area->scroll;
    to.x = std::clamp(to.x, 0.0f, bars.maxScroll.x);
    to.y = std::clamp(to.y - input.wheel * metrics.lineHeight * 3.0f, 0.0f, bars.maxScroll.y);
    glide(scene, entity, true, to);
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
        // Indexed again for a change from outside, and for folding turned on or off.
        if (state.contentWidth < 0.0f || area.folding == state.foldEnds.empty())
        {
            indexArea(scene, state, shown.text);
        }
        // Rows cut again for another width, and lines again once it stops wrapping.
        if (area.wrap || state.wrappedWidth >= 0.0f)
        {
            const asset::FontData* font = nullptr;
            TextAreaMetrics metrics;
            const LaidOutRect* rect = nullptr;
            float scale = 1.0f;
            if (!area.wrap)
            {
                indexArea(scene, state, shown.text);
            }
            else if (areaPlace(scene, entity, font, metrics, rect, scale) && std::abs(metrics.viewSize().x - state.wrappedWidth) > 0.5f)
            {
                wrapArea(scene, state, shown.text);
            }
        }
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
        m_areaColumn = false;
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
            else if (point.x >= rect->min.x + metrics.gutter - metrics.foldColumn && point.x < rect->min.x + metrics.gutter)
            {
                // On the column of the arrows, the block of the line folds or unfolds; the cursor stays where it is.
                const float down = (point.y - metrics.origin.y + area.scroll.y) / metrics.lineHeight;
                const std::size_t rows = areaRowCount(state->rows, state->lineStarts);
                const auto index = static_cast<std::size_t>(std::clamp(std::floor(down), 0.0f, static_cast<float>(rows - 1)));
                toggleTextFold(scene, pressed, areaRow(state->rows, state->lineStarts, shown.text.size(), index).line);
            }
            else
            {
                const std::size_t offset = areaOffsetAt(scene, *state, point);
                // A second press on the same place takes the word, a third one the line.
                m_areaPresses = m_clock - m_areaPressTime < repeatedPress && offset == m_areaPressOffset ? m_areaPresses + 1 : 1;
                m_areaPressTime = m_clock;
                m_areaPressOffset = offset;
                if (!input.caretModifier)
                {
                    state->carets.clear();
                }
                if (input.caretModifier)
                {
                    // Alt: a cursor more where it is pressed, or one less where one stood; dragged, a column.
                    addCaretAt(*state, offset);
                    const std::size_t rows = areaRowCount(state->rows, state->lineStarts);
                    m_columnRow = static_cast<std::size_t>(
                        std::clamp(std::floor((point.y - metrics.origin.y + area.scroll.y) / metrics.lineHeight), 0.0f, static_cast<float>(rows - 1)));
                    m_columnAcross = point.x - metrics.origin.x + area.scroll.x;
                    m_areaColumn = true;
                    m_areaSelecting = true;
                }
                else if (m_areaPresses == 2)
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
        m_areaColumn = false;
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
        else if (m_areaSelecting && m_areaColumn && !input.pointerPressed)
        {
            selectColumn(scene, *focused, point, metrics, area);
            revealCaret(scene, *focused);
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

    // A cursor moved into a fold goes past it, the way it went.
    const std::size_t before = focused->caret;
    editCarets(scene, *focused, input);
    if (focused->caret != before && showCaret(scene, *focused, false, focused->caret > before))
    {
        revealCaret(scene, *focused);
    }
    m_areaBlink += std::chrono::duration<float>(delta).count();
}

void UiWorld::editCarets(scene::Scene& scene, TextAreaState& state, const UiInput& input)
{
    auto* const shown = scene.tryGet<scene::UiText>(state.entity);
    auto* const area = scene.tryGet<scene::UiTextArea>(state.entity);
    if (shown == nullptr || area == nullptr)
    {
        return;
    }
    std::string& text = shown->text;
    // Escape, everything selected and a step back or forth leave one cursor.
    if (input.cancelPressed || input.selectAllPressed || input.undoPressed || input.redoPressed)
    {
        state.carets.clear();
    }
    if (input.nextOccurrencePressed)
    {
        addNextOccurrence(scene, state);
        return;
    }
    if (input.caretModifier && input.selecting && (input.upPressed || input.downPressed))
    {
        addCaretAcross(scene, state, input.downPressed ? 1 : -1);
        return;
    }
    if (state.carets.empty())
    {
        editTextArea(scene, state, input);
        return;
    }

    const bool writable = !area->readOnly;
    // Every cursor, the first one first, and the order they stand in the text.
    std::vector<TextAreaCaret> all;
    all.push_back(TextAreaCaret{.caret = state.caret, .anchor = state.anchor, .wantedX = state.wantedX});
    all.insert(all.end(), state.carets.begin(), state.carets.end());
    std::vector<std::pair<std::size_t, std::size_t>> was;
    std::vector<std::size_t> order;
    for (std::size_t index = 0; index < all.size(); ++index)
    {
        all[index].caret = std::min(all[index].caret, text.size());
        all[index].anchor = std::min(all[index].anchor, text.size());
        was.emplace_back(all[index].caret, all[index].anchor);
        order.push_back(index);
    }
    const std::size_t lengthBefore = text.size();
    const auto lowOf = [](const TextAreaCaret& caret) { return std::min(caret.caret, caret.anchor); };
    const auto highOf = [](const TextAreaCaret& caret) { return std::max(caret.caret, caret.anchor); };
    std::ranges::sort(order, {}, [&](std::size_t index) { return lowOf(all[index]); });

    // Copied together, the selections are the lines of the clipboard, in the order of the text.
    if ((input.copyPressed || input.cutPressed) && std::ranges::any_of(all, [](const TextAreaCaret& caret) { return caret.caret != caret.anchor; }))
    {
        std::string copied;
        bool first = true;
        for (const std::size_t index : order)
        {
            if (all[index].caret == all[index].anchor)
            {
                continue;
            }
            if (!first)
            {
                copied += '\n';
            }
            first = false;
            copied += text.substr(lowOf(all[index]), highOf(all[index]) - lowOf(all[index]));
        }
        m_clipboardRequest = std::move(copied);
    }
    // A paste deals its lines out when it has as many as there are cursors, and goes whole at each otherwise.
    std::vector<std::string> pieces;
    if (writable && input.pastePressed && !input.clipboard.empty())
    {
        std::string pasted;
        for (const char letter : input.clipboard)
        {
            if (letter != '\r')
            {
                pasted.push_back(letter);
            }
        }
        pasted = written(pasted);
        std::vector<std::string> lines;
        std::size_t start = 0;
        for (std::size_t at = 0; at <= pasted.size(); ++at)
        {
            if (at == pasted.size() || pasted[at] == '\n')
            {
                lines.push_back(pasted.substr(start, at - start));
                start = at + 1;
            }
        }
        if (lines.size() == all.size())
        {
            pieces = std::move(lines);
        }
        else
        {
            pieces.assign(all.size(), pasted);
        }
    }
    UiInput each = input;
    each.undoPressed = false;
    each.redoPressed = false;
    each.copyPressed = false;
    each.cutPressed = false;
    each.pastePressed = false;
    each.selectAllPressed = false;
    each.clipboard.clear();
    const bool cutting = writable && input.cutPressed;

    // From the last in the text to the first: each change leaves the cursors before it where they were, and moves
    // the ones after it by what it wrote or took.
    m_textGroup = ++m_textGroups;
    if (m_textGroup == 0)
    {
        m_textGroup = m_textGroups = 1;
    }
    for (std::size_t step = order.size(); step-- > 0;)
    {
        TextAreaCaret& caret = all[order[step]];
        state.caret = caret.caret;
        state.anchor = caret.anchor;
        state.wantedX = caret.wantedX;
        const std::size_t length = text.size();
        if (cutting && state.caret != state.anchor)
        {
            changeArea(scene, state, text, lowOf(caret), highOf(caret) - lowOf(caret), {}, false);
        }
        if (!pieces.empty())
        {
            const std::size_t from = std::min(state.caret, state.anchor);
            changeArea(scene, state, text, from, std::max(state.caret, state.anchor) - from, pieces[step], false);
        }
        editTextArea(scene, state, each, false);
        caret = TextAreaCaret{.caret = state.caret, .anchor = state.anchor, .wantedX = state.wantedX};
        const auto moved = static_cast<std::ptrdiff_t>(text.size()) - static_cast<std::ptrdiff_t>(length);
        for (std::size_t later = step + 1; later < order.size(); ++later)
        {
            TextAreaCaret& after = all[order[later]];
            after.caret = static_cast<std::size_t>(std::max<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(after.caret) + moved, 0));
            after.anchor = static_cast<std::size_t>(std::max<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(after.anchor) + moved, 0));
        }
    }
    m_textGroup = 0;

    // Cursors that meet become one, the first one kept.
    state.caret = all.front().caret;
    state.anchor = all.front().anchor;
    state.wantedX = all.front().wantedX;
    state.carets.clear();
    const auto meet = [&](const TextAreaCaret& one, const TextAreaCaret& other) {
        return lowOf(one) == lowOf(other) || (lowOf(other) < highOf(one) && lowOf(one) < highOf(other));
    };
    for (std::size_t index = 1; index < all.size(); ++index)
    {
        if (!meet(all.front(), all[index]) && std::ranges::none_of(state.carets, [&](const TextAreaCaret& kept) { return meet(kept, all[index]); }))
        {
            state.carets.push_back(all[index]);
        }
    }
    bool changed = text.size() != lengthBefore || state.carets.size() + 1 != was.size() || was.front() != std::pair{state.caret, state.anchor};
    for (std::size_t index = 0; !changed && index < state.carets.size(); ++index)
    {
        changed = was[index + 1] != std::pair{state.carets[index].caret, state.carets[index].anchor};
    }
    if (changed)
    {
        m_areaBlink = 0.0f;
        revealCaret(scene, state);
    }
}

void UiWorld::addCaretAt(TextAreaState& state, std::size_t offset)
{
    const auto same = std::ranges::find_if(state.carets, [&](const TextAreaCaret& other) { return other.caret == offset && other.anchor == offset; });
    if (same != state.carets.end())
    {
        state.carets.erase(same);
        return;
    }
    if (state.caret == offset && state.anchor == offset)
    {
        return;
    }
    state.carets.push_back(TextAreaCaret{.caret = state.caret, .anchor = state.anchor, .wantedX = state.wantedX});
    state.caret = offset;
    state.anchor = offset;
}

void UiWorld::addCaretAcross(scene::Scene& scene, TextAreaState& state, int direction)
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
    const auto& area = scene.get<scene::UiTextArea>(state.entity);
    // From the cursor furthest that way, at the place across it keeps.
    std::size_t from = std::min(state.caret, shown.text.size());
    float wanted = state.wantedX;
    for (const TextAreaCaret& other : state.carets)
    {
        if (direction > 0 ? other.caret > from : other.caret < from)
        {
            from = std::min(other.caret, shown.text.size());
            wanted = other.wantedX;
        }
    }
    const std::size_t index = areaRowOf(state.rows, state.lineStarts, from);
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 >= areaRowCount(state.rows, state.lineStarts)))
    {
        return;
    }
    if (wanted < 0.0f)
    {
        const TextAreaRow row = areaRow(state.rows, state.lineStarts, shown.text.size(), index);
        TextAreaLine placed;
        layoutAreaLine(*font, rowText(shown.text, row), shown.size, area.tabSize, math::Vec2{0.0f}, placed);
        wanted = placed.xOf(from - row.begin);
    }
    const std::size_t target = direction > 0 ? index + 1 : index - 1;
    const std::size_t offset = areaOffsetAt(
        scene, state,
        math::Vec2{metrics.origin.x - area.scroll.x + wanted, metrics.origin.y - area.scroll.y + (static_cast<float>(target) + 0.5f) * metrics.lineHeight});
    if (offset == state.caret || std::ranges::any_of(state.carets, [&](const TextAreaCaret& other) { return other.caret == offset; }))
    {
        return;
    }
    state.carets.push_back(TextAreaCaret{.caret = state.caret, .anchor = state.anchor, .wantedX = state.wantedX});
    state.caret = offset;
    state.anchor = offset;
    state.wantedX = wanted;
    m_areaBlink = 0.0f;
    revealCaret(scene, state);
}

void UiWorld::addNextOccurrence(scene::Scene& scene, TextAreaState& state)
{
    const std::string& text = scene.get<scene::UiText>(state.entity).text;
    state.caret = std::min(state.caret, text.size());
    state.anchor = std::min(state.anchor, text.size());
    if (state.caret == state.anchor)
    {
        const auto [begin, end] = wordAround(text, state.caret);
        if (begin < end)
        {
            state.anchor = begin;
            state.caret = end;
            state.wantedX = -1.0f;
            m_areaBlink = 0.0f;
            revealCaret(scene, state);
        }
        return;
    }
    const std::size_t low = std::min(state.caret, state.anchor);
    const std::string needle = text.substr(low, std::max(state.caret, state.anchor) - low);
    // After the last selection in the text, and from the start past the end; a place a cursor already holds is left.
    std::size_t after = std::max(state.caret, state.anchor);
    for (const TextAreaCaret& other : state.carets)
    {
        after = std::max({after, other.caret, other.anchor});
    }
    std::size_t found = text.find(needle, after);
    if (found == std::string::npos)
    {
        found = text.find(needle);
    }
    const auto taken = [&](std::size_t at) {
        return at == low || std::ranges::any_of(state.carets, [&](const TextAreaCaret& other) { return std::min(other.caret, other.anchor) == at; });
    };
    if (found == std::string::npos || taken(found))
    {
        return;
    }
    state.carets.push_back(TextAreaCaret{.caret = state.caret, .anchor = state.anchor, .wantedX = state.wantedX});
    state.anchor = found;
    state.caret = found + needle.size();
    state.wantedX = -1.0f;
    m_areaBlink = 0.0f;
    revealCaret(scene, state);
}

void UiWorld::selectColumn(const scene::Scene& scene, TextAreaState& state, math::Vec2 point, const TextAreaMetrics& metrics,
                           const scene::UiTextArea& area)
{
    const std::size_t rows = areaRowCount(state.rows, state.lineStarts);
    const auto row = static_cast<std::size_t>(
        std::clamp(std::floor((point.y - metrics.origin.y + area.scroll.y) / metrics.lineHeight), 0.0f, static_cast<float>(rows - 1)));
    const float across = point.x - metrics.origin.x + area.scroll.x;
    // Still where it was pressed: the cursor the press added stays as it is.
    if (row == m_columnRow && std::abs(across - m_columnAcross) < 2.0f)
    {
        return;
    }
    const auto at = [&](std::size_t index, float x) {
        return areaOffsetAt(scene, state,
                            math::Vec2{metrics.origin.x - area.scroll.x + x, metrics.origin.y - area.scroll.y + (static_cast<float>(index) + 0.5f) * metrics.lineHeight});
    };
    state.carets.clear();
    for (std::size_t index = std::min(row, m_columnRow); index <= std::max(row, m_columnRow); ++index)
    {
        if (index != row)
        {
            state.carets.push_back(TextAreaCaret{.caret = at(index, across), .anchor = at(index, m_columnAcross)});
        }
    }
    state.caret = at(row, across);
    state.anchor = at(row, m_columnAcross);
    state.wantedX = -1.0f;
    m_areaBlink = 0.0f;
}

void UiWorld::editTextArea(scene::Scene& scene, TextAreaState& state, const UiInput& input, bool reveal)
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
    // Where the cursor stands across in a row, and the byte of a row nearest to a place across: the rows of an area
    // that wraps, its lines otherwise.
    const auto acrossOf = [&](std::size_t offset) {
        const TextAreaRow row = areaRow(state.rows, state.lineStarts, text.size(), areaRowOf(state.rows, state.lineStarts, offset));
        if (fontRef.data == nullptr)
        {
            return static_cast<float>(offset - row.begin);
        }
        TextAreaLine placed;
        layoutAreaLine(*fontRef.data, rowText(text, row), shown->size, area->tabSize, math::Vec2{0.0f}, placed);
        return placed.xOf(offset - row.begin);
    };
    const auto offsetIn = [&](std::size_t index, float across) {
        const TextAreaRow row = areaRow(state.rows, state.lineStarts, text.size(), index);
        const std::string_view content = rowText(text, row);
        if (fontRef.data == nullptr)
        {
            return static_cast<std::size_t>(row.begin) + std::min(static_cast<std::size_t>(std::max(across, 0.0f)), content.size());
        }
        TextAreaLine placed;
        layoutAreaLine(*fontRef.data, content, shown->size, area->tabSize, math::Vec2{0.0f}, placed);
        return insideRow(state.rows, text, index, row.begin + placed.offsetAt(across));
    };
    // Up or down by rows, the cursor keeping the place across it started from.
    const auto moveLines = [&](std::ptrdiff_t lines) {
        if (state.wantedX < 0.0f)
        {
            state.wantedX = acrossOf(state.caret);
        }
        const auto line = static_cast<std::ptrdiff_t>(areaRowOf(state.rows, state.lineStarts, state.caret));
        const std::ptrdiff_t target = line + lines;
        if (target < 0)
        {
            moveTo(0, input.selecting);
        }
        else if (target >= static_cast<std::ptrdiff_t>(areaRowCount(state.rows, state.lineStarts)))
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
        if (reveal)
        {
            revealCaret(scene, state);
        }
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
    // A hair more, so that a row moved to the very top counts as the first; the lines of the rows in view.
    const std::size_t rows = areaRowCount(state->rows, state->lineStarts);
    const std::size_t first = std::min(static_cast<std::size_t>((moved + 0.01f) / metrics.lineHeight), rows - 1);
    const auto count = static_cast<std::size_t>(std::ceil(metrics.viewSize().y / metrics.lineHeight)) + 1;
    const std::size_t last = std::min(first + count, rows) - 1;
    const std::size_t size = scene.get<scene::UiText>(area).text.size();
    const std::size_t firstLine = areaRow(state->rows, state->lineStarts, size, first).line;
    return {firstLine, areaRow(state->rows, state->lineStarts, size, last).line - firstLine + 1};
}

std::vector<std::pair<std::size_t, std::size_t>> UiWorld::textCarets(scene::Entity area) const
{
    std::vector<std::pair<std::size_t, std::size_t>> carets;
    if (const TextAreaState* const state = areaState(area))
    {
        carets.emplace_back(state->caret, state->anchor);
        for (const TextAreaCaret& other : state->carets)
        {
            carets.emplace_back(other.caret, other.anchor);
        }
    }
    return carets;
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
    state.carets.clear();
    m_areaBlink = 0.0f;
    // Sent into a fold, it opens it.
    showCaret(scene, state, true, true);
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
    // The changes several cursors made together go back together, to one cursor.
    state->carets.clear();
    const std::uint32_t group = state->undo.back().group;
    do
    {
        TextChange change = std::move(state->undo.back());
        state->undo.pop_back();
        applyTextChange(scene, *state, shown->text, change, false);
        state->redo.push_back(std::move(change));
    } while (group != 0 && !state->undo.empty() && state->undo.back().group == group);
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
    state->carets.clear();
    const std::uint32_t group = state->redo.back().group;
    do
    {
        TextChange change = std::move(state->redo.back());
        state->redo.pop_back();
        applyTextChange(scene, *state, shown->text, change, true);
        state->undo.push_back(std::move(change));
    } while (group != 0 && !state->redo.empty() && state->redo.back().group == group);
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
    const std::size_t index = areaRowOf(state->rows, state->lineStarts, state->caret);
    const TextAreaRow row = areaRow(state->rows, state->lineStarts, shown.text.size(), index);
    const math::Vec2 origin{metrics.origin.x - component.scroll.x, metrics.origin.y - component.scroll.y + static_cast<float>(index) * metrics.lineHeight};
    TextAreaLine placed;
    layoutAreaLine(*font, rowText(shown.text, row), shown.size, component.tabSize, origin, placed);
    return CaretPlace{.position = math::Vec2{placed.xOf(state->caret - row.begin), origin.y}, .height = metrics.lineHeight};
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
    if (down < 0.0f || down >= static_cast<float>(areaRowCount(state->rows, state->lineStarts)))
    {
        return -1;
    }
    return static_cast<std::int32_t>(areaRow(state->rows, state->lineStarts, scene.get<scene::UiText>(area).text.size(), static_cast<std::size_t>(down)).line);
}

bool UiWorld::toggleTextFold(scene::Scene& scene, scene::Entity area, std::size_t line)
{
    TextAreaState* const state = areaStateOf(scene, area);
    if (state == nullptr)
    {
        return false;
    }
    const std::string& text = scene.get<scene::UiText>(area).text;
    if (state->foldEnds.empty())
    {
        indexArea(scene, *state, text);
    }
    if (line >= state->foldEnds.size())
    {
        return false;
    }
    const auto found = std::ranges::lower_bound(state->folds, static_cast<std::uint32_t>(line));
    bool moved = false;
    if (found != state->folds.end() && *found == line)
    {
        state->folds.erase(found);
    }
    else if (state->foldEnds[line] > line)
    {
        state->folds.insert(found, static_cast<std::uint32_t>(line));
        moved = showCaret(scene, *state, false, false);
    }
    else
    {
        return false;
    }
    wrapArea(scene, *state, text);
    // The view moves only for a cursor that had to.
    if (moved)
    {
        revealCaret(scene, *state);
    }
    return true;
}

void UiWorld::foldAllText(scene::Scene& scene, scene::Entity area, bool folded)
{
    TextAreaState* const state = areaStateOf(scene, area);
    if (state == nullptr)
    {
        return;
    }
    const std::string& text = scene.get<scene::UiText>(area).text;
    if (state->foldEnds.empty())
    {
        indexArea(scene, *state, text);
    }
    state->folds.clear();
    for (std::size_t line = 0; folded && line < state->foldEnds.size(); ++line)
    {
        if (state->foldEnds[line] > line)
        {
            state->folds.push_back(static_cast<std::uint32_t>(line));
        }
    }
    showCaret(scene, *state, false, false);
    wrapArea(scene, *state, text);
    revealCaret(scene, *state);
}

std::vector<std::size_t> UiWorld::foldedTextLines(scene::Entity area) const
{
    const TextAreaState* const state = areaState(area);
    return state != nullptr ? std::vector<std::size_t>(state->folds.begin(), state->folds.end()) : std::vector<std::size_t>{};
}

std::int32_t UiWorld::textBlockAt(scene::Entity area, std::size_t line) const noexcept
{
    const TextAreaState* const state = areaState(area);
    if (state == nullptr || line >= state->foldEnds.size())
    {
        return -1;
    }
    for (std::size_t above = line + 1; above-- > 0;)
    {
        if (state->foldEnds[above] > above && state->foldEnds[above] >= line)
        {
            return static_cast<std::int32_t>(above);
        }
    }
    return -1;
}

scene::Entity UiWorld::editedTextArea() const noexcept
{
    return m_areaFocus;
}

std::optional<std::size_t> UiWorld::textOffsetAt(const scene::Scene& scene, scene::Entity area,
                                                 math::Vec2 point) const
{
    if (textLineAt(scene, area, point) < 0)
        return std::nullopt;
    const auto* state = areaState(area);
    const asset::FontData* font = nullptr;
    TextAreaMetrics metrics;
    const LaidOutRect* rect = nullptr;
    float scale = 1.0f;
    if (state == nullptr || !areaPlace(scene, area, font, metrics, rect, scale))
        return std::nullopt;
    return areaOffsetAt(scene, *state, point / scale);
}

} // namespace devex::ui
