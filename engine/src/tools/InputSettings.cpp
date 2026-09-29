// The Input Map section of the project settings, as Godot's: the contexts game code turns on and off,
// and a card per action with its kind, its context and its bindings. A binding is chosen in the event
// window, which listens for the next key or gamepad button pressed and lists every key, button, axis
// and stick by device, with a filter.
#include "SettingsUi.hpp"

#include <devex/platform/InputSource.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

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

const std::vector<std::string> kindNames{"Button", "Axis", "Vector"};

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

// What a binding shows: its control, or that it has none or an unknown one.
[[nodiscard]] std::string bindingLabel(const ToolsState& state, const std::string& input)
{
    if (const std::optional<InputSource> source = platform::parseInputSource(input))
    {
        return labelOf(state, *source);
    }
    return input.empty() ? std::string("(none)") : std::format("{} (unknown)", input);
}

[[nodiscard]] const char* deviceOf(const std::string& input)
{
    const std::optional<InputSource> source = platform::parseInputSource(input);
    if (!source)
    {
        return "Binding";
    }
    switch (source->device)
    {
    case InputDevice::Key:
        return "Key";
    case InputDevice::MouseButton:
        return "Mouse";
    default:
        return "Gamepad";
    }
}

// The directions a binding may push an action of this kind.
[[nodiscard]] std::span<const InputDirection> directionsOf(InputActionKind kind)
{
    static constexpr std::array axisDirections{InputDirection::Positive, InputDirection::Negative};
    static constexpr std::array vectorDirections{InputDirection::Up, InputDirection::Down, InputDirection::Left, InputDirection::Right};
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

[[nodiscard]] std::vector<std::string> directionNames(InputActionKind kind)
{
    std::vector<std::string> names;
    for (const InputDirection direction : directionsOf(kind))
    {
        std::string name(asset::toString(direction));
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        names.push_back(std::move(name));
    }
    return names;
}

// A stick gives a whole vector: its binding has no direction to choose.
[[nodiscard]] bool wholeStick(InputActionKind kind, const std::string& input)
{
    const std::optional<InputSource> source = platform::parseInputSource(input);
    return kind == InputActionKind::Vector && source && source->device == InputDevice::GamepadStick;
}

void openEventDialog(ProjectSettingsUi& ui, ToolsState& state, EditorUiKit& kit, EventTarget target)
{
    InputMapUi& map = ui.input;
    const asset::InputAction& action = ui.project.input.actions[target.action];
    const bool existing = target.binding < action.bindings.size();
    map.target = target;
    map.chosen = existing ? platform::parseInputSource(action.bindings[target.binding].input) : std::nullopt;
    map.listening = true;
    const std::span<const InputDirection> allowed = directionsOf(action.kind);
    map.directions.assign(allowed.begin(), allowed.end());
    ui.scene().get<scene::UiText>(map.dialogTitle).text =
        std::format("{} of {}", existing ? "Binding" : "New binding", action.name.empty() ? "(unnamed)" : action.name);
    ui.scene().get<scene::UiText>(map.search).text.clear();
    map.lastSearch.clear();
    ui.scene().get<scene::UiScroll>(map.sourceScroll).offset = math::Vec2{0.0f};

    // The controls by device, their names as this keyboard prints them.
    std::vector<Entity> children;
    for (Entity child = ui.scene().firstChild(map.sourceList); child.isValid(); child = ui.scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        ui.scene().destroyEntity(child);
    }
    map.sources.clear();
    map.groupTitles.clear();
    const float rowHeight = std::round(ui.font * 1.8f);
    for (const DeviceGroup& group : deviceGroups)
    {
        map.groupTitles.push_back(ui.text(map.sourceList, wide(rowHeight), group.title, "dim", true));
        for (const InputSource source : platform::inputSources(group.device))
        {
            const Button row = ui.button(kit, map.sourceList, std::nullopt, labelOf(state, source), "row", -1.0f, rowHeight, scene::TextAlign::Left);
            map.sources.emplace_back(source, row);
        }
    }

    const std::vector<std::string> names = directionNames(action.kind);
    std::int32_t direction = 0;
    if (existing)
    {
        const auto found = std::ranges::find(map.directions, action.bindings[target.binding].direction);
        direction = found != map.directions.end() ? static_cast<std::int32_t>(found - map.directions.begin()) : 0;
    }
    scene::UiDropdown& chooser = ui.scene().get<scene::UiDropdown>(map.direction);
    chooser.options = names;
    chooser.selected = direction;
    ui.scene().get<UiRect>(map.directionRow).visible = !names.empty();
    ui.panel.world().openPopup(ui.scene(), map.dialog);
}

void closeEventDialog(ProjectSettingsUi& ui)
{
    ui.panel.world().closePopup(ui.scene(), ui.input.dialog);
    ui.input.target.reset();
    ui.input.chosen.reset();
    ui.input.listening = false;
}

void syncEventDialog(ProjectSettingsUi& ui, ToolsState& state)
{
    InputMapUi& map = ui.input;
    if (!map.target)
    {
        return;
    }
    scene::Scene& scene = ui.scene();
    const std::string chosen = map.chosen ? labelOf(state, *map.chosen) : std::string{};
    scene.get<scene::UiText>(map.listenText).text =
        map.listening ? (chosen.empty() ? std::string("Press a key or a gamepad button...") : std::format("{}   (or press another)", chosen))
                      : (chosen.empty() ? std::string("Click here, then press a key or a gamepad button") : chosen);
    scene.get<UiRect>(map.listen).style = map.listening ? "row_selected" : "row";

    // The list keeps what holds the filter, and the titles of the devices that keep something.
    const std::string& wanted = scene.get<scene::UiText>(map.search).text;
    if (wanted != map.lastSearch)
    {
        map.lastSearch = wanted;
        scene.get<scene::UiScroll>(map.sourceScroll).offset = math::Vec2{0.0f};
    }
    const scene::UiLayout& layout = scene.get<scene::UiLayout>(map.sourceList);
    float height = layout.padding.y + layout.padding.w;
    std::size_t shown = 0;
    std::size_t source = 0;
    for (std::size_t group = 0; group < deviceGroups.size(); ++group)
    {
        bool any = false;
        const std::size_t count = platform::inputSources(deviceGroups[group].device).size();
        for (std::size_t index = 0; index < count && source < map.sources.size(); ++index, ++source)
        {
            const auto& [control, row] = map.sources[source];
            UiRect& rect = scene.get<UiRect>(row.entity);
            rect.visible = containsIgnoringCase(scene.get<scene::UiText>(row.label).text, wanted);
            rect.style = map.chosen == control ? "row_selected" : "row";
            if (rect.visible)
            {
                any = true;
                height += rect.offsetMax.y - rect.offsetMin.y;
                ++shown;
            }
        }
        UiRect& title = scene.get<UiRect>(map.groupTitles[group]);
        title.visible = any;
        if (any)
        {
            height += title.offsetMax.y - title.offsetMin.y;
            ++shown;
        }
    }
    height += layout.spacing * static_cast<float>(shown > 0 ? shown - 1 : 0);
    UiRect& list = scene.get<UiRect>(map.sourceList);
    list.offsetMax.y = list.offsetMin.y + height;

    // A stick gives a whole vector.
    const asset::InputAction& action = ui.project.input.actions[map.target->action];
    scene::UiDropdown& direction = scene.get<scene::UiDropdown>(map.direction);
    const bool stick = map.chosen && wholeStick(action.kind, platform::toString(*map.chosen));
    direction.interactable = !stick;
    direction.placeholder = "Whole stick";
    if (stick)
    {
        direction.selected = -1;
    }
    else if (direction.selected < 0)
    {
        direction.selected = 0;
    }
    ui.enable(map.confirm, map.chosen.has_value());
}

void answerEventDialog(ProjectSettingsUi& ui, ToolsState& state)
{
    InputMapUi& map = ui.input;
    if (!map.target)
    {
        return;
    }
    ui::UiWorld& world = ui.panel.world();
    if (!world.isPopupOpen(ui.scene(), map.dialog))
    {
        closeEventDialog(ui);
        return;
    }
    // Typing a filter is not a binding.
    if (world.editedField() == map.search)
    {
        map.listening = false;
    }
    if (world.wasClicked(map.listen))
    {
        map.listening = true;
    }
    bool cancel = world.wasClicked(map.cancel.entity);
    bool confirm = false;
    if (map.listening && ui.panel.focused())
    {
        if (state.pressedKey == platform::Key::Escape)
        {
            cancel = true;
        }
        else if (state.pressedKey)
        {
            map.chosen = platform::keySource(*state.pressedKey);
        }
        const platform::Input& devices = state.platform.input();
        for (const InputSource button : platform::inputSources(InputDevice::GamepadButton))
        {
            for (std::size_t pad = 0; pad < platform::gamepadCount; ++pad)
            {
                if (devices.wasGamepadButtonPressed(static_cast<platform::GamepadButton>(button.code), pad))
                {
                    map.chosen = button;
                }
            }
        }
    }
    else if (ui.panel.input().cancelPressed && !world.isEditing())
    {
        cancel = true;
    }
    for (const auto& [control, row] : map.sources)
    {
        if (world.wasClicked(row.entity))
        {
            map.chosen = control;
            map.listening = false;
            confirm = world.wasDoubleClicked(row.entity);
        }
    }
    confirm = (confirm || world.wasClicked(map.confirm.entity)) && map.chosen && !platform::toString(*map.chosen).empty();
    if (confirm)
    {
        asset::InputAction& action = ui.project.input.actions[map.target->action];
        const std::int32_t selected = ui.scene().get<scene::UiDropdown>(map.direction).selected;
        asset::InputBinding binding{.input = platform::toString(*map.chosen)};
        binding.direction = selected >= 0 && static_cast<std::size_t>(selected) < map.directions.size()
                                ? map.directions[static_cast<std::size_t>(selected)]
                                : (map.directions.empty() ? InputDirection::Positive : map.directions.front());
        if (map.target->binding < action.bindings.size())
        {
            action.bindings[map.target->binding] = std::move(binding);
        }
        else
        {
            action.bindings.push_back(std::move(binding));
        }
        closeEventDialog(ui);
    }
    else if (cancel)
    {
        closeEventDialog(ui);
    }
}

} // namespace

