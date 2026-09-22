#include <devex/ui/UiWorld.hpp>

#include <devex/asset/ThemeData.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace devex::ui {
namespace {

// One number of a field, written the way the binding asks for it.
[[nodiscard]] std::string writeNumber(float value, std::int32_t decimals)
{
    return decimals >= 0 ? std::format("{:.{}f}", value, static_cast<int>(decimals))
                         : std::format("{}", value);
}

// The part of a field a binding asks for: "x" of a vector, or nothing for the whole of it.
[[nodiscard]] std::size_t partIndex(std::string_view part) noexcept
{
    constexpr std::array<std::string_view, 8> names{"x", "y", "z", "w", "r", "g", "b", "a"};
    const auto found = std::ranges::find(names, part);
    return found != names.end()
               ? static_cast<std::size_t>(found - names.begin()) % 4
               : 0;
}

// What a field reads as, in the text a binding writes. A field the binding cannot read, such as
// a list, answers with nothing.
[[nodiscard]] std::string readField(const reflection::FieldInfo& field, const void* component,
                                    std::string_view part, std::int32_t decimals)
{
    if (field.list != nullptr)
    {
        return {};
    }
    const void* const address = field.address(component);
    const std::size_t index = partIndex(part);
    switch (field.kind)
    {
    case reflection::ValueKind::Bool:
        return *static_cast<const bool*>(address) ? "true" : "false";
    case reflection::ValueKind::Int32:
        return std::format("{}", *static_cast<const std::int32_t*>(address));
    case reflection::ValueKind::UInt32:
        return std::format("{}", *static_cast<const std::uint32_t*>(address));
    case reflection::ValueKind::Float:
        return writeNumber(*static_cast<const float*>(address), decimals);
    case reflection::ValueKind::String:
        return *static_cast<const std::string*>(address);
    case reflection::ValueKind::Vec2: {
        const auto& value = *static_cast<const math::Vec2*>(address);
        return part.empty() ? std::format("{}, {}", writeNumber(value.x, decimals),
                                          writeNumber(value.y, decimals))
                            : writeNumber(index == 1 ? value.y : value.x, decimals);
    }
    case reflection::ValueKind::Vec3: {
        const auto& value = *static_cast<const math::Vec3*>(address);
        if (part.empty())
        {
            return std::format("{}, {}, {}", writeNumber(value.x, decimals),
                               writeNumber(value.y, decimals), writeNumber(value.z, decimals));
        }
        const std::array<float, 3> parts{value.x, value.y, value.z};
        return writeNumber(parts[std::min(index, std::size_t{2})], decimals);
    }
    case reflection::ValueKind::Vec4: {
        const auto& value = *static_cast<const math::Vec4*>(address);
        if (part.empty())
        {
            return std::format("{}, {}, {}, {}", writeNumber(value.x, decimals),
                               writeNumber(value.y, decimals), writeNumber(value.z, decimals),
                               writeNumber(value.w, decimals));
        }
        const std::array<float, 4> parts{value.x, value.y, value.z, value.w};
        return writeNumber(parts[index], decimals);
    }
    case reflection::ValueKind::Enum: {
        const auto value = field.enumSize == 1
                               ? static_cast<std::size_t>(*static_cast<const std::uint8_t*>(address))
                               : static_cast<std::size_t>(*static_cast<const std::uint32_t*>(address));
        return value < field.enumNames.size() ? std::string(field.enumNames[value]) : std::string{};
    }
    case reflection::ValueKind::Quat:
    case reflection::ValueKind::Uuid:
    case reflection::ValueKind::AssetId:
    case reflection::ValueKind::Entity:
        break;
    }
    return {};
}

// Whether the pointer stops on the element rather than passing through it.
[[nodiscard]] bool answersPointer(const scene::Scene& scene, scene::Entity entity) noexcept
{
    if (scene.tryGet<scene::UiButton>(entity) != nullptr ||
        scene.tryGet<scene::UiInput>(entity) != nullptr ||
        scene.tryGet<scene::UiSlider>(entity) != nullptr ||
        scene.tryGet<scene::UiToggle>(entity) != nullptr)
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

[[nodiscard]] bool isClickable(const scene::Scene& scene, scene::Entity entity) noexcept
{
    const scene::UiButton* const button = scene.tryGet<scene::UiButton>(entity);
    return button != nullptr && button->interactable;
}

// Whether the pointer and the pad act on the element: a button, a box to tick, a slider to drag.
[[nodiscard]] bool takesFocus(const scene::Scene& scene, scene::Entity entity) noexcept
{
    if (isClickable(scene, entity))
    {
        return true;
    }
    if (const scene::UiToggle* const toggle = scene.tryGet<scene::UiToggle>(entity))
    {
        return toggle->interactable;
    }
    if (const scene::UiSlider* const slider = scene.tryGet<scene::UiSlider>(entity))
    {
        return slider->interactable;
    }
    return false;
}

// Whether the entity is a field that takes what is typed.
[[nodiscard]] bool isEditable(const scene::Scene& scene, scene::Entity entity) noexcept
{
    if (!entity.isValid() || !scene.isAlive(entity))
    {
        return false;
    }
    const scene::UiInput* const field = scene.tryGet<scene::UiInput>(entity);
    return field != nullptr && field->interactable &&
           scene.tryGet<scene::UiText>(entity) != nullptr;
}

// The field a click lands on, with its rectangle and the point in the units of its canvas.
struct FieldHit
{
    scene::Entity entity;
    const LaidOutRect* rect = nullptr;
    math::Vec2 point{0.0f};
};

[[nodiscard]] FieldHit fieldUnder(const scene::Scene& scene,
                                  std::span<const UiWorld::CanvasLayout> canvases,
                                  math::Vec2 pointer)
{
    // The topmost canvas answers first, and inside it the element drawn last.
    for (auto canvas = canvases.rbegin(); canvas != canvases.rend(); ++canvas)
    {
        if (!canvas->interactive || canvas->layout.scale <= 0.0f)
        {
            continue;
        }
        const math::Vec2 point{pointer.x / canvas->layout.scale, pointer.y / canvas->layout.scale};
        const std::vector<LaidOutRect>& rects = canvas->layout.rects;
        for (auto rect = rects.rbegin(); rect != rects.rend(); ++rect)
        {
            if (!rect->visible || rect->opacity <= 0.0f || !contains(*rect, point))
            {
                continue;
            }
            if (isEditable(scene, rect->entity))
            {
                return FieldHit{.entity = rect->entity, .rect = &*rect, .point = point};
            }
            if (answersPointer(scene, rect->entity))
            {
                // Something else took the click: it does not reach a field underneath.
                return FieldHit{};
            }
        }
    }
    return FieldHit{};
}

[[nodiscard]] math::Vec2 centre(const LaidOutRect& rect) noexcept
{
    return math::Vec2{(rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f};
}

[[nodiscard]] math::Vec4 towards(math::Vec4 from, math::Vec4 to, float amount) noexcept
{
    return math::Vec4{from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount,
                      from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount};
}

} // namespace

void UiWorld::update(scene::Scene& scene, math::Vec2 windowSize, const UiInput& input,
                     core::Duration delta)
{
    m_clicked.clear();
    m_clickedActions.clear();
    m_changed.clear();
    m_changedActions.clear();
    m_submitted.clear();
    m_submittedActions.clear();
    m_clipboardRequest.clear();
    m_cancelled = input.cancelPressed;

    updateBindings(scene);

    // The canvases of the scene, laid out in the order they are drawn.
    std::vector<CanvasLayout> previous = std::move(m_canvases);
    m_canvases.clear();
    for (auto [entity, canvas] : scene.view<scene::Canvas>())
    {
        if (!canvas.visible)
        {
            continue;
        }
        CanvasLayout state{.entity = entity,
                           .sortOrder = canvas.sortOrder,
                           .interactive = canvas.interactive};
        // The room a canvas laid out last frame is reused rather than allocated again.
        if (const auto found = std::ranges::find(previous, entity, &CanvasLayout::entity);
            found != previous.end())
        {
            state.layout = std::move(found->layout);
        }
        layoutCanvas(scene, entity, windowSize, state.layout);
        m_canvases.push_back(std::move(state));
    }
    std::ranges::stable_sort(m_canvases, {}, &CanvasLayout::sortOrder);
    for (const CanvasLayout& canvas : m_canvases)
    {
        updateTheme(scene, canvas);
    }

    updateHover(scene, input);
    updateScroll(scene, input);
    const bool editedBefore = m_editing.entity.isValid();
    updateFields(scene, input, delta);
    // While a field takes what is typed, the keys belong to it rather than to the menu around it.
    const bool editing = editedBefore || m_editing.entity.isValid();
    // The arrows that moved a slider do not move the focus as well.
    const bool slid = updateSliders(scene, input);
    if (!editing && !slid)
    {
        updateNavigation(scene, input);
    }

    if (input.pointerPressed)
    {
        m_pressed = takesFocus(scene, m_hovered) ? m_hovered : scene::Entity{};
        if (m_pressed.isValid())
        {
            m_focused = m_pressed;
        }
    }
    if (input.pointerReleased)
    {
        // A click needs the press and the release on the same button, as every interface does.
        if (m_pressed.isValid() && m_pressed == m_hovered)
        {
            click(scene, m_pressed);
        }
        m_pressed = scene::Entity{};
    }
    if (input.submitPressed && !editing && takesFocus(scene, m_focused))
    {
        click(scene, m_focused);
    }

    updateTints(scene, delta);
}

void UiWorld::updateBindings(scene::Scene& scene)
{
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    for (auto [entity, binding] : scene.view<scene::UiBinding>())
    {
        scene::UiText* const text = scene.tryGet<scene::UiText>(entity);
        if (text == nullptr || binding.field.empty())
        {
            continue;
        }
        const scene::Entity source =
            binding.source.isNil() ? entity : scene.resolve(binding.source);
        const scene::ComponentType* const type = registry.find(binding.component);
        const void* const component =
            type != nullptr && source.isValid() ? type->find(scene, source) : nullptr;
        if (component == nullptr)
        {
            continue;
        }
        // "position.x" names the field and the part of it that is wanted.
        std::string_view name = binding.field;
        std::string_view part;
        if (const std::size_t dot = name.find('.'); dot != std::string_view::npos)
        {
            part = name.substr(dot + 1);
            name = name.substr(0, dot);
        }
        const reflection::FieldInfo* const field = type->type->findField(name);
        if (field == nullptr)
        {
            continue;
        }
        const std::string value = readField(*field, component, part, binding.decimals);

        // Every {} of the pattern takes the value; the rest is written as it stands.
        std::string written;
        written.reserve(binding.format.size() + value.size());
        for (std::size_t index = 0; index < binding.format.size(); ++index)
        {
            if (binding.format.compare(index, 2, "{}") == 0)
            {
                written += value;
                ++index;
                continue;
            }
            written.push_back(binding.format[index]);
        }
        if (text->text != written)
        {
            text->text = written;
        }
    }
}

void UiWorld::updateTheme(scene::Scene& scene, const CanvasLayout& canvas)
{
    if (!m_themes)
    {
        return;
    }
    const scene::Canvas* const root = scene.tryGet<scene::Canvas>(canvas.entity);
    const asset::ThemeData* const theme =
        root != nullptr && root->theme.isValid() ? m_themes(root->theme) : nullptr;
    if (theme == nullptr)
    {
        return;
    }
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    for (const LaidOutRect& rect : canvas.layout.rects)
    {
        const scene::UiRect* const element = scene.tryGet<scene::UiRect>(rect.entity);
        if (element == nullptr || element->style.empty())
        {
            continue;
        }
        const asset::ThemeStyle* const style = theme->find(element->style);
        if (style == nullptr)
        {
            continue;
        }
        for (const asset::ThemeOverride& written : style->values)
        {
            const scene::ComponentType* const type = registry.find(written.component);
            void* const component =
                type != nullptr && type->findMutable ? type->findMutable(scene, rect.entity)
                                                     : nullptr;
            const reflection::FieldInfo* const field =
                component != nullptr ? type->type->findField(written.field) : nullptr;
            if (field == nullptr || field->list != nullptr)
            {
                continue;
            }
            const std::optional<serialization::TextValue> value =
                serialization::parseValue(written.value);
            if (!value)
            {
                continue;
            }
            // A value the style writes badly is left alone rather than shouting every frame.
            static_cast<void>(scene::readFieldValue(*field, *value, field->address(component)));
        }
    }
}

void UiWorld::updateHover(const scene::Scene& scene, const UiInput& input)
{
    m_hovered = scene::Entity{};
    m_pointerOverInterface = false;
    // The topmost canvas answers first, and inside it the element drawn last.
    for (auto canvas = m_canvases.rbegin(); canvas != m_canvases.rend(); ++canvas)
    {
        if (!canvas->interactive || canvas->layout.scale <= 0.0f)
        {
            continue;
        }
        const math::Vec2 point{input.pointer.x / canvas->layout.scale,
                               input.pointer.y / canvas->layout.scale};
        const std::vector<LaidOutRect>& rects = canvas->layout.rects;
        for (auto rect = rects.rbegin(); rect != rects.rend(); ++rect)
        {
            if (!rect->visible || rect->opacity <= 0.0f || !answersPointer(scene, rect->entity) ||
                !contains(*rect, point))
            {
                continue;
            }
            m_pointerOverInterface = true;
            if (takesFocus(scene, rect->entity))
            {
                m_hovered = rect->entity;
            }
            return;
        }
    }
}

void UiWorld::updateScroll(scene::Scene& scene, const UiInput& input)
{
    if (input.wheel == 0.0f)
    {
        return;
    }
    // The wheel moves the innermost list under the pointer, as every interface does.
    for (auto canvas = m_canvases.rbegin(); canvas != m_canvases.rend(); ++canvas)
    {
        if (!canvas->interactive || canvas->layout.scale <= 0.0f)
        {
            continue;
        }
        const math::Vec2 point{input.pointer.x / canvas->layout.scale,
                               input.pointer.y / canvas->layout.scale};
        const std::vector<LaidOutRect>& rects = canvas->layout.rects;
        for (auto rect = rects.rbegin(); rect != rects.rend(); ++rect)
        {
            scene::UiScroll* const scroll = scene.tryGet<scene::UiScroll>(rect->entity);
            if (scroll == nullptr || !rect->visible || !contains(*rect, point))
            {
                continue;
            }
            // The content cannot be moved further than what it hides.
            const math::Vec2 room{std::max(rect->content.x - rect->size().x, 0.0f),
                                  std::max(rect->content.y - rect->size().y, 0.0f)};
            if (scroll->vertical)
            {
                scroll->offset.y = std::clamp(scroll->offset.y - input.wheel * scroll->speed, 0.0f,
                                              room.y);
            }
            if (scroll->horizontal)
            {
                scroll->offset.x = std::clamp(scroll->offset.x - input.wheel * scroll->speed, 0.0f,
                                              room.x);
            }
            return;
        }
    }
}

void UiWorld::updateNavigation(const scene::Scene& scene, const UiInput& input)
{
    if (input.moveX == 0 && input.moveY == 0)
    {
        return;
    }
    // The buttons the focus can reach: the ones of the topmost canvas that answers.
    const CanvasLayout* canvas = nullptr;
    for (auto state = m_canvases.rbegin(); state != m_canvases.rend(); ++state)
    {
        if (state->interactive)
        {
            canvas = &*state;
            break;
        }
    }
    if (canvas == nullptr)
    {
        return;
    }

    const LaidOutRect* const from = canvas->layout.find(m_focused);
    if (from == nullptr)
    {
        // Nothing is focused yet: the first button of the canvas takes it.
        for (const LaidOutRect& rect : canvas->layout.rects)
        {
            if (rect.visible && takesFocus(scene, rect.entity))
            {
                m_focused = rect.entity;
                return;
            }
        }
        return;
    }

    const math::Vec2 origin = centre(*from);
    const math::Vec2 direction{static_cast<float>(input.moveX), static_cast<float>(input.moveY)};
    scene::Entity best;
    float bestDistance = std::numeric_limits<float>::max();
    for (const LaidOutRect& rect : canvas->layout.rects)
    {
        if (!rect.visible || rect.entity == m_focused || !takesFocus(scene, rect.entity))
        {
            continue;
        }
        const math::Vec2 offset{centre(rect).x - origin.x, centre(rect).y - origin.y};
        const float along = offset.x * direction.x + offset.y * direction.y;
        if (along <= 0.0f)
        {
            continue;
        }
        // The nearest button that way, counting what lies across the direction twice, so that the
        // focus follows a column or a row rather than jumping sideways.
        const float across = std::abs(offset.x * direction.y - offset.y * direction.x);
        const float distance = along + across * 2.0f;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = rect.entity;
        }
    }
    if (best.isValid())
    {
        m_focused = best;
    }
}

void UiWorld::updateFields(scene::Scene& scene, const UiInput& input, core::Duration delta)
{
    // A field that is gone, or that stopped taking what is typed, loses the edit.
    if (m_editing.entity.isValid() && !isEditable(scene, m_editing.entity))
    {
        m_editing = EditingField{};
    }

    if (input.pointerPressed)
    {
        const FieldHit hit = fieldUnder(scene, m_canvases, input.pointer);
        if (hit.entity != m_editing.entity)
        {
            m_editing = EditingField{.entity = hit.entity};
        }
        if (hit.entity.isValid() && hit.rect != nullptr)
        {
            // The click lands between two letters, and starts a selection there.
            m_editing.caret = caretFromPoint(scene, hit.entity, *hit.rect, hit.point);
            m_editing.anchor = m_editing.caret;
            m_editing.dragging = true;
            m_editing.blink = 0.0f;
        }
    }
    if (input.pointerReleased)
    {
        m_editing.dragging = false;
    }
    else if (m_editing.dragging && input.pointerMoved && m_editing.entity.isValid())
    {
        // Dragging through the letters takes them in.
        const CanvasLayout* const canvas = canvasOf(m_editing.entity);
        const LaidOutRect* const rect = canvas != nullptr ? canvas->layout.find(m_editing.entity)
                                                          : nullptr;
        if (rect != nullptr)
        {
            const math::Vec2 point{input.pointer.x / canvas->layout.scale,
                                   input.pointer.y / canvas->layout.scale};
            m_editing.caret = caretFromPoint(scene, m_editing.entity, *rect, point);
            m_editing.blink = 0.0f;
        }
    }

    if (m_editing.entity.isValid())
    {
        editField(scene, input);
    }

    // Where the cursor stands in the text the field draws, which is dots when it hides what is
    // typed: the drawing knows nothing of the text behind them.
    m_editVisual = EditState{};
    if (!m_editing.entity.isValid())
    {
        return;
    }
    m_editing.blink += std::chrono::duration<float>(delta).count();
    const auto* const text = scene.tryGet<scene::UiText>(m_editing.entity);
    const auto* const field = scene.tryGet<scene::UiInput>(m_editing.entity);
    if (text == nullptr || field == nullptr)
    {
        return;
    }
    const std::string shown = shownText(text->text, field->password);
    const std::size_t caret =
        offsetOfCharacter(shown, characterIndexOf(text->text, m_editing.caret));
    const std::size_t anchor =
        offsetOfCharacter(shown, characterIndexOf(text->text, m_editing.anchor));
    m_editVisual = EditState{.caret = caret,
                             .selectionMin = std::min(caret, anchor),
                             .selectionMax = std::max(caret, anchor),
                             // Shown a little longer than it is hidden, which reads better.
                             .caretVisible = std::fmod(m_editing.blink, 1.0f) < 0.6f};
}

void UiWorld::editField(scene::Scene& scene, const UiInput& input)
{
    auto* const text = scene.tryGet<scene::UiText>(m_editing.entity);
    const auto* const field = scene.tryGet<scene::UiInput>(m_editing.entity);
    if (text == nullptr || field == nullptr)
    {
        return;
    }
    // A script may have written the text since the last frame.
    m_editing.caret = std::min(m_editing.caret, text->text.size());
    m_editing.anchor = std::min(m_editing.anchor, text->text.size());

    bool moved = false;
    const auto selectionMin = [this] { return std::min(m_editing.caret, m_editing.anchor); };
    const auto selectionMax = [this] { return std::max(m_editing.caret, m_editing.anchor); };
    const auto hasSelection = [this] { return m_editing.caret != m_editing.anchor; };
    const auto place = [&](std::size_t offset, bool keepAnchor) {
        m_editing.caret = offset;
        if (!keepAnchor)
        {
            m_editing.anchor = offset;
        }
        moved = true;
    };
    const auto eraseSelection = [&] {
        if (!hasSelection())
        {
            return false;
        }
        const std::size_t from = selectionMin();
        text->text.erase(from, selectionMax() - from);
        place(from, false);
        return true;
    };
    // What comes in: never the keys that steer, and new lines only when the field takes them.
    const auto insert = [&](std::string_view added) {
        std::string kept;
        kept.reserve(added.size());
        for (const char letter : added)
        {
            const auto byte = static_cast<unsigned char>(letter);
            if (byte == '\n')
            {
                if (field->multiline)
                {
                    kept.push_back('\n');
                }
                continue;
            }
            if (byte >= 0x20 && byte != 0x7F)
            {
                kept.push_back(letter);
            }
        }
        if (kept.empty())
        {
            return;
        }
        eraseSelection();
        if (field->maxLength > 0)
        {
            // The room left is counted in characters, which is what a field promises.
            const std::size_t held = characterIndexOf(text->text, text->text.size());
            if (held >= field->maxLength)
            {
                return;
            }
            const std::size_t room = field->maxLength - held;
            if (characterIndexOf(kept, kept.size()) > room)
            {
                kept.resize(offsetOfCharacter(kept, room));
            }
        }
        text->text.insert(m_editing.caret, kept);
        place(m_editing.caret + kept.size(), false);
    };

    if (!input.typed.empty())
    {
        insert(input.typed);
    }
    if (input.backspacePressed)
    {
        if (!eraseSelection() && m_editing.caret > 0)
        {
            const std::size_t from = previousOffset(text->text, m_editing.caret);
            text->text.erase(from, m_editing.caret - from);
            place(from, false);
        }
        moved = true;
    }
    if (input.deletePressed)
    {
        if (!eraseSelection() && m_editing.caret < text->text.size())
        {
            std::size_t next = m_editing.caret;
            static_cast<void>(nextCodepoint(text->text, next));
            text->text.erase(m_editing.caret, next - m_editing.caret);
        }
        moved = true;
    }
    if (input.leftPressed)
    {
        // Without a modifier the arrows leave a selection by its side rather than walking into it.
        if (hasSelection() && !input.selecting)
        {
            place(selectionMin(), false);
        }
        else
        {
            place(previousOffset(text->text, m_editing.caret), input.selecting);
        }
    }
    if (input.rightPressed)
    {
        if (hasSelection() && !input.selecting)
        {
            place(selectionMax(), false);
        }
        else
        {
            std::size_t next = m_editing.caret;
            if (next < text->text.size())
            {
                static_cast<void>(nextCodepoint(text->text, next));
            }
            place(next, input.selecting);
        }
    }
    if (input.selectAllPressed)
    {
        m_editing.anchor = 0;
        place(text->text.size(), true);
    }

    // The ends of a line and the line above or below are read from the text as it was placed.
    const bool needsLines =
        input.homePressed || input.endPressed || input.upPressed || input.downPressed;
    const CanvasLayout* const canvas = needsLines ? canvasOf(m_editing.entity) : nullptr;
    const LaidOutRect* const rect =
        canvas != nullptr ? canvas->layout.find(m_editing.entity) : nullptr;
    if (rect != nullptr && layoutField(*text, *field, *rect))
    {
        const std::string shown = shownText(text->text, field->password);
        const std::size_t here =
            offsetOfCharacter(shown, characterIndexOf(text->text, m_editing.caret));
        if (const CaretStop* const stop = caretAt(m_fieldLayout, here))
        {
            const auto toText = [&](const CaretStop& target) {
                return offsetOfCharacter(text->text, characterIndexOf(shown, target.offset));
            };
            if (input.homePressed || input.endPressed)
            {
                const CaretStop* best = stop;
                for (const CaretStop& other : m_fieldLayout.stops)
                {
                    if (other.line == stop->line &&
                        (input.homePressed ? other.offset < best->offset
                                           : other.offset > best->offset))
                    {
                        best = &other;
                    }
                }
                place(toText(*best), input.selecting);
            }
            if ((input.upPressed || input.downPressed) && field->multiline &&
                !(input.upPressed && stop->line == 0))
            {
                // The nearest place of the line above or below, under the cursor.
                const std::uint32_t wanted = input.upPressed ? stop->line - 1 : stop->line + 1;
                const CaretStop* best = nullptr;
                float bestDistance = 0.0f;
                for (const CaretStop& other : m_fieldLayout.stops)
                {
                    if (other.line != wanted)
                    {
                        continue;
                    }
                    const float distance = std::abs(other.position.x - stop->position.x);
                    if (best == nullptr || distance < bestDistance)
                    {
                        best = &other;
                        bestDistance = distance;
                    }
                }
                if (best != nullptr)
                {
                    place(toText(*best), input.selecting);
                }
            }
        }
    }

    // A password is never handed to the clipboard, whatever is asked of it.
    if ((input.copyPressed || input.cutPressed) && hasSelection() && !field->password)
    {
        m_clipboardRequest = text->text.substr(selectionMin(), selectionMax() - selectionMin());
        if (input.cutPressed)
        {
            eraseSelection();
        }
    }
    if (input.pastePressed && !input.clipboard.empty())
    {
        insert(input.clipboard);
    }

    if (input.submitPressed)
    {
        if (field->multiline)
        {
            insert("\n");
        }
        else
        {
            submit(scene, m_editing.entity);
            m_editing = EditingField{};
            return;
        }
    }
    if (input.cancelPressed)
    {
        // The field ate the key: the menu around it does not close as well.
        m_editing = EditingField{};
        m_cancelled = false;
        return;
    }
    if (moved)
    {
        m_editing.blink = 0.0f;
    }
}

bool UiWorld::layoutField(const scene::UiText& text, const scene::UiInput& field,
                          const LaidOutRect& rect)
{
    m_fieldLayout.clear();
    if (!m_fonts)
    {
        return false;
    }
    const FontRef font = m_fonts(text.font.isValid() ? text.font : m_defaultFont);
    if (font.data == nullptr)
    {
        return false;
    }
    math::Vec2 boxMin{0.0f};
    math::Vec2 boxMax{0.0f};
    fieldBox(field, rect.min, rect.max, boxMin, boxMax);
    layoutText(*font.data, shownText(text.text, field.password), fieldStyle(text, field), boxMin,
               boxMax, m_fieldLayout);
    return true;
}

std::size_t UiWorld::caretFromPoint(const scene::Scene& scene, scene::Entity entity,
                                    const LaidOutRect& rect, math::Vec2 point)
{
    const auto* const text = scene.tryGet<scene::UiText>(entity);
    const auto* const field = scene.tryGet<scene::UiInput>(entity);
    if (text == nullptr || field == nullptr)
    {
        return 0;
    }
    if (!layoutField(*text, *field, rect))
    {
        // Without a font the click cannot be placed: the cursor goes after the last letter.
        return text->text.size();
    }
    const std::size_t shownOffset = offsetAt(m_fieldLayout, point);
    return offsetOfCharacter(text->text,
                             characterIndexOf(shownText(text->text, field->password), shownOffset));
}

const UiWorld::CanvasLayout* UiWorld::canvasOf(scene::Entity entity) const noexcept
{
    for (const CanvasLayout& canvas : m_canvases)
    {
        if (canvas.layout.find(entity) != nullptr)
        {
            return &canvas;
        }
    }
    return nullptr;
}

void UiWorld::updateTints(const scene::Scene& scene, core::Duration delta)
{
    const auto seconds = std::chrono::duration<float>(delta).count();
    // Buttons that are gone leave their tint behind.
    std::erase_if(m_buttons, [&scene](const ButtonState& state) {
        return !scene.isAlive(state.entity) || scene.tryGet<scene::UiButton>(state.entity) == nullptr;
    });
    for (auto [entity, button] : scene.view<scene::UiButton>())
    {
        math::Vec4 target{1.0f, 1.0f, 1.0f, 1.0f};
        if (!button.interactable)
        {
            target = button.disabledColor;
        }
        else if (m_pressed == entity)
        {
            target = button.pressedColor;
        }
        else if (m_hovered == entity || m_focused == entity)
        {
            target = button.hoverColor;
        }
        ButtonState& state = buttonState(entity);
        const float amount =
            button.fadeTime > 0.0f ? std::min(seconds / button.fadeTime, 1.0f) : 1.0f;
        state.tint = towards(state.tint, target, amount);
    }
}

UiWorld::ButtonState& UiWorld::buttonState(scene::Entity entity)
{
    const auto found = std::ranges::find(m_buttons, entity, &ButtonState::entity);
    if (found != m_buttons.end())
    {
        return *found;
    }
    m_buttons.push_back({.entity = entity});
    return m_buttons.back();
}

void UiWorld::click(scene::Scene& scene, scene::Entity entity)
{
    // A slider answers the drag, not the click: it has nothing to report here.
    if (scene.tryGet<scene::UiSlider>(entity) != nullptr)
    {
        return;
    }
    m_clicked.push_back(entity);
    if (const scene::UiButton* const button = scene.tryGet<scene::UiButton>(entity);
        button != nullptr && !button->action.empty())
    {
        m_clickedActions.push_back(button->action);
    }
    if (scene::UiToggle* const toggle = scene.tryGet<scene::UiToggle>(entity);
        toggle != nullptr && toggle->interactable)
    {
        toggle->value = !toggle->value;
        m_changed.push_back(entity);
        if (!toggle->action.empty())
        {
            m_changedActions.push_back(toggle->action);
        }
    }
}

bool UiWorld::updateSliders(scene::Scene& scene, const UiInput& input)
{
    if (input.pointerPressed && m_hovered.isValid() &&
        scene.tryGet<scene::UiSlider>(m_hovered) != nullptr)
    {
        m_dragged = m_hovered;
    }
    if (!input.pointerDown || input.pointerReleased)
    {
        m_dragged = scene::Entity{};
    }
    if (m_dragged.isValid())
    {
        scene::UiSlider* const slider = scene.tryGet<scene::UiSlider>(m_dragged);
        const CanvasLayout* const canvas = canvasOf(m_dragged);
        const LaidOutRect* const rect =
            canvas != nullptr ? canvas->layout.find(m_dragged) : nullptr;
        if (slider == nullptr || !slider->interactable || rect == nullptr)
        {
            m_dragged = scene::Entity{};
        }
        else
        {
            // The value follows the middle of the handle, which stays inside the track.
            const float point = input.pointer.x / canvas->layout.scale;
            const float handle = std::max(slider->handleSize, 0.0f) * rect->size().y;
            const float room = std::max(rect->size().x - handle, 0.0001f);
            const float amount =
                std::clamp((point - rect->min.x - handle * 0.5f) / room, 0.0f, 1.0f);
            const float low = std::min(slider->minValue, slider->maxValue);
            const float high = std::max(slider->minValue, slider->maxValue);
            changeSlider(m_dragged, *slider, low + amount * (high - low));
        }
    }

    if (input.moveX == 0 || !m_focused.isValid())
    {
        return false;
    }
    scene::UiSlider* const slider = scene.tryGet<scene::UiSlider>(m_focused);
    if (slider == nullptr || !slider->interactable)
    {
        return false;
    }
    const float low = std::min(slider->minValue, slider->maxValue);
    const float high = std::max(slider->minValue, slider->maxValue);
    const float step = slider->keyStep > 0.0f      ? slider->keyStep
                       : slider->step > 0.0f       ? slider->step
                                                   : (high - low) * 0.05f;
    changeSlider(m_focused, *slider, slider->value + static_cast<float>(input.moveX) * step);
    return true;
}

void UiWorld::changeSlider(scene::Entity entity, scene::UiSlider& slider, float value)
{
    const float low = std::min(slider.minValue, slider.maxValue);
    const float high = std::max(slider.minValue, slider.maxValue);
    float wanted = std::clamp(value, low, high);
    if (slider.step > 0.0f)
    {
        wanted = std::clamp(low + std::round((wanted - low) / slider.step) * slider.step, low, high);
    }
    if (wanted == slider.value)
    {
        return;
    }
    slider.value = wanted;
    m_changed.push_back(entity);
    if (!slider.action.empty())
    {
        m_changedActions.push_back(slider.action);
    }
}

void UiWorld::submit(const scene::Scene& scene, scene::Entity entity)
{
    m_submitted.push_back(entity);
    if (const scene::UiInput* const field = scene.tryGet<scene::UiInput>(entity);
        field != nullptr && !field->action.empty())
    {
        m_submittedActions.push_back(field->action);
    }
}

void UiWorld::build(const scene::Scene& scene, const DrawContext& context,
                    render::RenderWorld& world) const
{
    DrawContext withTints = context;
    if (!withTints.tint)
    {
        withTints.tint = [this](scene::Entity entity) { return tint(entity); };
    }
    if (!withTints.editing)
    {
        withTints.editing = [this](scene::Entity entity) { return editStateOf(entity); };
    }
    for (const CanvasLayout& canvas : m_canvases)
    {
        buildDrawList(scene, canvas.layout, withTints, world);
    }
}

bool UiWorld::wasClicked(std::string_view action) const
{
    return std::ranges::find(m_clickedActions, action) != m_clickedActions.end();
}

bool UiWorld::wasClicked(scene::Entity entity) const
{
    return std::ranges::find(m_clicked, entity) != m_clicked.end();
}

bool UiWorld::wasChanged(std::string_view action) const
{
    return std::ranges::find(m_changedActions, action) != m_changedActions.end();
}

bool UiWorld::wasChanged(scene::Entity entity) const
{
    return std::ranges::find(m_changed, entity) != m_changed.end();
}

bool UiWorld::wasSubmitted(std::string_view action) const
{
    return std::ranges::find(m_submittedActions, action) != m_submittedActions.end();
}

bool UiWorld::wasSubmitted(scene::Entity entity) const
{
    return std::ranges::find(m_submitted, entity) != m_submitted.end();
}

void UiWorld::setFonts(std::function<FontRef(asset::AssetId)> fonts, asset::AssetId defaultFont)
{
    m_fonts = std::move(fonts);
    m_defaultFont = defaultFont;
}

void UiWorld::setThemes(std::function<const asset::ThemeData*(asset::AssetId)> themes)
{
    m_themes = std::move(themes);
}

scene::Entity UiWorld::editedField() const noexcept
{
    return m_editing.entity;
}

bool UiWorld::isEditing() const noexcept
{
    return m_editing.entity.isValid();
}

const EditState* UiWorld::editStateOf(scene::Entity entity) const noexcept
{
    return entity.isValid() && entity == m_editing.entity ? &m_editVisual : nullptr;
}

const std::string& UiWorld::clipboardRequest() const noexcept
{
    return m_clipboardRequest;
}

bool UiWorld::wasCancelled() const noexcept
{
    return m_cancelled;
}

scene::Entity UiWorld::hovered() const noexcept
{
    return m_hovered;
}

scene::Entity UiWorld::focused() const noexcept
{
    return m_focused;
}

void UiWorld::setFocus(const scene::Scene& scene, scene::Entity entity)
{
    m_focused = takesFocus(scene, entity) ? entity : scene::Entity{};
}

bool UiWorld::pointerOverInterface() const noexcept
{
    return m_pointerOverInterface;
}

math::Vec4 UiWorld::tint(scene::Entity entity) const
{
    const auto found = std::ranges::find(m_buttons, entity, &ButtonState::entity);
    return found != m_buttons.end() ? found->tint : math::Vec4{1.0f};
}

std::span<const UiWorld::CanvasLayout> UiWorld::canvases() const noexcept
{
    return m_canvases;
}

void UiWorld::clear()
{
    m_canvases.clear();
    m_buttons.clear();
    m_clicked.clear();
    m_clickedActions.clear();
    m_changed.clear();
    m_changedActions.clear();
    m_submitted.clear();
    m_submittedActions.clear();
    m_hovered = scene::Entity{};
    m_pressed = scene::Entity{};
    m_dragged = scene::Entity{};
    m_focused = scene::Entity{};
    m_pointerOverInterface = false;
    m_cancelled = false;
    m_editing = EditingField{};
    m_editVisual = EditState{};
}

} // namespace devex::ui
