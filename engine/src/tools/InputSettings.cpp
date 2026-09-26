#include "ToolsState.hpp"

#include <devex/platform/InputSource.hpp>

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace devex::tools::detail {
namespace {

using asset::InputActionKind;
using asset::InputDirection;
using platform::InputDevice;
using platform::InputSource;

struct DeviceGroup
{
    InputDevice device;
    const char* title;
};

constexpr std::array deviceGroups{
    DeviceGroup{InputDevice::Key, "Keyboard"},
    DeviceGroup{InputDevice::MouseButton, "Mouse"},
    DeviceGroup{InputDevice::GamepadButton, "Gamepad buttons"},
    DeviceGroup{InputDevice::GamepadAxis, "Gamepad axes and triggers"},
    DeviceGroup{InputDevice::GamepadStick, "Gamepad sticks"},
};

constexpr std::array<const char*, 3> kindNames{"Button", "Axis", "Vector"};

// A key as this keyboard prints it, with its place when that differs ("Z" for the W of a US
// keyboard on AZERTY); other controls by their name.
[[nodiscard]] std::string labelOf(const ToolsState& state, InputSource source)
{
    if (source.device != InputDevice::Key)
    {
        return platform::displayName(source);
    }
    const std::string printed = state.platform.keyLabel(static_cast<platform::Key>(source.code));
    const std::string place = platform::displayName(source);
    if (printed.empty() || printed == place)
    {
        return place;
    }
    return std::format("{} ({})", printed, place);
}

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    const auto lower = [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); };
    return std::ranges::search(text, part, {}, lower, lower).begin() != text.end() || part.empty();
}