std::string inputMapSignature(const asset::InputSettings& input)
{
    std::string text = std::format("{}|", input.contexts.size());
    for (const asset::InputAction& action : input.actions)
    {
        text += std::format("{}:{}:{},", static_cast<int>(action.kind), action.bindings.size(), action.name);
    }
    return text;
}

void buildInputMap(ProjectSettingsUi& ui, EditorUiKit& kit, std::size_t page)
{
    InputMapUi& map = ui.input;
    map.contexts.clear();
    map.actions.clear();
    const asset::InputSettings& input = ui.project.input;
    const float tool = ui.line - 6.0f;

    Section& intro = ui.pageCard(kit, page, "Input Map");
    ui.note(&intro,
            "Games read actions rather than keys: Input.IsActionDown(\"Jump\") in C#, context.actions->isDown(\"Jump\") in C++. "
            "Players may bind them to other keys, which the game keeps for them.",
            "dim", 3.0f);
    ui.note(&intro, "Changes apply the next time the game starts.");
    map.addAction = ui.action(kit, ui.actions(&intro), Icon::Plus, "Add Action");

    Section& contexts = ui.pageCard(kit, page, "Contexts");
    ui.note(&contexts, "Game code turns contexts on and off; the actions of one that is off read as released.", "dim", 2.0f);
    const float box = std::round(ui.font * 1.3f);
    const float word = kit.textWidth(EditorUiKit::regularFont(), "Active at start", ui.font) + 4.0f;
    const float right = tool + ui.font * 0.6f + box + ui.font * 0.4f + word;
    for (std::size_t index = 0; index < input.contexts.size(); ++index)
    {
        const FormRow row = ui.formRow(contexts, std::format("Context {}", index + 1));
        InputMapUi::Context made;
        made.name = ui.field(row.editor, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f},
                                                .offsetMax = {-right - ui.font * 0.4f, 0.0f}},
                             "", "name");
        made.active = ui.add(row.editor, "Active",
                             UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-right, -box * 0.5f}, .offsetMax = {-right + box, box * 0.5f}},
                             "toggle");
        ui.scene().add<scene::UiImage>(made.active);
        ui.scene().add<scene::UiToggle>(made.active);
        ui.scene().add<scene::UiButton>(made.active);
        ui.tooltip(made.active, "Whether the actions of the context are read when the game starts");
        ui.text(row.editor, UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-right + box + ui.font * 0.4f, 0.0f},
                                   .offsetMax = {-tool - ui.font * 0.6f, 0.0f}},
                "Active at start", "dim");
        made.remove = ui.toolButton(kit, row.editor, Icon::Trash, rightButton(tool, 0.0f));
        ui.tooltip(made.remove.entity, "Remove this context: its actions are then always read");
        map.contexts.push_back(made);
    }
    map.addContext = ui.action(kit, ui.actions(&contexts), Icon::Plus, "Add Context");

    const float directionWidth = std::round(ui.font * 7.5f);
    for (const asset::InputAction& action : input.actions)
    {
        InputMapUi::Action made;
        made.card = ui.sections.size();
        Section& card = ui.pageCard(kit, page, action.name.empty() ? std::string("(unnamed)") : action.name);
        made.name = ui.textField(ui.formRow(card, "Name").editor, "name");
        made.warning = ui.note(&card, "", "warning");
        const FormRow kindRow = ui.formRow(card, "Kind");
        made.kind = ui.choice(kindRow.editor, kindNames);
        ui.tooltip(kindRow.row, "A button is pressed or not; an axis goes from -1 to 1; a vector gives a direction");
        const FormRow contextRow = ui.formRow(card, "Context");
        made.context = ui.choice(contextRow.editor);
        ui.tooltip(contextRow.row, "The context whose actions the game turns on and off together");
        const bool directed = action.kind != InputActionKind::Button;
        if (directed)
        {
            const FormRow deadRow = ui.formRow(card, "Dead Zone");
            made.deadZone = ui.numbers(deadRow.editor, singleNumber, {.minValue = 0.0f, .maxValue = 0.9f, .dragSpeed = 0.005f, .decimals = 2}).front();
            ui.tooltip(deadRow.row, "Below this, the action reads zero; gamepads already ignore small moves of their sticks");
        }
        for (std::size_t index = 0; index < action.bindings.size(); ++index)
        {
            const FormRow row = ui.formRow(card, "Binding");
            InputMapUi::Binding binding;
            binding.device = row.label;
            const float room = tool + ui.gap * 2.0f + (directed ? directionWidth + ui.gap * 2.0f : 0.0f);
            binding.input = ui.button(kit, row.editor, Icon::Keyboard, "(none)", "button", -1.0f, ui.line - 4.0f, scene::TextAlign::Left);
            UiRect& rect = ui.scene().get<UiRect>(binding.input.entity);
            rect.anchorMax = {1.0f, 1.0f};
            rect.offsetMax = {-room, 0.0f};
            ui.tooltip(binding.input.entity, "Choose the key, the button or the axis, or press it");
            if (directed)
            {
                binding.direction = ui.choice(row.editor);
                placeRight(ui.scene(), binding.direction, tool + ui.gap * 2.0f, directionWidth);
            }
            binding.remove = ui.toolButton(kit, row.editor, Icon::Trash, rightButton(tool, 0.0f));
            ui.tooltip(binding.remove.entity, "Remove this binding");
            made.bindings.push_back(binding);
        }
        const Entity line = ui.actions(&card);
        made.add = ui.action(kit, line, Icon::Plus, "Add Binding");
        made.remove = ui.action(kit, line, Icon::Trash, "Remove Action");
        map.actions.push_back(std::move(made));
    }
}

