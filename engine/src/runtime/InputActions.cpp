#include <devex/runtime/InputActions.hpp>

#include <devex/core/Log.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace devex::runtime {
namespace {

using asset::InputActionKind;
using asset::InputDirection;
using platform::InputDevice;
using platform::InputSource;

// How far an axis or a stick goes before a button action it drives is down, and before it answers
// a binding the player is choosing.
constexpr float pressPoint = 0.5f;

[[nodiscard]] float signOf(InputDirection direction) noexcept
{
    return direction == InputDirection::Negative || direction == InputDirection::Down || direction == InputDirection::Left
               ? -1.0f
               : 1.0f;
}

// Where a binding pushes a vector action: up is positive, and an axis direction goes along x.
[[nodiscard]] math::Vec2 directionOf(InputDirection direction) noexcept
{
    switch (direction)
    {
    case InputDirection::Up:
        return {0.0f, 1.0f};
    case InputDirection::Down:
        return {0.0f, -1.0f};
    case InputDirection::Left:
    case InputDirection::Negative:
        return {-1.0f, 0.0f};
    case InputDirection::Right:
    case InputDirection::Positive:
        break;
    }
    return {1.0f, 0.0f};
}

// Zero below the dead zone, and the rest of the range spread back from zero to one.
[[nodiscard]] float pastDeadZone(float amount, float deadZone) noexcept
{
    if (amount <= deadZone)
    {
        return 0.0f;
    }
    return std::min(1.0f, (amount - deadZone) / (1.0f - deadZone));
}

// A key or a button reads 0 or 1, an axis from -1 to 1 (0 to 1 for a trigger). Every gamepad
// plays: its axes count through the one pushed furthest.
[[nodiscard]] float valueOf(const platform::Input& input, InputSource source, bool typing)
{
    switch (source.device)
    {
    case InputDevice::Key:
        return !typing && input.isKeyDown(static_cast<platform::Key>(source.code)) ? 1.0f : 0.0f;
    case InputDevice::MouseButton:
        return input.isMouseButtonDown(static_cast<platform::MouseButton>(source.code)) ? 1.0f : 0.0f;
    case InputDevice::GamepadButton:
        for (std::size_t pad = 0; pad < platform::gamepadCount; ++pad)
        {
            if (input.isGamepadButtonDown(static_cast<platform::GamepadButton>(source.code), pad))
            {
                return 1.0f;
            }
        }
        return 0.0f;
    case InputDevice::GamepadAxis: {
        float value = 0.0f;
        for (std::size_t pad = 0; pad < platform::gamepadCount; ++pad)
        {
            const float axis = input.gamepadAxis(static_cast<platform::GamepadAxis>(source.code), pad);
            value = std::abs(axis) > std::abs(value) ? axis : value;
        }
        return value;
    }
    case InputDevice::GamepadStick:
        break;
    }
    return 0.0f;
}

// A stick with y up, from the gamepad whose stick is pushed furthest.
[[nodiscard]] math::Vec2 stickOf(const platform::Input& input, InputSource source)
{
    math::Vec2 value{0.0f};
    for (std::size_t pad = 0; pad < platform::gamepadCount; ++pad)
    {
        const math::Vec2 stick = source.code == platform::leftStick ? input.gamepadLeftStick(pad) : input.gamepadRightStick(pad);
        value = math::length(stick) > math::length(value) ? stick : value;
    }
    return {value.x, -value.y};
}

} // namespace

InputActions::InputActions(const asset::InputSettings& settings)
{
    setSettings(settings);
}

void InputActions::setSettings(const asset::InputSettings& settings)
{
    m_actions.clear();
    m_actionIndices.clear();
    m_contexts.clear();
    m_listening.reset();
    for (const asset::InputContext& context : settings.contexts)
    {
        m_contexts.push_back({context.name, context.activeAtStart});
    }
    for (const asset::InputAction& source : settings.actions)
    {
        if (source.name.empty() || m_actionIndices.contains(source.name))
        {
            DEVEX_LOG_WARNING("Input action '{}' is named twice or not at all: only the first one counts", source.name);
            continue;
        }
        Action action{
            .name = source.name,
            .kind = source.kind,
            .deadZone = std::clamp(source.deadZone, 0.0f, 0.99f),
        };
        if (!source.context.empty())
        {
            const auto context = std::ranges::find(m_contexts, source.context, &Context::name);
            if (context != m_contexts.end())
            {
                action.context = static_cast<std::size_t>(context - m_contexts.begin());
            }
            else
            {
                DEVEX_LOG_WARNING("Input action '{}' belongs to no context named '{}': it is always read", source.name,
                                  source.context);
            }
        }
        for (const asset::InputBinding& binding : source.bindings)
        {
            const std::optional<InputSource> parsed = platform::parseInputSource(binding.input);
            if (!parsed && !binding.input.empty())
            {
                DEVEX_LOG_WARNING("Input action '{}' is bound to '{}', which is no control", source.name, binding.input);
            }
            action.bindings.push_back({parsed, parsed, binding.direction});
        }
        m_actionIndices.emplace(action.name, m_actions.size());
        m_actions.push_back(std::move(action));
    }
}