// The list a binding is chosen from, by device, with a filter.
void drawSourcePicker(ToolsState& state, std::string& input)
{
    const std::optional<InputSource> current = platform::parseInputSource(input);
    const std::string preview = current ? labelOf(state, *current) : input.empty() ? "(none)" : input + " (unknown)";
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!beginCombo("##input", preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        return;
    }
    if (ImGui::IsWindowAppearing())
    {
        state.inputSourceFilter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter", &state.inputSourceFilter);
    for (const DeviceGroup& group : deviceGroups)
    {
        bool titled = false;
        for (const InputSource source : platform::inputSources(group.device))
        {
            const std::string label = labelOf(state, source);
            if (!containsIgnoringCase(label, state.inputSourceFilter))
            {
                continue;
            }
            if (!std::exchange(titled, true))
            {
                ImGui::SeparatorText(group.title);
            }
            const std::string text = platform::toString(source);
            if (ImGui::Selectable(std::format("{}##{}", label, text).c_str(), current == source))
            {
                input = text;
            }
        }
    }
    ImGui::EndCombo();
}

// The directions a binding may push an action of this kind.
[[nodiscard]] std::span<const InputDirection> directionsOf(InputActionKind kind)
{
    static constexpr std::array axisDirections{InputDirection::Positive, InputDirection::Negative};
    static constexpr std::array vectorDirections{InputDirection::Up, InputDirection::Down, InputDirection::Left,
                                                 InputDirection::Right};
    if (kind == InputActionKind::Axis)
    {
        return axisDirections;
    }
    if (kind == InputActionKind::Vector)
    {
        return vectorDirections;
    }
    return {};
}

void drawDirection(asset::InputBinding& binding, InputActionKind kind)
{
    const std::optional<InputSource> source = platform::parseInputSource(binding.input);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (kind == InputActionKind::Vector && source && source->device == InputDevice::GamepadStick)
    {
        ImGui::BeginDisabled();
        if (beginCombo("##direction", "whole stick"))
        {
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        return;
    }
    std::string preview(asset::toString(binding.direction));
    preview[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(preview[0])));
    if (beginCombo("##direction", preview.c_str()))
    {
        for (const InputDirection direction : directionsOf(kind))
        {
            std::string name(asset::toString(direction));
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            if (ImGui::Selectable(name.c_str(), binding.direction == direction))
            {
                binding.direction = direction;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(source && platform::isAnalog(*source) ? "Where the positive end of the axis pushes the action"
                                                                 : "Where the key or button pushes the action");
}

// A binding waiting for a key or a button takes the first one pressed; Escape gives up.
void answerListening(ToolsState& state, asset::InputSettings& input)
{
    if (!state.listeningBinding)
    {
        return;
    }
    const auto [actionIndex, bindingIndex] = *state.listeningBinding;
    if (actionIndex >= input.actions.size() || bindingIndex >= input.actions[actionIndex].bindings.size())
    {
        state.listeningBinding.reset();
        return;
    }
    std::optional<InputSource> answer;
    if (state.pressedKey == platform::Key::Escape)
    {
        state.listeningBinding.reset();
        return;
    }
    if (state.pressedKey)
    {
        answer = platform::keySource(*state.pressedKey);
    }
    const platform::Input& devices = state.platform.input();
    for (const InputSource button : platform::inputSources(InputDevice::GamepadButton))
    {
        for (std::size_t pad = 0; pad < platform::gamepadCount && !answer; ++pad)
        {
            if (devices.wasGamepadButtonPressed(static_cast<platform::GamepadButton>(button.code), pad))
            {
                answer = button;
            }
        }
    }
    if (answer && !platform::toString(*answer).empty())
    {
        input.actions[actionIndex].bindings[bindingIndex].input = platform::toString(*answer);
        state.listeningBinding.reset();
    }
}

void drawBindings(ToolsState& state, asset::InputAction& action, std::size_t actionIndex)
{
    const ThemeColors& colors = themeColors();
    const bool directed = action.kind != InputActionKind::Button;
    std::optional<std::size_t> removed;
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    if (!action.bindings.empty() && ImGui::BeginTable("bindings", directed ? 4 : 3, flags))
    {
        ImGui::TableSetupColumn("input", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        if (directed)
        {
            ImGui::TableSetupColumn("direction", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        }
        ImGui::TableSetupColumn("listen", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed);
        for (std::size_t index = 0; index < action.bindings.size(); ++index)
        {
            asset::InputBinding& binding = action.bindings[index];
            const bool listening = state.listeningBinding == std::pair{actionIndex, index};
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (listening)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(uiColor(colors.accent), "Press a key or a gamepad button (Esc cancels)");
            }
            else
            {
                drawSourcePicker(state, binding.input);
            }
            if (directed)
            {
                ImGui::TableNextColumn();
                drawDirection(binding, action.kind);
            }
            ImGui::TableNextColumn();
            if (toolButton("##listen", icons::Keyboard, "Bind the next key or gamepad button pressed", listening))
            {
                state.listeningBinding = listening ? std::nullopt : std::optional(std::pair{actionIndex, index});
            }
            ImGui::TableNextColumn();
            if (toolButton("##remove", icons::Trash, "Remove this binding"))
            {
                removed = index;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removed)
    {
        action.bindings.erase(action.bindings.begin() + static_cast<std::ptrdiff_t>(*removed));
        state.listeningBinding.reset();
    }
    if (labelButton(icons::Plus, "Add Binding"))
    {
        action.bindings.push_back({});
    }
}

void drawAction(ToolsState& state, asset::InputSettings& input, std::size_t actionIndex, bool& remove)
{
    const ThemeColors& colors = themeColors();
    asset::InputAction& action = input.actions[actionIndex];
    const bool named = !action.name.empty() &&
                       std::ranges::count(input.actions, action.name, &asset::InputAction::name) == 1;
    const std::string title =
        std::format("{}   {}###action", action.name.empty() ? "(unnamed)" : action.name,
                    kindNames[static_cast<std::size_t>(action.kind)]);
    const bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    if (toolButton("##remove action", icons::Trash, "Remove this action"))
    {
        remove = true;
    }
    if (!open)
    {
        return;
    }

    ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
    if (beginProperties("action"))
    {
        propertyName("Name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##name", &action.name);
        if (!named)
        {
            ImGui::TextColored(uiColor(colors.warning), action.name.empty() ? "An action needs a name." : "Another action has this name.");
        }
        propertyName("Kind");
        if (beginCombo("##kind", kindNames[static_cast<std::size_t>(action.kind)]))
        {
            for (std::size_t kind = 0; kind < kindNames.size(); ++kind)
            {
                if (ImGui::Selectable(kindNames[kind], static_cast<std::size_t>(action.kind) == kind))
                {
                    action.kind = static_cast<InputActionKind>(kind);
                    // Directions follow the new kind.
                    const std::span<const InputDirection> directions = directionsOf(action.kind);
                    for (asset::InputBinding& binding : action.bindings)
                    {
                        if (!directions.empty() && std::ranges::find(directions, binding.direction) == directions.end())
                        {
                            binding.direction = directions.front();
                        }
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("A button is pressed or not; an axis goes from -1 to 1; a vector gives a direction");
        propertyName("Context");
        if (beginCombo("##context", action.context.empty() ? "(always read)" : action.context.c_str()))
        {
            if (ImGui::Selectable("(always read)", action.context.empty()))
            {
                action.context.clear();
            }
            for (const asset::InputContext& context : input.contexts)
            {
                if (ImGui::Selectable(context.name.c_str(), action.context == context.name))
                {
                    action.context = context.name;
                }
            }
            ImGui::EndCombo();
        }
        if (action.kind != InputActionKind::Button)
        {
            propertyName("Dead zone");
            ImGui::SliderFloat("##dead zone", &action.deadZone, 0.0f, 0.9f, "%.2f");
            ImGui::SetItemTooltip("Below this, the action reads zero; gamepads already ignore small moves of their sticks");
        }
        endProperties();
    }
    drawBindings(state, action, actionIndex);
    ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
    ImGui::Spacing();
}

void drawContexts(asset::InputSettings& input)
{
    std::optional<std::size_t> removed;
    if (!input.contexts.empty() &&
        ImGui::BeginTable("contexts", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("active", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed);
        for (std::size_t index = 0; index < input.contexts.size(); ++index)
        {
            asset::InputContext& context = input.contexts[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string previous = context.name;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##name", "name", &context.name))
            {
                // The actions of the context follow it under its new name.
                for (asset::InputAction& action : input.actions)
                {
                    action.context = action.context == previous ? context.name : action.context;
                }
            }
            ImGui::TableNextColumn();
            ImGui::Checkbox("Active at start", &context.activeAtStart);
            ImGui::TableNextColumn();
            if (toolButton("##remove", icons::Trash, "Remove this context: its actions are then always read"))
            {
                removed = index;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removed)
    {
        const std::string name = input.contexts[*removed].name;
        input.contexts.erase(input.contexts.begin() + static_cast<std::ptrdiff_t>(*removed));
        for (asset::InputAction& action : input.actions)
        {
            action.context = action.context == name ? std::string() : action.context;
        }
    }
    if (labelButton(icons::Plus, "Add Context"))
    {
        input.contexts.push_back({.name = std::format("Context {}", input.contexts.size() + 1)});
    }
}

} // namespace

void drawInputSettings(ToolsState& state, asset::InputSettings& input)
{
    answerListening(state, input);

    ImGui::TextWrapped("Games read actions rather than keys: Input.IsActionDown(\"Jump\") in C#, "
                       "context.actions->isDown(\"Jump\") in C++. Players may bind them to other keys, "
                       "which the game keeps for them.");
    ImGui::Spacing();
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("Contexts");
    ImGui::PopFont();
    ImGui::TextDisabled("Game code turns contexts on and off; the actions of one that is off read as released.");
    drawContexts(input);

    ImGui::Spacing();
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("Actions");
    ImGui::PopFont();
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < input.actions.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        bool remove = false;
        drawAction(state, input, index, remove);
        removed = remove ? std::optional(index) : removed;
        ImGui::PopID();
    }
    if (removed)
    {
        input.actions.erase(input.actions.begin() + static_cast<std::ptrdiff_t>(*removed));
        state.listeningBinding.reset();
    }
    if (labelButton(icons::Plus, "Add Action"))
    {
        input.actions.push_back({
            .name = std::format("Action {}", input.actions.size() + 1),
            .context = input.contexts.empty() ? std::string() : input.contexts.front().name,
        });
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Changes apply the next time the game starts.");
}

} // namespace devex::tools::detail