void buildEventDialog(ProjectSettingsUi& ui, EditorUiKit& kit)
{
    InputMapUi& map = ui.input;
    const float spacing = std::round(ui.font * 0.55f);
    const float padding = std::round(ui.font * 1.2f);
    const math::Vec2 size{std::round(ui.font * 30.0f), std::round(ui.font * 32.0f)};
    map.dialog = ui.dialog("Event", size);
    map.dialogTitle = ui.text(map.dialog, wide(ui.line), "Binding", "text", true, scene::TextAlign::Left, std::round(ui.font * 1.1f));
    const float listenHeight = std::round(ui.line * 1.6f);
    map.listen = ui.add(map.dialog, "Listen", wide(listenHeight), "row");
    ui.scene().add<scene::UiImage>(map.listen);
    ui.scene().add<scene::UiButton>(map.listen);
    ui.tooltip(map.listen, "While it is lit, the next key or gamepad button pressed is the binding; Esc gives up");
    map.listenText = ui.text(map.listen, whole(math::Vec4{ui.font * 0.8f, 0.0f, ui.font * 0.8f, 0.0f}), "", "text", false, scene::TextAlign::Center);
    map.search = ui.searchField(kit, map.dialog, wide(ui.line), "", "Filter the keys, buttons and axes");
    const float buttons = ui.font * 2.0f;
    const float listHeight = size.y - padding * 2.0f - ui.line * 3.0f - listenHeight - buttons - spacing * 5.0f;
    map.sourceScroll = ui.add(map.dialog, "Sources", wide(listHeight), "list");
    ui.scene().add<scene::UiImage>(map.sourceScroll);
    ui.scene().add<scene::UiScroll>(map.sourceScroll, scene::UiScroll{.speed = ui.line * 3.0f});
    map.sourceList = ui.add(map.sourceScroll, "List",
                            UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-8.0f, 0.0f}});
    ui.scene().add<scene::UiLayout>(map.sourceList, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                                    .spacing = 1.0f,
                                                                    .padding = math::Vec4{ui.font * 0.3f},
                                                                    .align = scene::TextAlign::Left});
    map.directionRow = ui.add(map.dialog, "Direction", wide(ui.line));
    ui.text(map.directionRow, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {ui.font * 6.0f, 0.0f}},
            "Direction", "label");
    const Entity chooser = ui.add(map.directionRow, "Editor",
                                  UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {ui.font * 6.0f, 2.0f}, .offsetMax = {0.0f, -2.0f}});
    map.direction = ui.choice(chooser);
    ui.tooltip(map.directionRow, "Where the key or button pushes the action; for an axis, where its positive end pushes it");
    std::tie(map.confirm, map.cancel) = ui.dialogButtons(kit, map.dialog, Icon::Check, "OK", std::round(ui.font * 7.0f));
    map.target.reset();
}