void InputActions::setKeyLabeler(KeyLabeler labeler)
{
    m_keyLabeler = std::move(labeler);
}

void InputActions::evaluate(Action& action, const platform::Input& input, bool typing, bool active) const
{
    bool down = false;
    float axis = 0.0f;
    math::Vec2 vector{0.0f};
    for (const Binding& binding : active ? std::span<const Binding>(action.bindings) : std::span<const Binding>())
    {
        if (!binding.source)
        {
            continue;
        }
        const InputSource source = *binding.source;
        if (source.device == InputDevice::GamepadStick)
        {
            const math::Vec2 stick = stickOf(input, source);
            down = down || math::length(stick) >= pressPoint;
            const bool vertical = binding.direction == InputDirection::Up || binding.direction == InputDirection::Down;
            axis += (vertical ? stick.y : stick.x) * signOf(binding.direction);
            vector += stick;
            continue;
        }
        const float value = valueOf(input, source, typing);
        down = down || (platform::isAnalog(source) ? std::abs(value) >= pressPoint : value > 0.0f);
        axis += value * signOf(binding.direction);
        vector += value * directionOf(binding.direction);
    }

    switch (action.kind)
    {
    case InputActionKind::Button:
        axis = down ? 1.0f : 0.0f;
        vector = math::Vec2{0.0f};
        break;
    case InputActionKind::Axis: {
        const float amount = pastDeadZone(std::min(std::abs(axis), 1.0f), action.deadZone);
        axis = axis < 0.0f ? -amount : amount;
        vector = {axis, 0.0f};
        down = amount >= pressPoint;
        break;
    }
    case InputActionKind::Vector: {
        // Two keys at once go no faster than one: the direction has a length of one at most.
        const float length = math::length(vector);
        const float amount = pastDeadZone(std::min(length, 1.0f), action.deadZone);
        vector = length > 0.0f ? vector * (amount / length) : math::Vec2{0.0f};
        axis = 0.0f;
        down = amount >= pressPoint;
        break;
    }
    }

    action.pressed = down && !action.down;
    action.released = !down && action.down;
    action.down = down;
    action.axis = axis;
    action.vector = vector;
}

std::optional<InputSource> InputActions::answer(const platform::Input& input) const
{
    if (m_listening->keyboard)
    {
        for (const InputSource key : platform::inputSources(InputDevice::Key))
        {
            if (input.wasKeyPressed(static_cast<platform::Key>(key.code)))
            {
                return key;
            }
        }
        for (const InputSource button : platform::inputSources(InputDevice::MouseButton))
        {
            if (input.wasMouseButtonPressed(static_cast<platform::MouseButton>(button.code)))
            {
                return button;
            }
        }
    }
    if (m_listening->gamepad)
    {
        for (const InputSource button : platform::inputSources(InputDevice::GamepadButton))
        {
            for (std::size_t pad = 0; pad < platform::gamepadCount; ++pad)
            {
                if (input.wasGamepadButtonPressed(static_cast<platform::GamepadButton>(button.code), pad))
                {
                    return button;
                }
            }
        }
    }
    return std::nullopt;
}

void InputActions::update(const platform::Input& input, bool typing)
{
    const bool waiting = m_listening.has_value();
    m_tookInput = false;
    if (m_listening)
    {
        if (input.wasKeyPressed(platform::Key::Escape))
        {
            m_listening.reset();
            m_tookInput = true;
        }
        else if (const std::optional<InputSource> source = answer(input))
        {
            Binding& binding = m_actions[m_listening->action].bindings[m_listening->binding];
            m_changed = m_changed || binding.source != *source;
            binding.source = *source;
            m_listening.reset();
            m_tookInput = true;
        }
    }
    for (Action& action : m_actions)
    {
        const bool active = !waiting && (!action.context || m_contexts[*action.context].active);
        evaluate(action, input, typing, active);
    }
}

const InputActions::Action* InputActions::find(std::string_view action) const
{
    const auto found = m_actionIndices.find(action);
    return found != m_actionIndices.end() ? &m_actions[found->second] : nullptr;
}

InputActions::Action* InputActions::find(std::string_view action)
{
    const auto found = m_actionIndices.find(action);
    return found != m_actionIndices.end() ? &m_actions[found->second] : nullptr;
}

bool InputActions::hasAction(std::string_view action) const
{
    return find(action) != nullptr;
}

bool InputActions::isDown(std::string_view action) const
{
    const Action* const found = find(action);
    return found != nullptr && found->down;
}

bool InputActions::wasPressed(std::string_view action) const
{
    const Action* const found = find(action);
    return found != nullptr && found->pressed;
}

bool InputActions::wasReleased(std::string_view action) const
{
    const Action* const found = find(action);
    return found != nullptr && found->released;
}

float InputActions::axis(std::string_view action) const
{
    const Action* const found = find(action);
    return found != nullptr ? found->axis : 0.0f;
}

