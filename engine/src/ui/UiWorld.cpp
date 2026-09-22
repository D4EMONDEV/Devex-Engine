#include <devex/ui/UiWorld.hpp>

#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace devex::ui {
namespace {

// Whether the pointer stops on the element rather than passing through it.
[[nodiscard]] bool answersPointer(const scene::Scene& scene, scene::Entity entity) noexcept
{
    if (scene.tryGet<scene::UiButton>(entity) != nullptr)
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

void UiWorld::update(const scene::Scene& scene, math::Vec2 windowSize, const UiInput& input,
                     core::Duration delta)
{
    m_clicked.clear();
    m_clickedActions.clear();
    m_cancelled = input.cancelPressed;

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

    updateHover(scene, input);
    updateNavigation(scene, input);

    if (input.pointerPressed)
    {
        m_pressed = isClickable(scene, m_hovered) ? m_hovered : scene::Entity{};
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
    if (input.submitPressed && isClickable(scene, m_focused))
    {
        click(scene, m_focused);
    }

    updateTints(scene, delta);
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
            if (isClickable(scene, rect->entity))
            {
                m_hovered = rect->entity;
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
            if (rect.visible && isClickable(scene, rect.entity))
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
        if (!rect.visible || rect.entity == m_focused || !isClickable(scene, rect.entity))
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

void UiWorld::click(const scene::Scene& scene, scene::Entity entity)
{
    m_clicked.push_back(entity);
    if (const scene::UiButton* const button = scene.tryGet<scene::UiButton>(entity);
        button != nullptr && !button->action.empty())
    {
        m_clickedActions.push_back(button->action);
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
    m_focused = isClickable(scene, entity) ? entity : scene::Entity{};
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
    m_hovered = scene::Entity{};
    m_pressed = scene::Entity{};
    m_focused = scene::Entity{};
    m_pointerOverInterface = false;
    m_cancelled = false;
}

} // namespace devex::ui