void syncInputMap(ProjectSettingsUi& ui, ToolsState& state, EditorUiKit& kit)
{
    InputMapUi& map = ui.input;
    const asset::InputSettings& input = ui.project.input;
    for (std::size_t index = 0; index < map.contexts.size() && index < input.contexts.size(); ++index)
    {
        ui.setText(map.contexts[index].name, input.contexts[index].name);
        ui.scene().get<scene::UiToggle>(map.contexts[index].active).value = input.contexts[index].activeAtStart;
    }
    std::vector<std::string> contextNames{"(always read)"};
    for (const asset::InputContext& context : input.contexts)
    {
        contextNames.push_back(context.name);
    }
    for (std::size_t index = 0; index < map.actions.size() && index < input.actions.size(); ++index)
    {
        const InputMapUi::Action& made = map.actions[index];
        const asset::InputAction& action = input.actions[index];
        Section& card = ui.sections[made.card];
        ui.setText(made.name, action.name);
        const bool named = !action.name.empty() && std::ranges::count(input.actions, action.name, &asset::InputAction::name) == 1;
        ui.scene().get<scene::UiText>(made.warning).text = action.name.empty() ? "An action needs a name." : "Another action has this name.";
        ui.showLine(card, made.warning, !named);
        ui.setChoice(made.kind, kindNames, static_cast<std::int32_t>(action.kind));
        const auto context = std::ranges::find(input.contexts, action.context, &asset::InputContext::name);
        ui.setChoice(made.context, contextNames,
                     action.context.empty()           ? 0
                     : context != input.contexts.end() ? static_cast<std::int32_t>(context - input.contexts.begin()) + 1
                                                       : -1);
        ui.scene().get<scene::UiDropdown>(made.context).placeholder = std::format("{} (missing)", action.context);
        if (made.deadZone.isValid())
        {
            ui.setNumber(made.deadZone, action.deadZone);
        }
        const std::vector<std::string> directions = directionNames(action.kind);
        for (std::size_t binding = 0; binding < made.bindings.size() && binding < action.bindings.size(); ++binding)
        {
            const InputMapUi::Binding& shown = made.bindings[binding];
            const asset::InputBinding& bound = action.bindings[binding];
            ui.relabel(kit, shown.input, bindingLabel(state, bound.input));
            ui.scene().get<scene::UiText>(shown.device).text = deviceOf(bound.input);
            if (shown.direction.isValid())
            {
                const bool stick = wholeStick(action.kind, bound.input);
                const std::span<const InputDirection> allowed = directionsOf(action.kind);
                const auto found = std::ranges::find(allowed, bound.direction);
                ui.setChoice(shown.direction, stick ? std::vector<std::string>{} : directions,
                             stick || found == allowed.end() ? -1 : static_cast<std::int32_t>(found - allowed.begin()));
                scene::UiDropdown& dropdown = ui.scene().get<scene::UiDropdown>(shown.direction);
                dropdown.interactable = !stick;
                dropdown.placeholder = stick ? "Whole stick" : std::string(asset::toString(bound.direction));
                const std::optional<InputSource> source = platform::parseInputSource(bound.input);
                ui.tooltip(shown.direction, source && platform::isAnalog(*source) ? "Where the positive end of the axis pushes the action"
                                                                                  : "Where the key or button pushes the action");
            }
        }
        ui.scene().get<scene::UiText>(card.title).text = action.name.empty() ? std::string("(unnamed)") : action.name;
    }
    syncEventDialog(ui, state);
}