math::Vec2 InputActions::vector(std::string_view action) const
{
    const Action* const found = find(action);
    return found != nullptr ? found->vector : math::Vec2{0.0f};
}

bool InputActions::setContextActive(std::string_view context, bool active)
{
    const auto found = std::ranges::find(m_contexts, context, &Context::name);
    if (found == m_contexts.end())
    {
        return false;
    }
    found->active = active;
    return true;
}

bool InputActions::isContextActive(std::string_view context) const
{
    const auto found = std::ranges::find(m_contexts, context, &Context::name);
    return found != m_contexts.end() && found->active;
}

std::size_t InputActions::bindingCount(std::string_view action) const
{
    const Action* const found = find(action);
    return found != nullptr ? found->bindings.size() : 0;
}

std::optional<InputSource> InputActions::binding(std::string_view action, std::size_t index) const
{
    const Action* const found = find(action);
    return found != nullptr && index < found->bindings.size() ? found->bindings[index].source : std::nullopt;
}

std::string InputActions::bindingLabel(std::string_view action, std::size_t index) const
{
    const std::optional<InputSource> source = binding(action, index);
    if (!source)
    {
        return {};
    }
    if (source->device == InputDevice::Key && m_keyLabeler)
    {
        if (std::string label = m_keyLabeler(static_cast<platform::Key>(source->code)); !label.empty())
        {
            return label;
        }
    }
    return platform::displayName(*source);
}

bool InputActions::rebind(std::string_view action, std::size_t index, InputSource source)
{
    Action* const found = find(action);
    if (found == nullptr || index >= found->bindings.size() || platform::toString(source).empty())
    {
        return false;
    }
    m_changed = m_changed || found->bindings[index].source != source;
    found->bindings[index].source = source;
    return true;
}

bool InputActions::listen(std::string_view action, std::size_t index)
{
    const auto found = m_actionIndices.find(action);
    if (found == m_actionIndices.end() || index >= m_actions[found->second].bindings.size())
    {
        return false;
    }
    // A binding keeps to its kind of device; one that has none takes any.
    const std::optional<InputSource> current = m_actions[found->second].bindings[index].source;
    m_listening = Listening{
        .action = found->second,
        .binding = index,
        .keyboard = !current || platform::isKeyboardOrMouse(*current),
        .gamepad = !current || !platform::isKeyboardOrMouse(*current),
    };
    return true;
}

bool InputActions::isListening() const noexcept
{
    return m_listening.has_value();
}

void InputActions::stopListening() noexcept
{
    m_listening.reset();
}

bool InputActions::tookInput() const noexcept
{
    return m_tookInput;
}

void InputActions::resetBindings()
{
    for (Action& action : m_actions)
    {
        for (Binding& binding : action.bindings)
        {
            m_changed = m_changed || binding.source != binding.projectSource;
            binding.source = binding.projectSource;
        }
    }
}

bool InputActions::takeChanges() noexcept
{
    return std::exchange(m_changed, false);
}

std::string InputActions::writeOverrides() const
{
    serialization::TextDocument document;
    serialization::TextSection& header = document.sections.emplace_back();
    header.type = "input_bindings";
    header.attributes.push_back({"format", serialization::TextValue(std::int64_t{1})});
    for (const Action& action : m_actions)
    {
        for (std::size_t index = 0; index < action.bindings.size(); ++index)
        {
            const Binding& binding = action.bindings[index];
            if (binding.source == binding.projectSource || !binding.source)
            {
                continue;
            }
            serialization::TextSection& section = document.sections.emplace_back();
            section.type = "binding";
            section.attributes.push_back({"action", serialization::TextValue(action.name)});
            section.attributes.push_back({"index", serialization::TextValue(static_cast<std::int64_t>(index))});
            section.attributes.push_back({"input", serialization::TextValue(platform::toString(*binding.source))});
        }
    }
    return serialization::writeText(document);
}

void InputActions::readOverrides(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document || document->sections.empty() || document->sections.front().type != "input_bindings")
    {
        DEVEX_LOG_WARNING("The key bindings of the player cannot be read: they are left as the game sets them");
        return;
    }
    for (const serialization::TextSection& section : document->sections)
    {
        const serialization::TextValue* const action = section.findAttribute("action");
        const serialization::TextValue* const index = section.findAttribute("index");
        const serialization::TextValue* const input = section.findAttribute("input");
        const std::string* const name = action != nullptr ? serialization::asString(*action) : nullptr;
        const std::optional<std::int64_t> slot = index != nullptr ? serialization::asInteger(*index) : std::nullopt;
        const std::string* const inputText = input != nullptr ? serialization::asString(*input) : nullptr;
        const std::optional<InputSource> source = inputText != nullptr ? platform::parseInputSource(*inputText) : std::nullopt;
        Action* const found = section.type == "binding" && name != nullptr ? find(*name) : nullptr;
        if (found != nullptr && slot && *slot >= 0 && static_cast<std::size_t>(*slot) < found->bindings.size() && source)
        {
            found->bindings[static_cast<std::size_t>(*slot)].source = *source;
        }
    }
}

} // namespace devex::runtime
