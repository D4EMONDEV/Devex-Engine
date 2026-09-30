// What the interface world does for the controls that stand over the others or that the pointer
// drags: popups and context menus, modals, dropdowns, tooltips, scrollbars, splitters, foldouts,
// the headers of tables, virtual lists, and what is dragged and dropped.
#include <devex/ui/UiWorld.hpp>

#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/Color.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <string>

namespace devex::ui {
namespace {

// Double clicks are two clicks on the same button, closer than this.
constexpr double doubleClickSeconds = 0.4;
// The pointer takes the edge of a column this close to it, in units.
constexpr float columnGrip = 5.0f;
// A column is never narrower than this, in units.
constexpr float columnMinimum = 24.0f;
// Options an open dropdown shows at once; the wheel reaches the others.
constexpr std::size_t dropdownRows = 10;
// How far the pointer moves from where it pressed a drag source before it takes it away, in
// pixels, so that a click that shakes a little stays a click.
constexpr float carryDistance = 6.0f;
// The same for a number field, which the pointer drags sideways.
constexpr float numberDragDistance = 3.0f;

[[nodiscard]] bool isMenu(const scene::Scene& scene, scene::Entity entity) noexcept
{
    const scene::UiPopup* const popup = scene.tryGet<scene::UiPopup>(entity);
    return popup != nullptr && popup->kind == scene::UiPopupKind::Menu;
}

[[nodiscard]] bool isOpen(const scene::Scene& scene, scene::Entity entity) noexcept
{
    const scene::UiRect* const rect = scene.tryGet<scene::UiRect>(entity);
    return rect != nullptr && rect->visible;
}

// The header cell an element belongs to, and its table: the element itself or one above it whose
// parent is the header row of a table.
struct HeaderCell
{
    scene::Entity cell;
    scene::Entity table;
    std::size_t column = 0;
};

[[nodiscard]] std::optional<HeaderCell> headerCellOf(const scene::Scene& scene, scene::Entity entity)
{
    for (scene::Entity cell = entity; cell.isValid(); cell = scene.parent(cell))
    {
        const scene::Entity row = scene.parent(cell);
        const scene::UiTableRow* const header = row.isValid() ? scene.tryGet<scene::UiTableRow>(row) : nullptr;
        if (header == nullptr || !header->header)
        {
            continue;
        }
        for (scene::Entity above = scene.parent(row); above.isValid(); above = scene.parent(above))
        {
            if (scene.has<scene::UiTable>(above))
            {
                std::size_t column = 0;
                for (scene::Entity sibling = scene.firstChild(row); sibling.isValid() && sibling != cell;
                     sibling = scene.nextSibling(sibling))
                {
                    column += scene.has<scene::UiRect>(sibling) ? 1 : 0;
                }
                return HeaderCell{.cell = cell, .table = above, .column = column};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// Whether the pointer stops on the element, as the hover of UiWorld counts it.
[[nodiscard]] bool stopsPointer(const scene::Scene& scene, scene::Entity entity) noexcept
{
    if (scene.has<scene::UiButton>(entity) || scene.has<scene::UiInput>(entity) ||
        scene.has<scene::UiSlider>(entity) || scene.has<scene::UiToggle>(entity) ||
        scene.has<scene::UiDropdown>(entity) || scene.has<scene::UiFoldout>(entity) ||
        scene.has<scene::UiDragSource>(entity) || scene.has<scene::UiDropTarget>(entity) ||
        scene.has<scene::UiNumberField>(entity) || scene.has<scene::UiColorPicker>(entity))
    {
        return true;
    }
    if (const scene::UiImage* const image = scene.tryGet<scene::UiImage>(entity))
    {
        return image->raycastTarget;
    }
    if (const scene::UiText* const text = scene.tryGet<scene::UiText>(entity))
    {
        return text->raycastTarget;
    }
    return false;
}

// The text of the first element at or under an element that has one, in the order they are drawn.
[[nodiscard]] std::string textBelow(const scene::Scene& scene, scene::Entity entity)
{
    if (const scene::UiText* const text = scene.tryGet<scene::UiText>(entity); text != nullptr && !text->text.empty())
    {
        return text->text;
    }
    for (scene::Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        if (std::string found = textBelow(scene, child); !found.empty())
        {
            return found;
        }
    }
    return {};
}

// The thumb of a vertical or horizontal scrollbar: where it starts and how long it is, in units.
[[nodiscard]] std::pair<float, float> thumbOf(const LaidOutRect& rect, const scene::UiScroll& scroll, int axis)
{
    const float size = axis == 1 ? rect.size().y : rect.size().x;
    const float content = axis == 1 ? rect.content.y : rect.content.x;
    const float offset = axis == 1 ? scroll.offset.y : scroll.offset.x;
    const float thumb = std::max(size * size / std::max(content, 1.0f), std::max(scroll.scrollbarSize, 1.0f) * 2.0f);
    const float room = std::max(content - size, 1.0f);
    const float start = (axis == 1 ? rect.min.y : rect.min.x) + (size - thumb) * std::clamp(offset / room, 0.0f, 1.0f);
    return {start, thumb};
}


// The text a number field writes around its value: what comes before {} and after it.
struct FormatParts
{
    std::string_view before;
    std::string_view after;
    bool hasValue = false;
};

[[nodiscard]] FormatParts splitFormat(std::string_view format) noexcept
{
    const std::size_t at = format.find("{}");
    if (at == std::string_view::npos)
    {
        return FormatParts{.before = format};
    }
    return FormatParts{.before = format.substr(0, at), .after = format.substr(at + 2), .hasValue = true};
}

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.remove_suffix(1);
    }
    return text;
}

// The value a number field keeps: on its steps, between its bounds.
[[nodiscard]] double fitNumber(const scene::UiNumberField& field, double value) noexcept
{
    const double low = std::min(field.minValue, field.maxValue);
    const double high = std::max(field.minValue, field.maxValue);
    if (field.step > 0.0f)
    {
        value = low + std::round((value - low) / field.step) * field.step;
    }
    return low < high ? std::clamp(value, low, high) : value;
}

// Reads a sum of numbers: + and - between products, * and / between factors, a sign before a
// factor, and brackets.
class Expression
{
public:
    explicit Expression(std::string_view text) noexcept
        : m_text(text)
    {
    }

    [[nodiscard]] std::optional<double> read()
    {
        std::optional<double> value = sum();
        skipSpaces();
        if (!value || m_at != m_text.size() || !std::isfinite(*value))
        {
            return std::nullopt;
        }
        return value;
    }

private:
    void skipSpaces() noexcept
    {
        while (m_at < m_text.size() && (m_text[m_at] == ' ' || m_text[m_at] == '\t'))
        {
            ++m_at;
        }
    }

    [[nodiscard]] bool take(char wanted) noexcept
    {
        skipSpaces();
        if (m_at < m_text.size() && m_text[m_at] == wanted)
        {
            ++m_at;
            return true;
        }
        return false;
    }

    [[nodiscard]] std::optional<double> sum()
    {
        std::optional<double> left = product();
        while (left)
        {
            if (take('+'))
            {
                const std::optional<double> right = product();
                left = right ? std::optional(*left + *right) : std::nullopt;
            }
            else if (take('-'))
            {
                const std::optional<double> right = product();
                left = right ? std::optional(*left - *right) : std::nullopt;
            }
            else
            {
                break;
            }
        }
        return left;
    }

    [[nodiscard]] std::optional<double> product()
    {
        std::optional<double> left = factor();
        while (left)
        {
            if (take('*'))
            {
                const std::optional<double> right = factor();
                left = right ? std::optional(*left * *right) : std::nullopt;
            }
            else if (take('/'))
            {
                const std::optional<double> right = factor();
                left = right && *right != 0.0 ? std::optional(*left / *right) : std::nullopt;
            }
            else
            {
                break;
            }
        }
        return left;
    }

    [[nodiscard]] std::optional<double> factor()
    {
        // Brackets inside brackets are read by recursion, which a limit keeps from running away.
        if (m_depth >= 64)
        {
            return std::nullopt;
        }
        ++m_depth;
        std::optional<double> value;
        if (take('-'))
        {
            const std::optional<double> inner = factor();
            value = inner ? std::optional(-*inner) : std::nullopt;
        }
        else if (take('+'))
        {
            value = factor();
        }
        else if (take('('))
        {
            value = sum();
            if (!take(')'))
            {
                value.reset();
            }
        }
        else
        {
            value = number();
        }
        --m_depth;
        return value;
    }

    [[nodiscard]] std::optional<double> number()
    {
        skipSpaces();
        std::string digits;
        while (m_at < m_text.size())
        {
            const char letter = m_text[m_at];
            const bool exponentSign = (letter == '-' || letter == '+') && !digits.empty() &&
                                      (digits.back() == 'e' || digits.back() == 'E');
            if ((letter >= '0' && letter <= '9') || letter == '.' || letter == ',' || letter == 'e' || letter == 'E' ||
                exponentSign)
            {
                // A comma is the point of the numbers of many languages.
                digits.push_back(letter == ',' ? '.' : letter);
                ++m_at;
                continue;
            }
            break;
        }
        double value = 0.0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size())
        {
            return std::nullopt;
        }
        return value;
    }

    std::string_view m_text;
    std::size_t m_at = 0;
    int m_depth = 0;
};

} // namespace

void UiWorld::layoutCanvases(scene::Scene& scene)
{
    // Popups closed by other means forget where they were opened.
    std::erase_if(m_popupPlacements, [&scene](const PopupPlacement& placement) {
        return !scene.isAlive(placement.popup) || !isOpen(scene, placement.popup);
    });

    // The canvases of the scene, laid out in the order they are drawn.
    std::vector<CanvasLayout> previous = std::move(m_canvases);
    m_canvases.clear();
    std::vector<PopupPlacement> placements;
    for (auto [entity, canvas] : scene.view<scene::Canvas>())
    {
        if (!canvas.visible)
        {
            continue;
        }
        CanvasLayout state{.entity = entity, .sortOrder = canvas.sortOrder, .interactive = canvas.interactive};
        // The room a canvas laid out last frame is reused rather than allocated again.
        if (const auto found = std::ranges::find(previous, entity, &CanvasLayout::entity); found != previous.end())
        {
            state.layout = std::move(found->layout);
        }
        placements.clear();
        for (const PopupPlacement& placement : m_popupPlacements)
        {
            if (canvasEntityOf(scene, placement.popup) == entity)
            {
                placements.push_back(placement);
            }
        }
        layoutCanvas(scene, entity, m_windowSize, state.layout, placements);
        m_canvases.push_back(std::move(state));
    }
    std::ranges::stable_sort(m_canvases, {}, &CanvasLayout::sortOrder);
}

void UiWorld::findModal(const scene::Scene& scene)
{
    m_modal.reset();
    // The modal of the topmost canvas, the one opened last in it.
    for (std::size_t canvas = m_canvases.size(); canvas-- > 0;)
    {
        const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
        for (std::size_t index = rects.size(); index-- > 0;)
        {
            const scene::UiPopup* const popup = scene.tryGet<scene::UiPopup>(rects[index].entity);
            if (rects[index].visible && popup != nullptr && popup->kind == scene::UiPopupKind::Modal)
            {
                m_modal = ModalRange{.canvas = canvas, .begin = index, .end = subtreeEnd(rects, index)};
                return;
            }
        }
    }
}

bool UiWorld::reachable(std::size_t canvas, std::size_t rect) const noexcept
{
    return !m_modal || (canvas == m_modal->canvas && rect >= m_modal->begin && rect < m_modal->end);
}

math::Vec2 UiWorld::toCanvas(std::size_t canvas, math::Vec2 pointer) const noexcept
{
    const float scale = std::max(m_canvases[canvas].layout.scale, 0.0001f);
    return math::Vec2{pointer.x / scale, pointer.y / scale};
}

scene::Entity UiWorld::canvasEntityOf(const scene::Scene& scene, scene::Entity entity) const
{
    for (scene::Entity current = entity; current.isValid(); current = scene.parent(current))
    {
        if (scene.has<scene::Canvas>(current))
        {
            return current;
        }
    }
    return scene::Entity{};
}

std::optional<std::pair<std::size_t, std::size_t>> UiWorld::hit(const scene::Scene& scene, math::Vec2 pointer,
                                                                bool anyElement) const
{
    for (std::size_t canvas = m_canvases.size(); canvas-- > 0;)
    {
        if (!m_canvases[canvas].interactive || m_canvases[canvas].layout.scale <= 0.0f)
        {
            continue;
        }
        const math::Vec2 point = toCanvas(canvas, pointer);
        const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
        for (std::size_t index = rects.size(); index-- > 0;)
        {
            const LaidOutRect& rect = rects[index];
            if (!rect.visible || rect.opacity <= 0.0f || !reachable(canvas, index) || !contains(rect, point))
            {
                continue;
            }
            if (anyElement || stopsPointer(scene, rect.entity))
            {
                return std::pair{canvas, index};
            }
        }
    }
    return std::nullopt;
}

void UiWorld::openPopup(scene::Scene& scene, scene::Entity popup, std::optional<math::Vec2> at)
{
    scene::UiRect* const rect = scene.tryGet<scene::UiRect>(popup);
    if (rect == nullptr || !scene.has<scene::UiPopup>(popup))
    {
        return;
    }
    rect->visible = true;
    std::erase_if(m_popupPlacements, [popup](const PopupPlacement& placement) { return placement.popup == popup; });
    if (at)
    {
        // The point in the units of the canvas of the popup, which scales as it was laid out last.
        const scene::Entity canvasEntity = canvasEntityOf(scene, popup);
        float scale = 1.0f;
        if (const auto found = std::ranges::find(m_canvases, canvasEntity, &CanvasLayout::entity); found != m_canvases.end())
        {
            scale = std::max(found->layout.scale, 0.0001f);
        }
        else if (const scene::Canvas* const canvas = scene.tryGet<scene::Canvas>(canvasEntity))
        {
            scale = std::max(canvasScale(*canvas, m_windowSize), 0.0001f);
        }
        m_popupPlacements.push_back(PopupPlacement{.popup = popup, .point = *at / scale});
    }
}

void UiWorld::closePopup(scene::Scene& scene, scene::Entity popup)
{
    if (scene::UiRect* const rect = scene.tryGet<scene::UiRect>(popup); rect != nullptr && scene.has<scene::UiPopup>(popup))
    {
        rect->visible = false;
    }
    std::erase_if(m_popupPlacements, [popup](const PopupPlacement& placement) { return placement.popup == popup; });
}

bool UiWorld::isPopupOpen(const scene::Scene& scene, scene::Entity popup) const
{
    return scene.isAlive(popup) && scene.has<scene::UiPopup>(popup) && isOpen(scene, popup);
}

scene::Entity UiWorld::contextTarget() const noexcept
{
    return m_contextTarget;
}

bool UiWorld::closeMenusOutside(scene::Scene& scene, const UiInput& input)
{
    if (!input.pointerPressed && !input.secondaryPressed)
    {
        return false;
    }
    bool closed = false;
    for (std::size_t canvas = 0; canvas < m_canvases.size(); ++canvas)
    {
        const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
        const math::Vec2 point = toCanvas(canvas, input.pointer);
        for (std::size_t index = 0; index < rects.size(); ++index)
        {
            if (!rects[index].visible || !isMenu(scene, rects[index].entity))
            {
                continue;
            }
            const std::size_t end = subtreeEnd(rects, index);
            const bool inside = std::any_of(rects.begin() + static_cast<std::ptrdiff_t>(index),
                                            rects.begin() + static_cast<std::ptrdiff_t>(end),
                                            [&](const LaidOutRect& rect) { return rect.visible && contains(rect, point); });
            if (!inside)
            {
                closePopup(scene, rects[index].entity);
                closed = true;
            }
        }
    }
    // A press that closes a menu does nothing else: the second button may still open another.
    return closed && input.pointerPressed;
}

void UiWorld::closeMenusHolding(scene::Scene& scene, scene::Entity entity)
{
    bool inMenu = false;
    for (scene::Entity above = entity; above.isValid() && !inMenu; above = scene.parent(above))
    {
        inMenu = isMenu(scene, above) && isOpen(scene, above);
    }
    if (!inMenu)
    {
        return;
    }
    // A choice ends the whole menu, and the menus it opened from.
    for (auto [popup, component] : scene.view<scene::UiPopup>())
    {
        if (component.kind == scene::UiPopupKind::Menu && isOpen(scene, popup))
        {
            closePopup(scene, popup);
        }
    }
}

bool UiWorld::closeTopMenu(scene::Scene& scene)
{
    for (std::size_t canvas = m_canvases.size(); canvas-- > 0;)
    {
        const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
        for (std::size_t index = rects.size(); index-- > 0;)
        {
            if (rects[index].visible && isMenu(scene, rects[index].entity))
            {
                closePopup(scene, rects[index].entity);
                return true;
            }
        }
    }
    return false;
}

void UiWorld::openContextMenu(scene::Scene& scene, const UiInput& input)
{
    const auto found = hit(scene, input.pointer, false);
    if (!found)
    {
        return;
    }
    // The element under the pointer, or the first one above it, that has a menu.
    for (scene::Entity target = m_canvases[found->first].layout.rects[found->second].entity; target.isValid();
         target = scene.parent(target))
    {
        const scene::UiContextMenu* const menu = scene.tryGet<scene::UiContextMenu>(target);
        if (menu == nullptr)
        {
            continue;
        }
        const scene::Entity popup = scene.resolve(menu->popup);
        if (popup.isValid())
        {
            m_contextTarget = target;
            openPopup(scene, popup, input.pointer);
        }
        return;
    }
}

void UiWorld::openDropdown(const scene::Scene& scene, scene::Entity entity)
{
    const scene::UiDropdown& dropdown = scene.get<scene::UiDropdown>(entity);
    const auto count = static_cast<std::int32_t>(dropdown.options.size());
    const std::int32_t selected = dropdown.selected >= 0 && dropdown.selected < count ? dropdown.selected : 0;
    const std::size_t shown = std::min(dropdown.options.size(), dropdownRows);
    const std::size_t first = std::min(static_cast<std::size_t>(std::max(selected - static_cast<std::int32_t>(shown / 2), 0)),
                                       dropdown.options.size() - shown);
    m_dropdown = OpenDropdown{.entity = entity, .highlighted = selected, .first = first};
}

bool UiWorld::dropdownList(const scene::Scene& scene, math::Vec2& min, math::Vec2& max, float& item,
                           std::size_t& shown) const
{
    if (!m_dropdown || !scene.isAlive(m_dropdown->entity))
    {
        return false;
    }
    const scene::UiDropdown* const dropdown = scene.tryGet<scene::UiDropdown>(m_dropdown->entity);
    const CanvasLayout* const canvas = canvasOf(m_dropdown->entity);
    const LaidOutRect* const rect = canvas != nullptr ? canvas->layout.find(m_dropdown->entity) : nullptr;
    if (dropdown == nullptr || rect == nullptr || dropdown->options.empty())
    {
        return false;
    }
    // Under the element, as wide as it, one option as tall as it; above it when there is no room.
    const float scale = canvas->layout.scale;
    item = rect->size().y * scale;
    shown = std::min(dropdown->options.size(), dropdownRows);
    const float height = item * static_cast<float>(shown);
    min = math::Vec2{rect->min.x * scale, rect->max.y * scale + 2.0f};
    if (min.y + height > m_windowSize.y && rect->min.y * scale - 2.0f - height >= 0.0f)
    {
        min.y = rect->min.y * scale - 2.0f - height;
    }
    max = math::Vec2{rect->max.x * scale, min.y + height};
    return true;
}

bool UiWorld::updateDropdown(scene::Scene& scene, const UiInput& input)
{
    if (!m_dropdown)
    {
        return false;
    }
    math::Vec2 min{0.0f};
    math::Vec2 max{0.0f};
    float item = 0.0f;
    std::size_t shown = 0;
    scene::UiDropdown* const dropdown =
        scene.isAlive(m_dropdown->entity) ? scene.tryGet<scene::UiDropdown>(m_dropdown->entity) : nullptr;
    if (dropdown == nullptr || !dropdown->interactable || !dropdownList(scene, min, max, item, shown))
    {
        m_dropdown.reset();
        return false;
    }
    const std::size_t count = dropdown->options.size();
    const auto choose = [&](std::int32_t option) {
        if (option >= 0 && static_cast<std::size_t>(option) < count && option != dropdown->selected)
        {
            dropdown->selected = option;
            m_changed.push_back(m_dropdown->entity);
            if (!dropdown->action.empty())
            {
                m_changedActions.push_back(dropdown->action);
            }
        }
        m_dropdown.reset();
    };

    // The wheel walks the options that do not fit.
    if (input.wheel != 0.0f && count > shown)
    {
        const auto step = static_cast<std::ptrdiff_t>(-std::round(input.wheel));
        m_dropdown->first = static_cast<std::size_t>(
            std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(m_dropdown->first) + step, 0,
                                       static_cast<std::ptrdiff_t>(count - shown)));
    }
    const bool inside = input.pointer.x >= min.x && input.pointer.x <= max.x && input.pointer.y >= min.y &&
                        input.pointer.y <= max.y;
    if (inside && (input.pointerMoved || input.pointerPressed))
    {
        m_dropdown->highlighted = static_cast<std::int32_t>(
            m_dropdown->first + static_cast<std::size_t>((input.pointer.y - min.y) / std::max(item, 1.0f)));
    }
    if (input.moveY != 0)
    {
        m_dropdown->highlighted = std::clamp(m_dropdown->highlighted + input.moveY, 0, static_cast<std::int32_t>(count) - 1);
        const auto highlighted = static_cast<std::size_t>(m_dropdown->highlighted);
        if (highlighted < m_dropdown->first)
        {
            m_dropdown->first = highlighted;
        }
        else if (highlighted >= m_dropdown->first + shown)
        {
            m_dropdown->first = highlighted + 1 - shown;
        }
    }
    if (input.pointerPressed)
    {
        // A press on an option chooses it; a press anywhere else closes the list.
        if (inside)
        {
            choose(m_dropdown->highlighted);
        }
        else
        {
            m_dropdown.reset();
        }
    }
    else if (input.submitPressed)
    {
        choose(m_dropdown->highlighted);
    }
    else if (input.cancelPressed)
    {
        m_dropdown.reset();
        m_cancelled = false;
    }
    return true;
}

bool UiWorld::startDrag(scene::Scene& scene, const UiInput& input)
{
    if (!input.pointerPressed)
    {
        return false;
    }
    for (std::size_t canvas = m_canvases.size(); canvas-- > 0;)
    {
        if (!m_canvases[canvas].interactive || m_canvases[canvas].layout.scale <= 0.0f)
        {
            continue;
        }
        const math::Vec2 point = toCanvas(canvas, input.pointer);
        const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
        for (std::size_t index = rects.size(); index-- > 0;)
        {
            const LaidOutRect& rect = rects[index];
            if (!rect.visible || !reachable(canvas, index))
            {
                continue;
            }
            // What is drawn over the element from outside it keeps the pointer from its bars.
            const std::size_t end = subtreeEnd(rects, index);
            const bool covered = std::any_of(rects.begin() + static_cast<std::ptrdiff_t>(end),
                                             rects.end(), [&](const LaidOutRect& over) {
                                                 return over.visible && stopsPointer(scene, over.entity) &&
                                                        contains(over, point);
                                             });
            if (covered)
            {
                continue;
            }
            // The thumb of a scrollbar, or its track, which moves a page.
            if (scene::UiScroll* const scroll = scene.tryGet<scene::UiScroll>(rect.entity);
                scroll != nullptr && scroll->scrollbar && contains(rect, point))
            {
                const float bar = std::max(scroll->scrollbarSize, 1.0f);
                for (const int axis : {1, 0})
                {
                    const bool along = axis == 1 ? scroll->vertical && rect.content.y > rect.size().y + 0.5f
                                                 : scroll->horizontal && rect.content.x > rect.size().x + 0.5f;
                    const bool onBar = axis == 1 ? point.x >= rect.max.x - bar : point.y >= rect.max.y - bar;
                    if (!along || !onBar)
                    {
                        continue;
                    }
                    const auto [start, thumb] = thumbOf(rect, *scroll, axis);
                    const float at = axis == 1 ? point.y : point.x;
                    if (at >= start && at <= start + thumb)
                    {
                        m_drag = Drag{.kind = axis == 1 ? DragKind::ScrollVertical : DragKind::ScrollHorizontal,
                                      .entity = rect.entity,
                                      .grab = at - start};
                    }
                    else
                    {
                        const float page = axis == 1 ? rect.size().y : rect.size().x;
                        const float room = std::max((axis == 1 ? rect.content.y : rect.content.x) - page, 0.0f);
                        float& offset = axis == 1 ? scroll->offset.y : scroll->offset.x;
                        offset = std::clamp(offset + (at < start ? -page : page), 0.0f, room);
                    }
                    return true;
                }
            }
            // The bar of a splitter.
            if (const scene::UiSplitter* const splitter = scene.tryGet<scene::UiSplitter>(rect.entity))
            {
                const auto [barMin, barMax] = splitterBar(rect, *splitter);
                if (point.x >= barMin.x && point.x <= barMax.x && point.y >= barMin.y && point.y <= barMax.y)
                {
                    m_drag = Drag{.kind = DragKind::Splitter,
                                  .entity = rect.entity,
                                  .grab = splitter->vertical ? point.y - barMin.y : point.x - barMin.x};
                    return true;
                }
            }
            // The right edge of a header cell, which resizes its column.
            if (point.y >= rect.min.y && point.y <= rect.max.y && std::abs(point.x - rect.max.x) <= columnGrip)
            {
                if (const std::optional<HeaderCell> cell = headerCellOf(scene, rect.entity);
                    cell && cell->cell == rect.entity)
                {
                    const scene::UiTable& table = scene.get<scene::UiTable>(cell->table);
                    if (table.resizable && cell->column < table.columns.size())
                    {
                        m_drag = Drag{.kind = DragKind::Column,
                                      .entity = cell->table,
                                      .grab = table.columns[cell->column] - point.x,
                                      .column = cell->column};
                        return true;
                    }
                }
            }
        }
        // A canvas that took the pointer hides the ones under it.
        if (hit(scene, input.pointer, false))
        {
            return false;
        }
    }
    return false;
}

void UiWorld::updateDrag(scene::Scene& scene, const UiInput& input)
{
    m_barHover = scene::Entity{};
    if (m_drag.kind == DragKind::None)
    {
        // The bar of a splitter under the pointer lights up.
        for (std::size_t canvas = m_canvases.size(); canvas-- > 0 && !m_barHover.isValid();)
        {
            const math::Vec2 point = toCanvas(canvas, input.pointer);
            for (const LaidOutRect& rect : m_canvases[canvas].layout.rects)
            {
                const scene::UiSplitter* const splitter = scene.tryGet<scene::UiSplitter>(rect.entity);
                if (splitter == nullptr || !rect.visible)
                {
                    continue;
                }
                const auto [barMin, barMax] = splitterBar(rect, *splitter);
                if (point.x >= barMin.x && point.x <= barMax.x && point.y >= barMin.y && point.y <= barMax.y)
                {
                    m_barHover = rect.entity;
                }
            }
        }
        return;
    }
    if (!input.pointerDown || input.pointerReleased || !scene.isAlive(m_drag.entity))
    {
        m_drag = Drag{};
        return;
    }
    const CanvasLayout* const canvas = canvasOf(m_drag.entity);
    const LaidOutRect* const rect = canvas != nullptr ? canvas->layout.find(m_drag.entity) : nullptr;
    if (rect == nullptr)
    {
        m_drag = Drag{};
        return;
    }
    const math::Vec2 point{input.pointer.x / canvas->layout.scale, input.pointer.y / canvas->layout.scale};
    switch (m_drag.kind)
    {
    case DragKind::ScrollVertical:
    case DragKind::ScrollHorizontal: {
        scene::UiScroll& scroll = scene.get<scene::UiScroll>(m_drag.entity);
        const int axis = m_drag.kind == DragKind::ScrollVertical ? 1 : 0;
        const auto [start, thumb] = thumbOf(*rect, scroll, axis);
        static_cast<void>(start);
        const float size = axis == 1 ? rect->size().y : rect->size().x;
        const float content = axis == 1 ? rect->content.y : rect->content.x;
        const float track = std::max(size - thumb, 1.0f);
        const float from = (axis == 1 ? point.y - rect->min.y : point.x - rect->min.x) - m_drag.grab;
        (axis == 1 ? scroll.offset.y : scroll.offset.x) =
            std::clamp(from / track, 0.0f, 1.0f) * std::max(content - size, 0.0f);
        break;
    }
    case DragKind::Splitter: {
        scene::UiSplitter& splitter = scene.get<scene::UiSplitter>(m_drag.entity);
        splitter.position = (splitter.vertical ? point.y - rect->min.y : point.x - rect->min.x) - m_drag.grab;
        // Kept where the layout can put it, so that the bar does not lag behind the pointer later.
        const float length = splitter.vertical ? rect->size().y : rect->size().x;
        const float low = std::min(std::max(splitter.minSize, 0.0f), (length - splitter.barSize) * 0.5f);
        splitter.position = std::clamp(splitter.position, low, std::max(length - splitter.barSize - low, low));
        break;
    }
    case DragKind::Column: {
        scene::UiTable& table = scene.get<scene::UiTable>(m_drag.entity);
        if (m_drag.column < table.columns.size())
        {
            table.columns[m_drag.column] = std::max(point.x + m_drag.grab, columnMinimum);
        }
        break;
    }
    case DragKind::None:
        break;
    }
}

void UiWorld::updateControls(scene::Scene& scene, const UiInput& input)
{
    // A foldout shows what it holds only while expanded, whoever changed it.
    for (auto [entity, foldout] : scene.view<scene::UiFoldout>())
    {
        if (scene::UiRect* const content = scene.tryGet<scene::UiRect>(scene.resolve(foldout.content)))
        {
            content->visible = foldout.expanded;
        }
    }
    // A dropdown writes its choice into its text.
    for (auto [entity, dropdown] : scene.view<scene::UiDropdown>())
    {
        if (scene::UiText* const text = scene.tryGet<scene::UiText>(entity))
        {
            const bool chosen = dropdown.selected >= 0 && static_cast<std::size_t>(dropdown.selected) < dropdown.options.size();
            const std::string& option = chosen ? dropdown.options[static_cast<std::size_t>(dropdown.selected)] : dropdown.placeholder;
            if (text->text != option)
            {
                text->text = option;
            }
        }
    }
    // A virtual list says which item its first row shows, for a script to fill the rows; a list
    // whose content shrank, or that was moved from elsewhere, never shows past its end.
    for (const CanvasLayout& canvas : m_canvases)
    {
        for (const LaidOutRect& rect : canvas.layout.rects)
        {
            if (scene::UiVirtualList* const list = scene.tryGet<scene::UiVirtualList>(rect.entity))
            {
                list->first = virtualFirst(rect, *list);
            }
            if (scene::UiScroll* const scroll = scene.tryGet<scene::UiScroll>(rect.entity); scroll != nullptr && rect.visible)
            {
                scroll->offset.x = std::clamp(scroll->offset.x, 0.0f, std::max(rect.content.x - rect.size().x, 0.0f));
                scroll->offset.y = std::clamp(scroll->offset.y, 0.0f, std::max(rect.content.y - rect.size().y, 0.0f));
            }
        }
    }

    // A header cell pressed and let go sorts its table by its column, the other way when it
    // already did.
    if (input.pointerPressed)
    {
        m_headerPressed = scene::Entity{};
        if (const auto found = hit(scene, input.pointer, true))
        {
            if (const std::optional<HeaderCell> cell =
                    headerCellOf(scene, m_canvases[found->first].layout.rects[found->second].entity))
            {
                m_headerPressed = cell->cell;
                m_headerColumn = cell->column;
            }
        }
    }
    if (input.pointerReleased && m_headerPressed.isValid())
    {
        const auto found = hit(scene, input.pointer, true);
        const std::optional<HeaderCell> cell =
            found ? headerCellOf(scene, m_canvases[found->first].layout.rects[found->second].entity) : std::nullopt;
        if (cell && cell->cell == m_headerPressed)
        {
            scene::UiTable& table = scene.get<scene::UiTable>(cell->table);
            const auto column = static_cast<std::int32_t>(m_headerColumn);
            table.sortAscending = table.sortColumn == column ? !table.sortAscending : true;
            table.sortColumn = column;
            m_changed.push_back(cell->table);
            if (!table.action.empty())
            {
                m_changedActions.push_back(table.action);
            }
        }
        m_headerPressed = scene::Entity{};
    }
}

void UiWorld::updateTooltip(const scene::Scene& scene, const UiInput& input, float seconds)
{
    // The element under the pointer, or the first one above it, that has a tooltip.
    // The topmost element under the pointer that stops it or carries a tooltip: an element that
    // only draws, such as a text over a list, lets the pointer through to what lies under it.
    scene::Entity entity;
    if (!m_dropdown && m_drag.kind == DragKind::None && !m_carried)
    {
        scene::Entity under;
        for (std::size_t canvas = m_canvases.size(); canvas-- > 0 && !under.isValid();)
        {
            if (!m_canvases[canvas].interactive)
            {
                continue;
            }
            const math::Vec2 point = toCanvas(canvas, input.pointer);
            const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
            for (std::size_t index = rects.size(); index-- > 0;)
            {
                const LaidOutRect& rect = rects[index];
                if (rect.visible && rect.opacity > 0.0f && reachable(canvas, index) && contains(rect, point) &&
                    (stopsPointer(scene, rect.entity) || scene.has<scene::UiTooltip>(rect.entity)))
                {
                    under = rect.entity;
                    break;
                }
            }
        }
        for (scene::Entity above = under; above.isValid(); above = scene.parent(above))
        {
            if (scene.has<scene::UiTooltip>(above))
            {
                entity = above;
                break;
            }
        }
    }
    if (entity != m_tooltip.entity || input.pointerPressed || input.secondaryPressed || input.wheel != 0.0f)
    {
        // A press hides it until the pointer rests on another element.
        const bool pressed = entity == m_tooltip.entity && entity.isValid();
        m_tooltip = Tooltip{.entity = entity, .rested = pressed ? -1.0e9f : 0.0f};
        return;
    }
    if (!entity.isValid() || m_tooltip.shown)
    {
        return;
    }
    m_tooltip.rested += seconds;
    if (m_tooltip.rested >= scene.get<scene::UiTooltip>(entity).delay)
    {
        m_tooltip.shown = true;
        m_tooltip.at = input.pointer;
    }
}

void UiWorld::noteClick(const scene::Scene& scene, scene::Entity entity)
{
    if (entity == m_lastClick && m_clock - m_lastClickTime <= doubleClickSeconds)
    {
        m_doubleClicked.push_back(entity);
        if (const scene::UiButton* const button = scene.tryGet<scene::UiButton>(entity); button != nullptr && !button->action.empty())
        {
            m_doubleClickedActions.push_back(button->action);
        }
        // A third click starts over rather than making a second double click.
        m_lastClick = scene::Entity{};
        return;
    }
    m_lastClick = entity;
    m_lastClickTime = m_clock;
}

bool UiWorld::wasDoubleClicked(std::string_view action) const
{
    return std::ranges::find(m_doubleClickedActions, action) != m_doubleClickedActions.end();
}

bool UiWorld::wasDoubleClicked(scene::Entity entity) const
{
    return std::ranges::find(m_doubleClicked, entity) != m_doubleClicked.end();
}

void UiWorld::setTooltipStyle(TooltipStyle style)
{
    m_tooltipStyle = style;
}

void UiWorld::setTooltipsDrawn(bool drawn) noexcept
{
    m_tooltipsDrawn = drawn;
}

std::optional<UiWorld::ShownTooltip> UiWorld::shownTooltip(const scene::Scene& scene) const
{
    if (!m_tooltip.shown || !scene.isAlive(m_tooltip.entity))
    {
        return std::nullopt;
    }
    const scene::UiTooltip* const tooltip = scene.tryGet<scene::UiTooltip>(m_tooltip.entity);
    if (tooltip == nullptr || tooltip->text.empty())
    {
        return std::nullopt;
    }
    return ShownTooltip{.text = tooltip->text, .at = m_tooltip.at};
}

bool UiWorld::updateCarry(scene::Scene& scene, const UiInput& input, bool taken)
{
    // A drag from outside lasts as long as it is announced.
    const bool outside = m_outside.has_value();
    if (outside)
    {
        m_carried = std::move(m_outside);
        m_outside.reset();
        m_dragCandidate.reset();
    }
    else if (m_carried && !m_carried->source.isValid())
    {
        m_carried.reset();
        m_dropTarget = scene::Entity{};
    }

    if (!m_carried)
    {
        // A press on a drag source, or on an element inside one, makes it a candidate; it is taken
        // away once the pointer moves far enough, before which the press is an ordinary one.
        if (input.pointerPressed && !taken)
        {
            m_dragCandidate.reset();
            scene::Entity under;
            for (std::size_t canvas = m_canvases.size(); canvas-- > 0 && !under.isValid();)
            {
                if (!m_canvases[canvas].interactive || m_canvases[canvas].layout.scale <= 0.0f)
                {
                    continue;
                }
                const math::Vec2 point = toCanvas(canvas, input.pointer);
                const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
                for (std::size_t index = rects.size(); index-- > 0;)
                {
                    const LaidOutRect& rect = rects[index];
                    if (rect.visible && rect.opacity > 0.0f && reachable(canvas, index) && contains(rect, point) &&
                        stopsPointer(scene, rect.entity))
                    {
                        under = rect.entity;
                        break;
                    }
                }
            }
            for (scene::Entity above = under; above.isValid(); above = scene.parent(above))
            {
                if (const scene::UiDragSource* const source = scene.tryGet<scene::UiDragSource>(above))
                {
                    if (source->interactable)
                    {
                        m_dragCandidate = DragCandidate{.source = above, .from = input.pointer};
                    }
                    break;
                }
            }
        }
        if (!input.pointerDown || input.pointerReleased || (m_dragCandidate && !scene.isAlive(m_dragCandidate->source)))
        {
            m_dragCandidate.reset();
            return false;
        }
        if (!m_dragCandidate ||
            std::hypot(input.pointer.x - m_dragCandidate->from.x, input.pointer.y - m_dragCandidate->from.y) < carryDistance)
        {
            return false;
        }
        const scene::UiDragSource& source = scene.get<scene::UiDragSource>(m_dragCandidate->source);
        m_carried = Carried{.source = m_dragCandidate->source,
                            .type = source.type,
                            .data = source.data,
                            .label = source.label.empty() ? textBelow(scene, m_dragCandidate->source) : source.label};
        m_dragCandidate.reset();
        m_tooltip = Tooltip{};
    }

    // Escape puts back what the pointer carries, and does nothing else.
    if (input.cancelPressed && m_carried->source.isValid())
    {
        m_carried.reset();
        m_dropTarget = scene::Entity{};
        m_cancelled = false;
        return true;
    }
    m_dropTarget = targetUnder(scene, input.pointer, *m_carried);
    if (input.pointerReleased || !input.pointerDown)
    {
        // Only the release drops: a drag from outside may still be announced a frame after it.
        if (m_dropTarget.isValid() && input.pointerReleased)
        {
            Drop drop{.source = m_carried->source, .target = m_dropTarget, .type = m_carried->type, .data = m_carried->data};
            if (const CanvasLayout* const canvas = canvasOf(m_dropTarget))
            {
                if (const LaidOutRect* const rect = canvas->layout.find(m_dropTarget); rect != nullptr && canvas->layout.scale > 0.0f)
                {
                    const math::Vec2 point = input.pointer / canvas->layout.scale;
                    const math::Vec2 size{std::max(rect->size().x, 0.0001f), std::max(rect->size().y, 0.0001f)};
                    drop.at = math::Vec2{std::clamp((point.x - rect->min.x) / size.x, 0.0f, 1.0f),
                                         std::clamp((point.y - rect->min.y) / size.y, 0.0f, 1.0f)};
                }
            }
            m_drops.push_back(std::move(drop));
            if (const std::string& action = scene.get<scene::UiDropTarget>(m_dropTarget).action; !action.empty())
            {
                m_dropActions.push_back(action);
            }
        }
        m_carried.reset();
        m_dropTarget = scene::Entity{};
    }
    return true;
}

scene::Entity UiWorld::targetUnder(const scene::Scene& scene, math::Vec2 pointer, const Carried& carried) const
{
    // The topmost element under the pointer that stops it, then the first target at or above it
    // that accepts what is carried; a source is never dropped on itself.
    for (std::size_t canvas = m_canvases.size(); canvas-- > 0;)
    {
        if (!m_canvases[canvas].interactive || m_canvases[canvas].layout.scale <= 0.0f)
        {
            continue;
        }
        const math::Vec2 point = toCanvas(canvas, pointer);
        const std::vector<LaidOutRect>& rects = m_canvases[canvas].layout.rects;
        for (std::size_t index = rects.size(); index-- > 0;)
        {
            const LaidOutRect& rect = rects[index];
            if (!rect.visible || rect.opacity <= 0.0f || !reachable(canvas, index) || !contains(rect, point) ||
                !stopsPointer(scene, rect.entity))
            {
                continue;
            }
            for (scene::Entity above = rect.entity; above.isValid(); above = scene.parent(above))
            {
                const scene::UiDropTarget* const target = scene.tryGet<scene::UiDropTarget>(above);
                if (target != nullptr && above != carried.source &&
                    std::ranges::find(target->accepts, carried.type) != target->accepts.end())
                {
                    return above;
                }
            }
            return scene::Entity{};
        }
    }
    return scene::Entity{};
}

const Carried* UiWorld::carried() const noexcept
{
    return m_carried ? &*m_carried : nullptr;
}

void UiWorld::carryFromOutside(std::string type, std::string data)
{
    m_outside = Carried{.type = std::move(type), .data = std::move(data)};
}

scene::Entity UiWorld::dropTarget() const noexcept
{
    return m_dropTarget;
}

bool UiWorld::wasDropped(std::string_view action) const
{
    return std::ranges::find(m_dropActions, action) != m_dropActions.end();
}

bool UiWorld::wasDropped(scene::Entity target) const
{
    return std::ranges::find(m_drops, target, &Drop::target) != m_drops.end();
}

const Drop* UiWorld::dropped() const noexcept
{
    return m_drops.empty() ? nullptr : &m_drops.back();
}

void UiWorld::updateNumberFields(scene::Scene& scene, const UiInput& input)
{
    // Pressed, a number field waits to see whether the pointer drags it or lets it go.
    if (input.pointerPressed && m_hovered.isValid() && m_hovered != m_editing.entity)
    {
        if (const scene::UiNumberField* const number = scene.tryGet<scene::UiNumberField>(m_hovered);
            number != nullptr && number->interactable)
        {
            m_numberDrag = NumberDrag{.entity = m_hovered, .from = input.pointer, .lastX = input.pointer.x, .raw = number->value};
        }
    }
    if (!m_numberDrag || !input.pointerDown)
    {
        return;
    }
    scene::UiNumberField* const number =
        scene.isAlive(m_numberDrag->entity) ? scene.tryGet<scene::UiNumberField>(m_numberDrag->entity) : nullptr;
    const CanvasLayout* const canvas = canvasOf(m_numberDrag->entity);
    if (number == nullptr || !number->interactable || canvas == nullptr)
    {
        m_numberDrag.reset();
        return;
    }
    if (!m_numberDrag->moved)
    {
        const math::Vec2 moved = input.pointer - m_numberDrag->from;
        if (std::abs(moved.x) < numberDragDistance && std::abs(moved.y) < numberDragDistance)
        {
            return;
        }
        // The value moves from here, so that it does not jump by the distance that started the drag.
        m_numberDrag->moved = true;
        m_numberDrag->lastX = input.pointer.x;
        return;
    }
    const float units = (input.pointer.x - m_numberDrag->lastX) / std::max(canvas->layout.scale, 0.0001f);
    m_numberDrag->lastX = input.pointer.x;
    if (units == 0.0f)
    {
        return;
    }
    // Shift drags ten times finer.
    m_numberDrag->raw += static_cast<double>(units) * number->dragSpeed * (input.selecting ? 0.1 : 1.0);
    const double low = std::min(number->minValue, number->maxValue);
    const double high = std::max(number->minValue, number->maxValue);
    if (low < high)
    {
        // Past a bound the value waits there, and comes back as soon as the pointer does.
        m_numberDrag->raw = std::clamp(m_numberDrag->raw, low, high);
    }
    double value = m_numberDrag->raw;
    if (number->step <= 0.0f && number->decimals >= 0)
    {
        // Dragged values stop at the digits shown.
        const double unit = std::pow(10.0, std::min(number->decimals, 9));
        value = std::round(value * unit) / unit;
    }
    setNumber(m_numberDrag->entity, *number, value);
}

void UiWorld::beginNumberEdit(scene::Scene& scene, scene::Entity entity)
{
    const scene::UiNumberField* const number = scene.tryGet<scene::UiNumberField>(entity);
    scene::UiText* const text = scene.tryGet<scene::UiText>(entity);
    if (number == nullptr || text == nullptr || !scene.has<scene::UiInput>(entity))
    {
        return;
    }
    // The number alone, without what the format writes around it, selected to be typed over.
    text->text = formatNumber(number->value, number->decimals);
    startEditing(scene, entity);
}

void UiWorld::finishNumberEdit(scene::Scene& scene)
{
    if (!m_numberEdit.isValid() || m_editing.entity == m_numberEdit)
    {
        return;
    }
    const scene::Entity entity = std::exchange(m_numberEdit, scene::Entity{});
    const bool cancelled = std::exchange(m_editCancelled, false);
    const std::string started = std::exchange(m_numberEditText, std::string{});
    scene::UiNumberField* const number = scene.isAlive(entity) ? scene.tryGet<scene::UiNumberField>(entity) : nullptr;
    const scene::UiText* const text = number != nullptr ? scene.tryGet<scene::UiText>(entity) : nullptr;
    if (text == nullptr || cancelled || text->text == started)
    {
        return;
    }
    // What the format writes around the value may have been typed as well: 90° is 90.
    const FormatParts parts = splitFormat(number->format);
    std::string_view typed = trimmed(text->text);
    if (parts.hasValue)
    {
        if (const std::string_view before = trimmed(parts.before); !before.empty() && typed.starts_with(before))
        {
            typed.remove_prefix(before.size());
        }
        if (const std::string_view after = trimmed(parts.after); !after.empty() && typed.ends_with(after))
        {
            typed.remove_suffix(after.size());
        }
    }
    if (const std::optional<double> value = evaluateNumber(typed))
    {
        setNumber(entity, *number, *value);
    }
}

void UiWorld::setNumber(scene::Entity entity, scene::UiNumberField& field, double value)
{
    const auto stored = static_cast<float>(fitNumber(field, value));
    if (stored == field.value)
    {
        return;
    }
    field.value = stored;
    m_changed.push_back(entity);
    if (!field.action.empty())
    {
        m_changedActions.push_back(field.action);
    }
}

void UiWorld::writeNumberTexts(scene::Scene& scene) const
{
    for (auto [entity, number] : scene.view<scene::UiNumberField>())
    {
        scene::UiText* const text = scene.tryGet<scene::UiText>(entity);
        if (text == nullptr || entity == m_editing.entity)
        {
            continue;
        }
        const FormatParts parts = splitFormat(number.format);
        std::string shown = parts.hasValue
                                ? std::string(parts.before) + formatNumber(number.value, number.decimals) + std::string(parts.after)
                                : std::string(number.format);
        if (text->text != shown)
        {
            text->text = std::move(shown);
        }
    }
}

UiWorld::PickerState& UiWorld::pickerState(scene::Entity entity, math::Vec4 color)
{
    auto found = std::ranges::find(m_pickers, entity, &PickerState::entity);
    if (found == m_pickers.end())
    {
        m_pickers.push_back(PickerState{.entity = entity});
        found = std::prev(m_pickers.end());
    }
    if (found->color != color)
    {
        // A colour set from outside gives its hue, but a grey keeps the hue it had, and a black its
        // saturation as well, so that the square does not jump back to red.
        const float intensity = std::max({1.0f, color.x, color.y, color.z});
        const math::Vec3 hsv = hsvFromRgb(math::Vec3{srgbFromLinear(color.x / intensity), srgbFromLinear(color.y / intensity),
                                                     srgbFromLinear(color.z / intensity)});
        const bool known = found->color.x >= 0.0f;
        found->hsv = math::Vec3{known && (hsv.y <= 0.0f || hsv.z <= 0.0f) ? found->hsv.x : hsv.x,
                                known && hsv.z <= 0.0f ? found->hsv.y : hsv.y, hsv.z};
        found->intensity = intensity;
        found->color = color;
    }
    return *found;
}

bool UiWorld::pickerHsv(const scene::Scene& scene, scene::Entity entity, math::Vec3& hsv) const
{
    const scene::UiColorPicker* const picker = scene.isAlive(entity) ? scene.tryGet<scene::UiColorPicker>(entity) : nullptr;
    if (picker == nullptr)
    {
        return false;
    }
    if (const auto found = std::ranges::find(m_pickers, entity, &PickerState::entity);
        found != m_pickers.end() && found->color == picker->color)
    {
        hsv = found->hsv;
        return true;
    }
    const math::Vec4 color = picker->color;
    const float intensity = std::max({1.0f, color.x, color.y, color.z});
    hsv = hsvFromRgb(math::Vec3{srgbFromLinear(color.x / intensity), srgbFromLinear(color.y / intensity),
                                srgbFromLinear(color.z / intensity)});
    return true;
}

void UiWorld::updateColorPickers(scene::Scene& scene, const UiInput& input)
{
    std::erase_if(m_pickers, [&scene](const PickerState& state) {
        return !scene.isAlive(state.entity) || !scene.has<scene::UiColorPicker>(state.entity);
    });
    for (auto [entity, picker] : scene.view<scene::UiColorPicker>())
    {
        static_cast<void>(pickerState(entity, picker.color));
    }

    const auto partsOf = [this](scene::Entity entity, const scene::UiColorPicker& picker,
                                float& scale) -> std::optional<ColorPickerParts> {
        const CanvasLayout* const canvas = canvasOf(entity);
        const LaidOutRect* const rect = canvas != nullptr ? canvas->layout.find(entity) : nullptr;
        if (rect == nullptr)
        {
            return std::nullopt;
        }
        scale = std::max(canvas->layout.scale, 0.0001f);
        return colorPickerParts(*rect, picker);
    };
    const auto inside = [](math::Vec2 point, math::Vec2 min, math::Vec2 max) {
        return max.x > min.x && max.y > min.y && point.x >= min.x && point.x <= max.x && point.y >= min.y &&
               point.y <= max.y;
    };

    // The part pressed is the one dragged, until the button is let go.
    if (input.pointerPressed && m_hovered.isValid())
    {
        if (const scene::UiColorPicker* const picker = scene.tryGet<scene::UiColorPicker>(m_hovered);
            picker != nullptr && picker->interactable)
        {
            float scale = 1.0f;
            if (const std::optional<ColorPickerParts> parts = partsOf(m_hovered, *picker, scale))
            {
                const math::Vec2 point = input.pointer / scale;
                m_pickerPart = inside(point, parts->squareMin, parts->squareMax) ? PickerPart::Square
                               : inside(point, parts->hueMin, parts->hueMax)     ? PickerPart::Hue
                               : inside(point, parts->alphaMin, parts->alphaMax) ? PickerPart::Alpha
                                                                                 : PickerPart::None;
                m_pickerHeld = m_pickerPart != PickerPart::None ? m_hovered : scene::Entity{};
            }
        }
    }
    if (!m_pickerHeld.isValid())
    {
        return;
    }
    scene::UiColorPicker* const picker =
        scene.isAlive(m_pickerHeld) ? scene.tryGet<scene::UiColorPicker>(m_pickerHeld) : nullptr;
    float scale = 1.0f;
    const std::optional<ColorPickerParts> parts =
        picker != nullptr && picker->interactable ? partsOf(m_pickerHeld, *picker, scale) : std::nullopt;
    if (!parts || !input.pointerDown)
    {
        m_pickerHeld = scene::Entity{};
        m_pickerPart = PickerPart::None;
        return;
    }
    const math::Vec2 point = input.pointer / scale;
    const auto fraction = [](float at, float from, float to) {
        return to > from ? std::clamp((at - from) / (to - from), 0.0f, 1.0f) : 0.0f;
    };
    PickerState& state = pickerState(m_pickerHeld, picker->color);
    math::Vec3 hsv = state.hsv;
    math::Vec4 color = picker->color;
    switch (m_pickerPart)
    {
    case PickerPart::Square:
        hsv.y = fraction(point.x, parts->squareMin.x, parts->squareMax.x);
        hsv.z = 1.0f - fraction(point.y, parts->squareMin.y, parts->squareMax.y);
        break;
    case PickerPart::Hue:
        hsv.x = fraction(point.y, parts->hueMin.y, parts->hueMax.y);
        break;
    case PickerPart::Alpha:
        color.w = fraction(point.x, parts->alphaMin.x, parts->alphaMax.x);
        break;
    case PickerPart::None:
        break;
    }
    if (m_pickerPart != PickerPart::Alpha)
    {
        const math::Vec3 rgb = rgbFromHsv(hsv);
        color = math::Vec4{linearFromSrgb(rgb.x) * state.intensity, linearFromSrgb(rgb.y) * state.intensity,
                           linearFromSrgb(rgb.z) * state.intensity, color.w};
    }
    // The hue of a grey changes what the square shows, not the colour.
    state.hsv = hsv;
    if (color != picker->color)
    {
        picker->color = color;
        state.color = color;
        m_changed.push_back(m_pickerHeld);
        if (!picker->action.empty())
        {
            m_changedActions.push_back(picker->action);
        }
    }
}

std::string formatNumber(double value, std::int32_t decimals)
{
    if (!std::isfinite(value))
    {
        return std::isnan(value) ? "nan" : value > 0.0 ? "inf" : "-inf";
    }
    std::string text = decimals < 0 ? std::format("{}", static_cast<float>(value))
                                    : std::format("{:.{}f}", value, std::min(static_cast<int>(decimals), 9));
    if (decimals >= 0 && text.find('.') != std::string::npos)
    {
        while (text.back() == '0')
        {
            text.pop_back();
        }
        if (text.back() == '.')
        {
            text.pop_back();
        }
    }
    if (text == "-0")
    {
        text = "0";
    }
    return text;
}

std::optional<double> evaluateNumber(std::string_view text)
{
    text = trimmed(text);
    if (text.empty())
    {
        return std::nullopt;
    }
    return Expression(text).read();
}

} // namespace devex::ui