void answerInputMap(ProjectSettingsUi& ui, ToolsState& state, EditorUiKit& kit)
{
    InputMapUi& map = ui.input;
    asset::InputSettings& input = ui.project.input;
    const ui::UiWorld& world = ui.panel.world();
    const auto typedText = [&](Entity entity) -> const std::string* {
        return ui.endedField.isValid() && ui.endedField == entity ? &ui.scene().get<scene::UiText>(entity).text : nullptr;
    };
    const auto chosen = [&](Entity entity) -> std::optional<std::size_t> {
        if (!entity.isValid() || !world.wasChanged(entity))
        {
            return std::nullopt;
        }
        const std::int32_t index = ui.scene().get<scene::UiDropdown>(entity).selected;
        return index >= 0 ? std::optional(static_cast<std::size_t>(index)) : std::nullopt;
    };

    // Contexts, whose actions follow them under a new name.
    std::optional<std::size_t> removedContext;
    for (std::size_t index = 0; index < map.contexts.size() && index < input.contexts.size(); ++index)
    {
        const InputMapUi::Context& made = map.contexts[index];
        if (const std::string* const written = typedText(made.name); written != nullptr && *written != input.contexts[index].name)
        {
            const std::string previous = input.contexts[index].name;
            input.contexts[index].name = *written;
            for (asset::InputAction& action : input.actions)
            {
                action.context = action.context == previous ? *written : action.context;
            }
        }
        if (world.wasChanged(made.active))
        {
            input.contexts[index].activeAtStart = ui.scene().get<scene::UiToggle>(made.active).value;
        }
        if (world.wasClicked(made.remove.entity))
        {
            removedContext = index;
        }
    }
    if (removedContext)
    {
        const std::string name = input.contexts[*removedContext].name;
        input.contexts.erase(input.contexts.begin() + static_cast<std::ptrdiff_t>(*removedContext));
        for (asset::InputAction& action : input.actions)
        {
            action.context = action.context == name ? std::string() : action.context;
        }
    }
    if (world.wasClicked(map.addContext.entity))
    {
        input.contexts.push_back({.name = std::format("Context {}", input.contexts.size() + 1)});
    }

    // Actions and their bindings.
    std::optional<std::size_t> removedAction;
    std::optional<EventTarget> opened;
    for (std::size_t index = 0; index < map.actions.size() && index < input.actions.size(); ++index)
    {
        const InputMapUi::Action& made = map.actions[index];
        asset::InputAction& action = input.actions[index];
        if (const std::string* const written = typedText(made.name))
        {
            action.name = *written;
        }
        if (const std::optional<std::size_t> kind = chosen(made.kind); kind && *kind < kindNames.size())
        {
            action.kind = static_cast<InputActionKind>(*kind);
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
        if (const std::optional<std::size_t> context = chosen(made.context))
        {
            action.context = *context == 0 || *context > input.contexts.size() ? std::string() : input.contexts[*context - 1].name;
        }
        if (made.deadZone.isValid() && world.wasChanged(made.deadZone))
        {
            action.deadZone = ui.scene().get<scene::UiNumberField>(made.deadZone).value;
        }
        std::optional<std::size_t> removedBinding;
        for (std::size_t binding = 0; binding < made.bindings.size() && binding < action.bindings.size(); ++binding)
        {
            const InputMapUi::Binding& shown = made.bindings[binding];
            if (world.wasClicked(shown.input.entity))
            {
                opened = EventTarget{.action = index, .binding = binding};
            }
            if (const std::optional<std::size_t> direction = chosen(shown.direction); direction && *direction < directionsOf(action.kind).size())
            {
                action.bindings[binding].direction = directionsOf(action.kind)[*direction];
            }
            if (world.wasClicked(shown.remove.entity))
            {
                removedBinding = binding;
            }
        }
        if (removedBinding)
        {
            action.bindings.erase(action.bindings.begin() + static_cast<std::ptrdiff_t>(*removedBinding));
        }
        if (world.wasClicked(made.add.entity))
        {
            opened = EventTarget{.action = index, .binding = action.bindings.size()};
        }
        if (world.wasClicked(made.remove.entity))
        {
            removedAction = index;
        }
    }
    if (removedAction)
    {
        input.actions.erase(input.actions.begin() + static_cast<std::ptrdiff_t>(*removedAction));
        opened.reset();
    }
    if (world.wasClicked(map.addAction.entity))
    {
        input.actions.push_back({
            .name = std::format("Action {}", input.actions.size() + 1),
            .context = input.contexts.empty() ? std::string() : input.contexts.front().name,
        });
    }
    answerEventDialog(ui, state);
    if (opened)
    {
        openEventDialog(ui, state, kit, *opened);
    }
}

} // namespace devex::tools::detail
