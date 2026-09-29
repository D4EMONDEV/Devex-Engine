#include "InspectorUi.hpp"

#include <devex/animation/AnimationWorld.hpp>
#include <devex/asset/import/AnimatorFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/AnimationComponents.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace devex::tools::detail {
namespace {

using asset::AnimatorBlend;
using asset::AnimatorData;
using asset::AnimatorParameterType;
using asset::AnimatorState;
using asset::AnimatorTest;
using asset::AnimatorTransition;

// The size of nodes in the graph, in units that are pixels at a zoom of 1 with a font of 16 pixels.
constexpr ImVec2 stateSize{170.0f, 48.0f};
constexpr ImVec2 markerSize{110.0f, 30.0f};
constexpr std::size_t maxUndo = 200;

[[nodiscard]] std::filesystem::file_time_type writeTime(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    return error ? std::filesystem::file_time_type{} : time;
}

[[nodiscard]] ImVec2 toImVec(math::Vec2 value) noexcept
{
    return {value.x, value.y};
}

[[nodiscard]] math::Vec2 fromImVec(ImVec2 value) noexcept
{
    return {value.x, value.y};
}

[[nodiscard]] float length(ImVec2 value) noexcept
{
    return std::sqrt(value.x * value.x + value.y * value.y);
}

// The entity whose Animator the panel follows: the selected one, or its closest ancestor with one.
[[nodiscard]] scene::Entity animatorOf(const scene::Scene& scene, scene::Entity entity)
{
    for (scene::Entity candidate = entity; candidate.isValid(); candidate = scene.parent(candidate))
    {
        if (scene.has<scene::Animator>(candidate))
        {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] bool isNumber(AnimatorParameterType type) noexcept
{
    return type == AnimatorParameterType::Float || type == AnimatorParameterType::Integer;
}

// A name that no state, or no parameter, has yet: the base, then the base and a number.
template <typename Items, typename Projection>
[[nodiscard]] std::string uniqueName(const Items& items, Projection name, std::string_view base)
{
    for (int number = 1;; ++number)
    {
        const std::string candidate = number == 1 ? std::string(base) : std::format("{} {}", base, number);
        if (std::ranges::none_of(items, [&](const auto& item) { return name(item) == candidate; }))
        {
            return candidate;
        }
    }
}

[[nodiscard]] std::string stateName(const AnimatorState& state)
{
    return state.name;
}

[[nodiscard]] std::string parameterName(const asset::AnimatorParameter& parameter)
{
    return parameter.name;
}

void renameState(AnimatorData& animator, const std::string& from, const std::string& to)
{
    for (AnimatorState& state : animator.states)
    {
        if (state.name == from)
        {
            state.name = to;
        }
    }
    for (AnimatorTransition& transition : animator.transitions)
    {
        transition.from = transition.from == from ? to : transition.from;
        transition.to = transition.to == from ? to : transition.to;
    }
    animator.entry = animator.entry == from ? to : animator.entry;
}

void renameParameter(AnimatorData& animator, const std::string& from, const std::string& to)
{
    for (asset::AnimatorParameter& parameter : animator.parameters)
    {
        parameter.name = parameter.name == from ? to : parameter.name;
    }
    for (AnimatorState& state : animator.states)
    {
        state.parameter = state.parameter == from ? to : state.parameter;
        state.parameterY = state.parameterY == from ? to : state.parameterY;
        state.speedParameter = state.speedParameter == from ? to : state.speedParameter;
    }
    for (AnimatorTransition& transition : animator.transitions)
    {
        for (asset::AnimatorCondition& condition : transition.conditions)
        {
            condition.parameter = condition.parameter == from ? to : condition.parameter;
        }
    }
}

// Removes a state, the transitions that reach or leave it, and moves the entry elsewhere.
void removeState(AnimatorData& animator, std::size_t index)
{
    const std::string name = animator.states[index].name;
    std::erase_if(animator.transitions, [&](const AnimatorTransition& transition) {
        return transition.from == name || transition.to == name;
    });
    animator.states.erase(animator.states.begin() + static_cast<std::ptrdiff_t>(index));
    if (animator.entry == name)
    {
        animator.entry = animator.states.empty() ? std::string{} : animator.states.front().name;
    }
}

// Removes a parameter and the conditions on it; blends and speeds on it are left for the user to
// point elsewhere.
void removeParameter(AnimatorData& animator, std::size_t index)
{
    const std::string name = animator.parameters[index].name;
    animator.parameters.erase(animator.parameters.begin() + static_cast<std::ptrdiff_t>(index));
    for (AnimatorTransition& transition : animator.transitions)
    {
        std::erase_if(transition.conditions, [&](const asset::AnimatorCondition& condition) { return condition.parameter == name; });
    }
    for (AnimatorState& state : animator.states)
    {
        state.speedParameter = state.speedParameter == name ? std::string{} : state.speedParameter;
    }
}

// The first parameter that holds a number, made when there is none.
[[nodiscard]] std::string numberParameter(AnimatorData& animator, std::size_t skip, std::string_view fallback)
{
    std::size_t seen = 0;
    for (const asset::AnimatorParameter& parameter : animator.parameters)
    {
        if (isNumber(parameter.type) && seen++ == skip)
        {
            return parameter.name;
        }
    }
    const std::string name = uniqueName(animator.parameters, parameterName, fallback);
    animator.parameters.push_back({.name = name});
    return name;
}

[[nodiscard]] const char* parameterTypeLabel(AnimatorParameterType type) noexcept
{
    switch (type)
    {
    case AnimatorParameterType::Float:
        return "Float";
    case AnimatorParameterType::Integer:
        return "Int";
    case AnimatorParameterType::Bool:
        return "Bool";
    case AnimatorParameterType::Trigger:
        return "Trigger";
    }
    return "?";
}

[[nodiscard]] const char* testLabel(AnimatorTest test) noexcept
{
    switch (test)
    {
    case AnimatorTest::Greater:
        return "Greater";
    case AnimatorTest::Less:
        return "Less";
    case AnimatorTest::Equals:
        return "Equals";
    case AnimatorTest::NotEquals:
        return "Not Equal";
    case AnimatorTest::IsTrue:
        return "Is True";
    case AnimatorTest::IsFalse:
        return "Is False";
    case AnimatorTest::Triggered:
        return "Is Set";
    }
    return "?";
}

[[nodiscard]] AnimatorTest firstFittingTest(AnimatorParameterType type) noexcept
{
    switch (type)
    {
    case AnimatorParameterType::Float:
    case AnimatorParameterType::Integer:
        return AnimatorTest::Greater;
    case AnimatorParameterType::Bool:
        return AnimatorTest::IsTrue;
    case AnimatorParameterType::Trigger:
        return AnimatorTest::Triggered;
    }
    return AnimatorTest::Greater;
}

// What a state plays, in a few words, under its name in the graph.
[[nodiscard]] std::string motionSummary(const ToolsState& state, const AnimatorState& node)
{
    const auto clipName = [&](asset::AssetId clip) {
        const asset::AssetInfo* const info = state.database != nullptr && clip.isValid() ? state.database->find(clip) : nullptr;
        return info != nullptr ? info->name : std::string("no clip");
    };
    switch (node.blend)
    {
    case AnimatorBlend::Linear:
        return std::format("Blend 1D: {}", node.parameter);
    case AnimatorBlend::Planar:
        return std::format("Blend 2D: {}, {}", node.parameter, node.parameterY);
    case AnimatorBlend::None:
        break;
    }
    if (!node.motions.empty() && node.motions.front().clip.isValid())
    {
        return clipName(node.motions.front().clip);
    }
    return node.spriteAnimation.empty() ? std::string("no motion") : std::format("Sprite: {}", node.spriteAnimation);
}

// A condition in a few words: "Speed > 0.5", "Grounded", "!Grounded", "Jump".
[[nodiscard]] std::string conditionText(const asset::AnimatorCondition& condition)
{
    switch (condition.test)
    {
    case AnimatorTest::Greater:
        return std::format("{} > {:g}", condition.parameter, condition.value);
    case AnimatorTest::Less:
        return std::format("{} < {:g}", condition.parameter, condition.value);
    case AnimatorTest::Equals:
        return std::format("{} = {:g}", condition.parameter, condition.value);
    case AnimatorTest::NotEquals:
        return std::format("{} != {:g}", condition.parameter, condition.value);
    case AnimatorTest::IsTrue:
    case AnimatorTest::Triggered:
        return condition.parameter;
    case AnimatorTest::IsFalse:
        return "!" + condition.parameter;
    }
    return condition.parameter;
}

[[nodiscard]] std::string transitionText(const AnimatorTransition& transition)
{
    std::string text;
    for (const asset::AnimatorCondition& condition : transition.conditions)
    {
        text += (text.empty() ? "" : ", ") + conditionText(condition);
    }
    if (transition.exitTime >= 0.0f || transition.conditions.empty())
    {
        text += std::format("{}exit {:g}", text.empty() ? "" : ", ", transition.exitTime >= 0.0f ? transition.exitTime : 1.0f);
    }
    return text;
}

// ---- The file ----

// Reads the controller from its file when another one is edited, or when the file changed outside
// the editor.
void loadAnimator(ToolsState& state, asset::AssetId id)
{
    AnimatorEditor& editor = state.animatorEditor;
    const std::optional<asset::SourceFile> source = state.database->sourceOf(id);
    const std::optional<std::filesystem::path> file =
        source ? state.database->project().absolutePath(source->path) : std::nullopt;
    if (!file)
    {
        return;
    }
    const std::filesystem::file_time_type time = writeTime(*file);
    const bool sameAsset = editor.asset == id;
    if ((sameAsset && editor.file == *file && editor.fileTime == time) || (sameAsset && editor.dragged != AnimatorElement::None))
    {
        return;
    }
    editor.asset = id;
    editor.file = *file;
    editor.fileTime = time;
    editor.error.clear();
    const core::Result<std::string> text = core::readTextFile(*file);
    core::Result<AnimatorData> animator =
        text ? asset::parseAnimatorFile(*text) : core::Result<AnimatorData>(std::unexpected(text.error()));
    if (!animator)
    {
        editor.error = std::format("The file could not be read: {}", animator.error().message);
        editor.animator = {};
    }
    else
    {
        editor.animator = std::move(*animator);
    }
    editor.saved = editor.animator;
    if (!sameAsset)
    {
        editor.undo.clear();
        editor.redo.clear();
        editor.selected = AnimatorElement::None;
        editor.index = -1;
        editor.inspecting = false;
        editor.connectingFrom.reset();
        editor.frame = true;
    }
}

void writeAnimator(ToolsState& state)
{
    AnimatorEditor& editor = state.animatorEditor;
    if (core::Result<void> written = core::writeTextFile(editor.file, asset::writeAnimatorFile(editor.animator)); !written)
    {
        editor.error = std::format("Not saved: {}", written.error().message);
        return;
    }
    editor.saved = editor.animator;
    editor.fileTime = writeTime(editor.file);
    editor.error.clear();
    if (core::Result<void> queued = state.database->reimport(editor.asset); !queued)
    {
        DEVEX_LOG_WARNING("{}", queued.error());
    }
}

// Saves a change once it is over, as one step of the undo history of the panel. A controller that
// does not hold together stays unsaved, with the reason shown, until it does.
void commit(ToolsState& state)
{
    AnimatorEditor& editor = state.animatorEditor;
    if (editor.animator == editor.saved)
    {
        return;
    }
    if (core::Result<void> valid = asset::validate(editor.animator); !valid)
    {
        editor.error = std::format("Not saved: {}", valid.error().message);
        return;
    }
    editor.undo.push_back(editor.saved);
    if (editor.undo.size() > maxUndo)
    {
        editor.undo.erase(editor.undo.begin());
    }
    editor.redo.clear();
    writeAnimator(state);
}

// Keeps the selection on something that still exists.
void clampSelection(AnimatorEditor& editor)
{
    const std::size_t count = editor.selected == AnimatorElement::State        ? editor.animator.states.size()
                              : editor.selected == AnimatorElement::Transition ? editor.animator.transitions.size()
                                                                               : 1;
    if (editor.index < 0 || static_cast<std::size_t>(editor.index) >= count)
    {
        if (editor.selected == AnimatorElement::State || editor.selected == AnimatorElement::Transition)
        {
            editor.selected = AnimatorElement::None;
            editor.index = -1;
            editor.inspecting = false;
        }
    }
}

void undoAnimator(ToolsState& state)
{
    AnimatorEditor& editor = state.animatorEditor;
    if (editor.undo.empty())
    {
        return;
    }
    editor.redo.push_back(editor.animator);
    editor.animator = std::move(editor.undo.back());
    editor.undo.pop_back();
    writeAnimator(state);
    clampSelection(editor);
}

void redoAnimator(ToolsState& state)
{
    AnimatorEditor& editor = state.animatorEditor;
    if (editor.redo.empty())
    {
        return;
    }
    editor.undo.push_back(editor.animator);
    editor.animator = std::move(editor.redo.back());
    editor.redo.pop_back();
    writeAnimator(state);
    clampSelection(editor);
}

void select(ToolsState& state, AnimatorElement element, std::int32_t index)
{
    AnimatorEditor& editor = state.animatorEditor;
    editor.selected = element;
    editor.index = index;
    // The inspector shows it, until another entity or asset is chosen.
    editor.inspecting = element != AnimatorElement::None;
    editor.inspectedEntity = state.selection.active();
    editor.inspectedAsset = state.selectedAsset;
}

void deleteSelection(ToolsState& state)
{
    AnimatorEditor& editor = state.animatorEditor;
    if (editor.selected == AnimatorElement::State && editor.index >= 0)
    {
        removeState(editor.animator, static_cast<std::size_t>(editor.index));
    }
    else if (editor.selected == AnimatorElement::Transition && editor.index >= 0)
    {
        editor.animator.transitions.erase(editor.animator.transitions.begin() + editor.index);
    }
    else
    {
        return;
    }
    select(state, AnimatorElement::None, -1);
    commit(state);
}

// Adds a state at a place of the graph, the entry state when it is the first one, and selects it.
void addState(ToolsState& state, math::Vec2 position, AnimatorBlend blend, asset::AssetId clip = {},
              std::string_view name = {})
{
    AnimatorData& animator = state.animatorEditor.animator;
    AnimatorState added{.name = uniqueName(animator.states, stateName,
                                            !name.empty() ? name : blend == AnimatorBlend::None ? "New State" : "Blend Tree"),
                        .blend = blend,
                        .graphPosition = position};
    if (clip.isValid())
    {
        added.motions.push_back({.clip = clip});
    }
    if (blend != AnimatorBlend::None)
    {
        added.parameter = numberParameter(animator, 0, blend == AnimatorBlend::Linear ? "Speed" : "X");
    }
    if (blend == AnimatorBlend::Planar)
    {
        added.parameterY = numberParameter(animator, 1, "Y");
        added.motions = {{.position = {0.0f, 0.0f}}, {.position = {0.0f, 1.0f}}};
    }
    else if (blend == AnimatorBlend::Linear)
    {
        added.motions = {{.threshold = 0.0f}, {.threshold = 1.0f}};
    }
    if (animator.states.empty())
    {
        animator.entry = added.name;
    }
    animator.states.push_back(std::move(added));
    select(state, AnimatorElement::State, static_cast<std::int32_t>(animator.states.size() - 1));
    commit(state);
}

// ---- The parameters ----

void drawParameters(ToolsState& state, const std::optional<animation::AnimatorStatus>& status, scene::Entity live)
{
    AnimatorEditor& editor = state.animatorEditor;
    AnimatorData& animator = editor.animator;
    const ThemeColors& colors = themeColors();
    ImGui::AlignTextToFramePadding();
    boldText("Parameters");
    ImGui::SameLine();
    alignRight(toolButtonWidth());
    if (toolButton("add parameter", icons::Plus, "Add a parameter that game code sets"))
    {
        ImGui::OpenPopup("new parameter");
    }
    if (ImGui::BeginPopup("new parameter"))
    {
        for (const AnimatorParameterType type : {AnimatorParameterType::Float, AnimatorParameterType::Integer,
                                                 AnimatorParameterType::Bool, AnimatorParameterType::Trigger})
        {
            if (ImGui::MenuItem(parameterTypeLabel(type)))
            {
                animator.parameters.push_back(
                    {.name = uniqueName(animator.parameters, parameterName, std::format("New {}", parameterTypeLabel(type))),
                     .type = type});
                commit(state);
            }
        }
        ImGui::EndPopup();
    }
    if (status)
    {
        ImGui::TextColored(uiColor(colors.success), "Live: values of the game");
    }
    ImGui::Separator();

    std::optional<std::size_t> removed;
    const float valueWidth = ImGui::GetFontSize() * 4.5f;
    for (std::size_t index = 0; index < animator.parameters.size(); ++index)
    {
        asset::AnimatorParameter& parameter = animator.parameters[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%c", parameterTypeLabel(parameter.type)[0]);
        ImGui::SetItemTooltip("%s", parameterTypeLabel(parameter.type));
        ImGui::SameLine();

        // The name, renamed everywhere once typed.
        std::string name = parameter.name;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - valueWidth - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
            (ImGui::IsItemDeactivatedAfterEdit() && name != parameter.name))
        {
            if (!name.empty() && animator.findParameter(name) == nullptr)
            {
                renameParameter(animator, parameter.name, name);
                commit(state);
            }
        }
        if (ImGui::BeginPopupContextItem("parameter menu"))
        {
            if (ImGui::MenuItem("Remove"))
            {
                removed = index;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(valueWidth);

        // The value: the one it starts with, or the one the game gives it while it plays.
        const auto liveValue = [&]() -> std::optional<float> {
            if (!status)
            {
                return std::nullopt;
            }
            const auto found = std::ranges::find(status->parameters, parameter.name, &animation::AnimatorStatus::Parameter::name);
            return found != status->parameters.end() ? std::optional(found->value) : std::nullopt;
        }();
        if (liveValue && state.animationWorld != nullptr)
        {
            float value = *liveValue;
            switch (parameter.type)
            {
            case AnimatorParameterType::Float:
                if (ImGui::DragFloat("##value", &value, 0.01f))
                {
                    state.animationWorld->setFloat(live, parameter.name, value);
                }
                break;
            case AnimatorParameterType::Integer: {
                int whole = static_cast<int>(std::lround(value));
                if (ImGui::DragInt("##value", &whole))
                {
                    state.animationWorld->setInteger(live, parameter.name, whole);
                }
                break;
            }
            case AnimatorParameterType::Bool: {
                bool flag = value != 0.0f;
                if (ImGui::Checkbox("##value", &flag))
                {
                    state.animationWorld->setBool(live, parameter.name, flag);
                }
                break;
            }
            case AnimatorParameterType::Trigger:
                if (ImGui::Button(value != 0.0f ? "Set##trigger" : "Fire##trigger", ImVec2(valueWidth, 0.0f)))
                {
                    state.animationWorld->setTrigger(live, parameter.name);
                }
                break;
            }
        }
        else
        {
            switch (parameter.type)
            {
            case AnimatorParameterType::Float:
                ImGui::DragFloat("##default", &parameter.defaultValue, 0.01f);
                break;
            case AnimatorParameterType::Integer: {
                int whole = static_cast<int>(std::lround(parameter.defaultValue));
                if (ImGui::DragInt("##default", &whole))
                {
                    parameter.defaultValue = static_cast<float>(whole);
                }
                break;
            }
            case AnimatorParameterType::Bool: {
                bool flag = parameter.defaultValue != 0.0f;
                if (ImGui::Checkbox("##default", &flag))
                {
                    parameter.defaultValue = flag ? 1.0f : 0.0f;
                }
                break;
            }
            case AnimatorParameterType::Trigger:
                ImGui::Dummy(ImVec2(valueWidth, ImGui::GetFrameHeight()));
                break;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() || (parameter.type == AnimatorParameterType::Bool && ImGui::IsItemEdited()))
            {
                commit(state);
            }
            ImGui::SetItemTooltip("The value it starts with");
        }
        ImGui::PopID();
    }
    if (animator.parameters.empty())
    {
        ImGui::TextWrapped("Game code sets parameters, and transitions check them: add one with +.");
    }
    if (removed)
    {
        removeParameter(animator, *removed);
        commit(state);
    }
}

// ---- The graph ----

struct Graph
{
    ImVec2 origin;
    float scale = 1.0f;

    [[nodiscard]] ImVec2 toScreen(math::Vec2 point) const noexcept
    {
        return origin + toImVec(point) * scale;
    }

    [[nodiscard]] math::Vec2 toGraph(ImVec2 point) const noexcept
    {
        return fromImVec((point - origin) / scale);
    }
};

struct Node
{
    AnimatorElement element = AnimatorElement::None;
    std::int32_t index = -1;
    ImVec2 center;
    ImVec2 half;

    [[nodiscard]] bool contains(ImVec2 point) const noexcept
    {
        return std::abs(point.x - center.x) <= half.x && std::abs(point.y - center.y) <= half.y;
    }

    // Where a line from the center towards a direction leaves the node.
    [[nodiscard]] ImVec2 edge(ImVec2 direction) const noexcept
    {
        const float x = std::abs(direction.x) > 1e-6f ? half.x / std::abs(direction.x) : FLT_MAX;
        const float y = std::abs(direction.y) > 1e-6f ? half.y / std::abs(direction.y) : FLT_MAX;
        return center + direction * std::min(x, y);
    }
};

struct Arrow
{
    std::int32_t transition = -1;
    ImVec2 start;
    ImVec2 end;
};

[[nodiscard]] float distanceToSegment(ImVec2 point, ImVec2 start, ImVec2 end) noexcept
{
    const ImVec2 segment = end - start;
    const float length2 = segment.x * segment.x + segment.y * segment.y;
    const float along = length2 > 0.0f ? std::clamp(((point.x - start.x) * segment.x + (point.y - start.y) * segment.y) / length2, 0.0f, 1.0f) : 0.0f;
    return length(point - (start + segment * along));
}

void drawArrow(ImDrawList& draw, ImVec2 start, ImVec2 end, ImU32 color, float thickness, float scale)
{
    draw.AddLine(start, end, color, thickness);
    const ImVec2 direction = end - start;
    const float size = length(direction);
    if (size < 1.0f)
    {
        return;
    }
    const ImVec2 along = direction / size;
    const ImVec2 across{-along.y, along.x};
    const ImVec2 tip = start + direction * 0.5f + along * 6.0f * scale;
    const float head = 7.0f * scale;
    draw.AddTriangleFilled(tip, tip - along * head * 1.6f + across * head, tip - along * head * 1.6f - across * head, color);
}

// Where the transitions of the controller run, from node to node. Two transitions that join the same
// states both ways sit side by side.
[[nodiscard]] std::vector<Arrow> arrowsOf(const AnimatorData& animator, const std::vector<Node>& nodes, float scale)
{
    std::vector<Arrow> arrows;
    const auto nodeOf = [&](const std::string& name, bool any) -> const Node* {
        for (const Node& node : nodes)
        {
            if ((any && node.element == AnimatorElement::AnyState) ||
                (!any && node.element == AnimatorElement::State &&
                 animator.states[static_cast<std::size_t>(node.index)].name == name))
            {
                return &node;
            }
        }
        return nullptr;
    };
    for (std::size_t index = 0; index < animator.transitions.size(); ++index)
    {
        const AnimatorTransition& transition = animator.transitions[index];
        const Node* const from = nodeOf(transition.from, transition.from.empty());
        const Node* const to = nodeOf(transition.to, false);
        if (from == nullptr || to == nullptr || from == to)
        {
            continue;
        }
        ImVec2 direction = to->center - from->center;
        const float size = length(direction);
        if (size < 1.0f)
        {
            continue;
        }
        direction = direction / size;
        const ImVec2 side = ImVec2(-direction.y, direction.x) * 6.0f * scale;
        const Node shiftedFrom{from->element, from->index, from->center + side, from->half};
        const Node shiftedTo{to->element, to->index, to->center + side, to->half};
        arrows.push_back({static_cast<std::int32_t>(index), shiftedFrom.edge(direction), shiftedTo.edge(direction * -1.0f)});
    }
    return arrows;
}

void drawGraph(ToolsState& state, const std::optional<animation::AnimatorStatus>& status)
{
    AnimatorEditor& editor = state.animatorEditor;
    AnimatorData& animator = editor.animator;
    const ThemeColors& colors = themeColors();
    const ImGuiIO& io = ImGui::GetIO();

    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImMax(ImGui::GetContentRegionAvail(), ImVec2(50.0f, 50.0f));
    const ImVec2 canvasMax = canvasMin + canvasSize;
    ImGui::InvisibleButton("canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (editor.frame)
    {
        // Every node in view, no larger than at a zoom of 1.
        math::Vec2 low = animator.entryPosition - fromImVec(markerSize * 0.5f);
        math::Vec2 high = animator.entryPosition + fromImVec(markerSize * 0.5f);
        const auto include = [&](math::Vec2 center, ImVec2 size) {
            low = math::min(low, center - fromImVec(size * 0.5f));
            high = math::max(high, center + fromImVec(size * 0.5f));
        };
        include(animator.anyStatePosition, markerSize);
        for (const AnimatorState& node : animator.states)
        {
            include(node.graphPosition, stateSize);
        }
        const float unit = ImGui::GetFontSize() / 16.0f;
        const math::Vec2 extent = (high - low) + math::Vec2{60.0f, 60.0f};
        editor.zoom = std::clamp(std::min(canvasSize.x / (extent.x * unit), canvasSize.y / (extent.y * unit)), 0.35f, 1.0f);
        editor.pan = fromImVec(canvasSize * 0.5f) - (low + high) * 0.5f * (editor.zoom * unit);
        editor.frame = false;
    }
    const Graph graph{canvasMin + toImVec(editor.pan), editor.zoom * ImGui::GetFontSize() / 16.0f};

    // Clips dropped on the graph become states; on a state, its clip.
    std::optional<asset::AssetId> droppedClip = acceptDroppedAsset(asset::AssetType::AnimationClip);

    // The nodes, where they are drawn now.
    std::vector<Node> nodes;
    nodes.push_back({AnimatorElement::Entry, -1, graph.toScreen(animator.entryPosition), markerSize * 0.5f * graph.scale});
    nodes.push_back({AnimatorElement::AnyState, -1, graph.toScreen(animator.anyStatePosition), markerSize * 0.5f * graph.scale});
    for (std::size_t index = 0; index < animator.states.size(); ++index)
    {
        nodes.push_back({AnimatorElement::State, static_cast<std::int32_t>(index), graph.toScreen(animator.states[index].graphPosition),
                         stateSize * 0.5f * graph.scale});
    }
    const std::vector<Arrow> arrows = arrowsOf(animator, nodes, graph.scale);
    const ImVec2 mouse = io.MousePos;
    const Node* hoveredNode = nullptr;
    for (const Node& node : nodes)
    {
        hoveredNode = hovered && node.contains(mouse) ? &node : hoveredNode;
    }
    const Arrow* hoveredArrow = nullptr;
    if (hovered && hoveredNode == nullptr)
    {
        for (const Arrow& arrow : arrows)
        {
            if (distanceToSegment(mouse, arrow.start, arrow.end) < 6.0f)
            {
                hoveredArrow = &arrow;
            }
        }
    }

    if (droppedClip)
    {
        if (hoveredNode != nullptr && hoveredNode->element == AnimatorElement::State)
        {
            AnimatorState& target = animator.states[static_cast<std::size_t>(hoveredNode->index)];
            if (target.blend == AnimatorBlend::None && !target.motions.empty())
            {
                target.motions.front().clip = *droppedClip;
            }
            else
            {
                target.motions.push_back({.clip = *droppedClip, .threshold = target.motions.empty() ? 0.0f : target.motions.back().threshold + 1.0f});
            }
            select(state, AnimatorElement::State, hoveredNode->index);
            commit(state);
        }
        else
        {
            const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(*droppedClip) : nullptr;
            addState(state, graph.toGraph(mouse), AnimatorBlend::None, *droppedClip, info != nullptr ? info->name : "New State");
        }
    }

    // Selection, moves, transitions drawn from a state.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (editor.connectingFrom)
        {
            if (hoveredNode != nullptr && hoveredNode->element == AnimatorElement::State &&
                hoveredNode->index != *editor.connectingFrom)
            {
                animator.transitions.push_back({
                    .from = *editor.connectingFrom >= 0 ? animator.states[static_cast<std::size_t>(*editor.connectingFrom)].name : std::string{},
                    .to = animator.states[static_cast<std::size_t>(hoveredNode->index)].name,
                });
                select(state, AnimatorElement::Transition, static_cast<std::int32_t>(animator.transitions.size() - 1));
                commit(state);
            }
            editor.connectingFrom.reset();
        }
        else if (hoveredNode != nullptr)
        {
            select(state, hoveredNode->element, hoveredNode->index);
            editor.dragged = hoveredNode->element;
            editor.draggedIndex = hoveredNode->index;
        }
        else if (hoveredArrow != nullptr)
        {
            select(state, AnimatorElement::Transition, hoveredArrow->transition);
        }
        else
        {
            select(state, AnimatorElement::None, -1);
        }
    }
    if (editor.dragged != AnimatorElement::None)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const math::Vec2 moved = fromImVec(io.MouseDelta / graph.scale);
            if (editor.dragged == AnimatorElement::Entry)
            {
                animator.entryPosition += moved;
            }
            else if (editor.dragged == AnimatorElement::AnyState)
            {
                animator.anyStatePosition += moved;
            }
            else if (editor.draggedIndex >= 0 && static_cast<std::size_t>(editor.draggedIndex) < animator.states.size())
            {
                animator.states[static_cast<std::size_t>(editor.draggedIndex)].graphPosition += moved;
            }
        }
        else
        {
            editor.dragged = AnimatorElement::None;
            commit(state);
        }
    }
    // The view moves with the middle or the right button, and zooms around the mouse.
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
    {
        editor.pan += fromImVec(io.MouseDelta);
        editor.panning = true;
    }
    if (hovered && io.MouseWheel != 0.0f)
    {
        const math::Vec2 under = graph.toGraph(mouse);
        editor.zoom = std::clamp(editor.zoom * std::pow(1.15f, io.MouseWheel), 0.35f, 2.5f);
        const float scale = editor.zoom * ImGui::GetFontSize() / 16.0f;
        editor.pan = fromImVec(mouse - canvasMin) - under * scale;
    }
    // A right click that did not move the view opens the menu of what it is on.
    static AnimatorElement menuElement = AnimatorElement::None;
    static std::int32_t menuIndex = -1;
    static math::Vec2 menuPosition{0.0f};
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        if (!editor.panning && !editor.connectingFrom)
        {
            menuElement = hoveredNode != nullptr ? hoveredNode->element : hoveredArrow != nullptr ? AnimatorElement::Transition : AnimatorElement::None;
            menuIndex = hoveredNode != nullptr ? hoveredNode->index : hoveredArrow != nullptr ? hoveredArrow->transition : -1;
            menuPosition = graph.toGraph(mouse);
            if (menuElement != AnimatorElement::None)
            {
                select(state, menuElement, menuIndex);
            }
            ImGui::OpenPopup("graph menu");
        }
        editor.connectingFrom.reset();
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        editor.panning = false;
    }

    // Drawing: a grid, the transitions, the nodes, and the transition being drawn.
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    draw.PushClipRect(canvasMin, canvasMax, true);
    draw.AddRectFilled(canvasMin, canvasMax, uiColorU32(colors.outer));
    const float step = 32.0f * graph.scale;
    const ImU32 gridColor = uiColorU32(ImVec4(colors.border.x, colors.border.y, colors.border.z, 0.16f));
    for (float x = std::fmod(graph.origin.x - canvasMin.x, step); x < canvasSize.x; x += step)
    {
        draw.AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), gridColor);
    }
    for (float y = std::fmod(graph.origin.y - canvasMin.y, step); y < canvasSize.y; y += step)
    {
        draw.AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), gridColor);
    }

    const std::string current = status ? status->state : std::string{};
    const std::string previous = status ? status->previousState : std::string{};
    for (const Arrow& arrow : arrows)
    {
        const AnimatorTransition& transition = animator.transitions[static_cast<std::size_t>(arrow.transition)];
        const bool selected = editor.selected == AnimatorElement::Transition && editor.index == arrow.transition;
        const bool running = !previous.empty() && transition.to == current && (transition.from == previous || transition.from.empty());
        const ImVec4 color = selected ? colors.accent : running ? colors.success : &arrow == hoveredArrow ? colors.text : colors.textDim;
        drawArrow(draw, arrow.start, arrow.end, uiColorU32(color), (selected || running ? 2.5f : 1.5f) * graph.scale, graph.scale);
    }
    // Entry points at the entry state.
    if (const AnimatorState* const entry = animator.findState(animator.entry))
    {
        const Node from = nodes[0];
        const Node to{AnimatorElement::State, -1, graph.toScreen(entry->graphPosition), stateSize * 0.5f * graph.scale};
        ImVec2 direction = to.center - from.center;
        if (length(direction) > 1.0f)
        {
            direction = direction / length(direction);
            drawArrow(draw, from.edge(direction), to.edge(direction * -1.0f), uiColorU32(colors.warning), 1.5f * graph.scale, graph.scale);
        }
    }

    ImFont* const bold = editorFonts().bold;
    const float fontSize = ImGui::GetFontSize() * editor.zoom;
    const float rounding = 6.0f * graph.scale;
    for (const Node& node : nodes)
    {
        const ImVec2 low = node.center - node.half;
        const ImVec2 high = node.center + node.half;
        const bool selected = editor.selected == node.element && (node.element != AnimatorElement::State || editor.index == node.index);
        std::string title;
        std::string subtitle;
        ImVec4 fill = colors.panel;
        ImVec4 border = colors.border;
        float progress = -1.0f;
        if (node.element == AnimatorElement::Entry)
        {
            title = "Entry";
            fill = ImVec4(colors.warning.x * 0.45f, colors.warning.y * 0.45f, colors.warning.z * 0.45f, 1.0f);
        }
        else if (node.element == AnimatorElement::AnyState)
        {
            title = "Any State";
            fill = ImVec4(colors.animation.x * 0.4f, colors.animation.y * 0.4f, colors.animation.z * 0.4f, 1.0f);
        }
        else
        {
            const AnimatorState& animatorState = animator.states[static_cast<std::size_t>(node.index)];
            title = animatorState.name;
            subtitle = motionSummary(state, animatorState);
            if (animatorState.name == animator.entry)
            {
                border = colors.warning;
            }
            if (animatorState.name == current)
            {
                fill = ImVec4(colors.success.x * 0.35f, colors.success.y * 0.35f, colors.success.z * 0.35f, 1.0f);
                progress = animatorState.loop ? status->normalizedTime - std::floor(status->normalizedTime)
                                              : std::min(status->normalizedTime, 1.0f);
            }
        }
        if (selected)
        {
            border = colors.accent;
        }
        draw.AddRectFilled(low, high, uiColorU32(fill), rounding);
        draw.AddRect(low, high, uiColorU32(border), rounding, 0, (selected ? 2.5f : 1.5f) * graph.scale);
        if (progress >= 0.0f)
        {
            const float bar = 3.0f * graph.scale;
            draw.AddRectFilled(ImVec2(low.x + rounding, high.y - bar * 2.0f),
                               ImVec2(low.x + rounding + (high.x - low.x - rounding * 2.0f) * progress, high.y - bar),
                               uiColorU32(colors.success));
        }
        draw.PushClipRect(low, high, true);
        const ImVec2 titleSize = bold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, title.c_str());
        const float titleY = subtitle.empty() ? node.center.y - titleSize.y * 0.5f : node.center.y - titleSize.y;
        draw.AddText(bold, fontSize, ImVec2(node.center.x - titleSize.x * 0.5f, titleY), uiColorU32(colors.text), title.c_str());
        if (!subtitle.empty())
        {
            ImFont* const font = ImGui::GetFont();
            const float size = fontSize * 0.85f;
            const ImVec2 subtitleSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, subtitle.c_str());
            draw.AddText(font, size, ImVec2(node.center.x - subtitleSize.x * 0.5f, node.center.y + 1.0f * graph.scale),
                         uiColorU32(colors.textDim), subtitle.c_str());
        }
        draw.PopClipRect();
    }

    if (editor.connectingFrom)
    {
        const auto from = std::ranges::find_if(nodes, [&](const Node& node) {
            return *editor.connectingFrom >= 0 ? node.element == AnimatorElement::State && node.index == *editor.connectingFrom
                                               : node.element == AnimatorElement::AnyState;
        });
        if (from != nodes.end())
        {
            drawArrow(draw, from->center, mouse, uiColorU32(colors.accent), 2.0f * graph.scale, graph.scale);
        }
        if (from == nodes.end() || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            editor.connectingFrom.reset();
        }
    }
    if (animator.states.empty())
    {
        const char* const hint = "Right-click to add a state, or drop animation clips here.";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        draw.AddText(canvasMin + (canvasSize - size) * 0.5f, uiColorU32(colors.textDim), hint);
    }
    draw.PopClipRect();

    if (hoveredArrow != nullptr && !ImGui::IsPopupOpen("graph menu"))
    {
        const AnimatorTransition& transition = animator.transitions[static_cast<std::size_t>(hoveredArrow->transition)];
        ImGui::SetTooltip("%s → %s\n%s", transition.from.empty() ? "Any State" : transition.from.c_str(), transition.to.c_str(),
                          transitionText(transition).c_str());
    }

    if (ImGui::BeginPopup("graph menu"))
    {
        if (menuElement == AnimatorElement::None)
        {
            if (ImGui::MenuItemEx("New State", icons::Plus.c_str()))
            {
                addState(state, menuPosition, AnimatorBlend::None);
            }
            if (ImGui::MenuItemEx("New Blend Tree 1D", icons::Sliders.c_str()))
            {
                addState(state, menuPosition, AnimatorBlend::Linear);
            }
            ImGui::SetItemTooltip("Clips along a line that a parameter moves on: walk to run");
            if (ImGui::MenuItemEx("New Blend Tree 2D", icons::Move.c_str()))
            {
                addState(state, menuPosition, AnimatorBlend::Planar);
            }
            ImGui::SetItemTooltip("Clips on a plane that two parameters move on: the directions of a walk");
        }
        if (menuElement == AnimatorElement::State || menuElement == AnimatorElement::AnyState)
        {
            if (ImGui::MenuItemEx("Make Transition", icons::Link.c_str()))
            {
                editor.connectingFrom = menuElement == AnimatorElement::State ? menuIndex : -1;
            }
            ImGui::SetItemTooltip("Then click the state it goes to");
        }
        if (menuElement == AnimatorElement::State && menuIndex >= 0 && static_cast<std::size_t>(menuIndex) < animator.states.size())
        {
            const bool entry = animator.states[static_cast<std::size_t>(menuIndex)].name == animator.entry;
            if (ImGui::MenuItemEx("Set as Entry State", icons::Play.c_str(), nullptr, false, !entry))
            {
                animator.entry = animator.states[static_cast<std::size_t>(menuIndex)].name;
                commit(state);
            }
        }
        if ((menuElement == AnimatorElement::State || menuElement == AnimatorElement::Transition) &&
            ImGui::MenuItemEx("Delete", icons::Trash.c_str(), "Delete"))
        {
            deleteSelection(state);
        }
        ImGui::EndPopup();
    }
}

// ---- The pages of the inspector ----

using scene::Entity;
using Button = PanelButton;

// The parameters a list offers: those that hold a number when asked, after "none" when it may be
// left empty.
void fillParameters(InspectorUi& ui, Entity list, const AnimatorData& animator, const std::string& value, bool numbersOnly,
                    const char* none, std::vector<std::string>& names)
{
    names.clear();
    std::vector<std::string> options;
    if (none != nullptr)
    {
        names.emplace_back();
        options.emplace_back(none);
    }
    for (const asset::AnimatorParameter& parameter : animator.parameters)
    {
        if (numbersOnly && !isNumber(parameter.type))
        {
            continue;
        }
        names.push_back(parameter.name);
        options.push_back(std::format("{}  ({})", parameter.name, parameterTypeLabel(parameter.type)));
    }
    scene::UiDropdown& dropdown = ui.scene().get<scene::UiDropdown>(list);
    if (dropdown.options != options)
    {
        dropdown.options = std::move(options);
    }
    const auto found = std::ranges::find(names, value);
    dropdown.selected = found != names.end() ? static_cast<std::int32_t>(found - names.begin()) : -1;
    dropdown.placeholder = value.empty() ? std::string("(none)") : value;
}

// The animation clips a list offers: all of them while it may open, the chosen one otherwise.
void fillClips(InspectorUi& ui, const ToolsState& state, Entity list, asset::AssetId value, std::vector<asset::AssetId>& clips)
{
    const ui::UiWorld& world = ui.panel.world();
    const bool full = world.hovered() == list || world.focused() == list || world.listedDropdown() == list;
    clips.assign(1, asset::AssetId{});
    if (full && state.database != nullptr)
    {
        for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::AnimationClip))
        {
            clips.push_back(info.id);
        }
    }
    else if (value.isValid())
    {
        clips.push_back(value);
    }
    std::vector<std::string> options;
    std::int32_t selected = -1;
    for (const asset::AssetId clip : clips)
    {
        if (clip == value)
        {
            selected = static_cast<std::int32_t>(options.size());
        }
        options.push_back(assetLabel(state, clip));
    }
    scene::UiDropdown& dropdown = ui.scene().get<scene::UiDropdown>(list);
    if (dropdown.options != options)
    {
        dropdown.options = std::move(options);
    }
    dropdown.selected = selected;
    dropdown.placeholder = assetLabel(state, value);
}

[[nodiscard]] std::string clipDrop()
{
    return std::format("asset:{}", asset::toString(asset::AssetType::AnimationClip));
}

// The controller, from FileSystem: what it holds, and the way to its graph.
class AnimatorPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        loadAnimator(state, state.selectedAsset);
        return state.animatorEditor.error;
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        ui.heading(kit, icons::Workflow, themeColors().animation, info->name, source->path);
        if (!state.animatorEditor.error.empty())
        {
            ui.note(nullptr, state.animatorEditor.error, "error", 2.0f);
        }
        m_counts = ui.note(nullptr, "");
        m_entry = ui.note(nullptr, "");
        const Entity row = ui.actions(nullptr);
        m_open = ui.action(kit, row, Icon::Workflow, "Open in the Animator Panel");
        ui.note(nullptr, "An Animator component plays it when its Controller names it; game code sets its parameters (Animation.SetFloat, "
                         "SetBool, SetTrigger in C#).",
                "dim", 3.0f);
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        if (!m_counts.isValid())
        {
            return;
        }
        const AnimatorData& animator = state.animatorEditor.animator;
        ui.scene().get<scene::UiText>(m_counts).text =
            std::format("{} states, {} transitions, {} parameters", animator.states.size(), animator.transitions.size(),
                        animator.parameters.size());
        ui.scene().get<scene::UiText>(m_entry).text = animator.entry.empty() ? std::string{} : std::format("Starts in {}", animator.entry);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        if (m_open.entity.isValid() && ui.panel.world().wasClicked(m_open.entity))
        {
            state.showAnimator = true;
            ImGui::SetWindowFocus(animatorWindow);
        }
    }

private:
    Entity m_counts;
    Entity m_entry;
    Button m_open;
};

// A state or a transition chosen in the graph of the Animator panel.
class AnimatorElementPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        AnimatorEditor& editor = state.animatorEditor;
        clampSelection(editor);
        const AnimatorData& animator = editor.animator;
        std::string text = std::format("{}|{}|{}", static_cast<int>(editor.selected), editor.index, animator.parameters.empty());
        if (editor.selected == AnimatorElement::State && editor.index >= 0)
        {
            const AnimatorState& node = animator.states[static_cast<std::size_t>(editor.index)];
            text += std::format("|{}|{}|", static_cast<int>(node.blend), node.motions.size());
            for (const AnimatorTransition& leaving : animator.transitions)
            {
                text += leaving.from == node.name ? leaving.to + "," : std::string{};
            }
        }
        else if (editor.selected == AnimatorElement::Transition && editor.index >= 0)
        {
            const AnimatorTransition& transition = animator.transitions[static_cast<std::size_t>(editor.index)];
            text += std::format("|{}|{}|{}|{}", transition.from, transition.to, transition.conditions.size(), transition.exitTime >= 0.0f);
        }
        return text;
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const ThemeColors& colors = themeColors();
        AnimatorEditor& editor = state.animatorEditor;
        *this = AnimatorElementPage{};
        const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(editor.asset) : nullptr;
        const bool isState = editor.selected == AnimatorElement::State;
        if (editor.index < 0 || (!isState && editor.selected != AnimatorElement::Transition))
        {
            return;
        }
        ui.heading(kit, icons::Workflow, colors.animation, isState ? "State" : "Transition",
                   std::format("in {}", info != nullptr ? info->name : std::string("the animator")));
        const Entity top = ui.actions(nullptr);
        m_back = ui.action(kit, top, Icon::Close, "Show the Selection Again");
        if (isState)
        {
            buildState(ui, state, kit, editor.animator.states[static_cast<std::size_t>(editor.index)]);
        }
        else
        {
            buildTransition(ui, state, kit, editor.animator.transitions[static_cast<std::size_t>(editor.index)]);
        }
        m_error = ui.note(nullptr, "", "error", 2.0f);
        m_built = true;
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        AnimatorEditor& editor = state.animatorEditor;
        if (!m_built || editor.index < 0)
        {
            return;
        }
        ui.scene().get<scene::UiText>(m_error).text = editor.error;
        ui.scene().get<scene::UiRect>(m_error).visible = !editor.error.empty();
        if (editor.selected == AnimatorElement::State)
        {
            syncState(ui, state, editor.animator.states[static_cast<std::size_t>(editor.index)]);
        }
        else if (editor.selected == AnimatorElement::Transition)
        {
            syncTransition(ui, editor.animator, editor.animator.transitions[static_cast<std::size_t>(editor.index)]);
        }
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        AnimatorEditor& editor = state.animatorEditor;
        if (!m_built || editor.index < 0)
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        if (world.wasClicked(m_back.entity))
        {
            editor.inspecting = false;
            return;
        }
        const bool done = editor.selected == AnimatorElement::State
                              ? answerState(ui, state, editor.animator.states[static_cast<std::size_t>(editor.index)])
                              : answerTransition(ui, state, editor.animator.transitions[static_cast<std::size_t>(editor.index)]);
        if (done)
        {
            return;
        }
        // Saved once a change is over, as one step of the undo history of the panel.
        const bool busy = world.held().isValid() || world.isEditing();
        if (m_dirty && !busy)
        {
            commit(state);
            m_dirty = false;
        }
    }

private:
    struct MotionRow
    {
        Entity clip;
        std::vector<Entity> numbers;
        Button remove;
        std::vector<asset::AssetId> clips;
    };
    struct ConditionRow
    {
        Entity parameter;
        std::vector<std::string> names;
        Entity test;
        std::vector<AnimatorTest> tests;
        Entity value;
        Button remove;
    };

    // A row of controls placed by hand from the right: the first takes what the others leave.
    Entity controlRow(InspectorUi& ui, Section& section)
    {
        const Entity row = ui.add(section.card, "Row", rects::wide(ui.line));
        section.lines.push_back(Line{.entity = row});
        return row;
    }

    Entity placed(InspectorUi& ui, Entity row, float right, float width)
    {
        return ui.add(row, "Control",
                      scene::UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-right - width, 2.0f},
                                    .offsetMax = {-right, -2.0f}});
    }

    void buildState(InspectorUi& ui, ToolsState& state, EditorUiKit& kit, const AnimatorState& node)
    {
        static_cast<void>(state);
        const std::array<std::string_view, 1> one{""};
        Section& card = ui.card(kit, "State", EntityIcon{icons::Workflow, themeColors().animation});
        m_card = ui.sections.size() - 1;
        const FormRow nameRow = ui.formRow(card, "Name");
        m_name = ui.textField(nameRow.editor);
        const FormRow entryRow = ui.formRow(card, "Entry State");
        m_entry = ui.toggle(entryRow.editor);
        ui.tooltip(entryRow.editor, "The state the animator starts in");
        const FormRow motionRow = ui.formRow(card, "Motion");
        m_blend = ui.choice(motionRow.editor, {"Clip", "Blend Tree 1D", "Blend Tree 2D"});
        if (node.blend == AnimatorBlend::None)
        {
            const FormRow clipRow = ui.formRow(card, "Clip");
            m_clip = ui.choice(clipRow.editor);
            const math::Vec4 accent = linearColor(themeColors().accent);
            ui.scene().add<scene::UiDropTarget>(m_clip, scene::UiDropTarget{.accepts = {clipDrop()},
                                                                            .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.35f}});
        }
        else
        {
            const FormRow parameterRow = ui.formRow(card, node.blend == AnimatorBlend::Linear ? "Parameter" : "Parameter X");
            m_parameter = ui.choice(parameterRow.editor);
            if (node.blend == AnimatorBlend::Planar)
            {
                const FormRow yRow = ui.formRow(card, "Parameter Y");
                m_parameterY = ui.choice(yRow.editor);
            }
        }
        const FormRow spriteRow = ui.formRow(card, "Sprite Animation");
        m_sprite = ui.textField(spriteRow.editor, "none");
        ui.tooltip(spriteRow.editor, "A named animation that the SpriteAnimator of the entity plays in this state");
        const FormRow speedRow = ui.formRow(card, "Speed");
        m_speed = ui.numbers(speedRow.editor, one, {.minValue = -10.0f, .maxValue = 10.0f, .dragSpeed = 0.01f, .decimals = 2}).front();
        const FormRow speedParameterRow = ui.formRow(card, "Speed Parameter");
        m_speedParameter = ui.choice(speedParameterRow.editor);
        ui.tooltip(speedParameterRow.editor, "Multiplies the speed");
        const FormRow loopRow = ui.formRow(card, "Loop");
        m_loop = ui.toggle(loopRow.editor);

        if (node.blend != AnimatorBlend::None)
        {
            Section& clips = ui.card(kit, node.blend == AnimatorBlend::Linear ? "Clips along the parameter" : "Clips on the plane");
            const float button = ui.line - 6.0f;
            const float numbers = ui.font * (node.blend == AnimatorBlend::Linear ? 4.5f : 8.0f);
            const std::array<std::string_view, 2> axes{"x", "y"};
            for (std::size_t index = 0; index < node.motions.size(); ++index)
            {
                MotionRow motion;
                const Entity row = controlRow(ui, clips);
                const Entity clipBox = ui.add(row, "Clip",
                                              scene::UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f},
                                                            .offsetMin = {ui.font * 0.35f, 2.0f}, .offsetMax = {-button - numbers - ui.font * 0.6f, -2.0f}});
                motion.clip = ui.choice(clipBox);
                const Entity numberBox = placed(ui, row, button + ui.font * 0.3f, numbers);
                if (node.blend == AnimatorBlend::Linear)
                {
                    motion.numbers = ui.numbers(numberBox, one, {.dragSpeed = 0.01f, .decimals = 2});
                    ui.tooltip(numberBox, std::format("Where the clip plays alone, along {}", node.parameter));
                }
                else
                {
                    motion.numbers = ui.numbers(numberBox, axes, {.dragSpeed = 0.01f, .decimals = 2});
                    ui.tooltip(numberBox, std::format("Where the clip plays alone: {}, {}", node.parameter, node.parameterY));
                }
                motion.remove = ui.toolButton(kit, row, Icon::Trash,
                                              scene::UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f},
                                                            .offsetMin = {-button, -button * 0.5f}, .offsetMax = {0.0f, button * 0.5f}});
                ui.tooltip(motion.remove.entity, "Remove the clip");
                m_motions.push_back(std::move(motion));
            }
            const Entity actions = ui.actions(&clips);
            m_addClip = ui.action(kit, actions, Icon::Plus, "Add Clip");
            ui.tooltip(m_addClip.entity, "Or drop animation clips on this button");
            const math::Vec4 accent = linearColor(themeColors().accent);
            ui.scene().add<scene::UiDropTarget>(m_addClip.entity, scene::UiDropTarget{.accepts = {clipDrop()},
                                                                                     .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.35f}});
        }

        // The ways out of the state, in the order they are checked.
        Section& transitions = ui.card(kit, "Transitions");
        const AnimatorData& animator = state.animatorEditor.animator;
        for (std::size_t index = 0; index < animator.transitions.size(); ++index)
        {
            const AnimatorTransition& leaving = animator.transitions[index];
            if (leaving.from != node.name)
            {
                continue;
            }
            const Entity row = ui.add(transitions.card, "Transition", rects::wide(ui.line), "soft_row");
            ui.scene().add<scene::UiImage>(row);
            ui.scene().add<scene::UiButton>(row);
            ui.text(row, rects::whole(math::Vec4{ui.font * 0.6f, 0.0f, ui.font * 0.6f, 0.0f}),
                    std::format("-> {}   {}", leaving.to, transitionText(leaving)), "text");
            transitions.lines.push_back(Line{.entity = row});
            m_leaving.emplace_back(row, index);
        }
        if (m_leaving.empty())
        {
            ui.note(&transitions, "None: right-click the state in the graph, then Make Transition.", "dim", 2.0f);
        }
        const Entity actions = ui.actions(nullptr);
        m_delete = ui.action(kit, actions, Icon::Trash, "Delete State");
    }

    void syncState(InspectorUi& ui, ToolsState& state, const AnimatorState& node)
    {
        const AnimatorData& animator = state.animatorEditor.animator;
        const ui::UiWorld& world = ui.panel.world();
        ui.scene().get<scene::UiText>(ui.sections[m_card].title).text = node.name;
        if (world.editedField() != m_name)
        {
            ui.scene().get<scene::UiText>(m_name).text = node.name;
        }
        ui.setToggle(m_entry, node.name == animator.entry);
        ui.scene().get<scene::UiDropdown>(m_blend).selected = static_cast<std::int32_t>(node.blend);
        if (m_clip.isValid())
        {
            fillClips(ui, state, m_clip, node.motions.empty() ? asset::AssetId{} : node.motions.front().clip, m_clips);
        }
        if (m_parameter.isValid())
        {
            fillParameters(ui, m_parameter, animator, node.parameter, true, nullptr, m_parameters);
        }
        if (m_parameterY.isValid())
        {
            fillParameters(ui, m_parameterY, animator, node.parameterY, true, nullptr, m_parametersY);
        }
        if (world.editedField() != m_sprite)
        {
            ui.scene().get<scene::UiText>(m_sprite).text = node.spriteAnimation;
        }
        if (world.editedField() != m_speed && world.held() != m_speed)
        {
            ui.scene().get<scene::UiNumberField>(m_speed).value = node.speed;
        }
        fillParameters(ui, m_speedParameter, animator, node.speedParameter, true, "none", m_speedParameters);
        ui.setToggle(m_loop, node.loop);
        for (std::size_t index = 0; index < m_motions.size() && index < node.motions.size(); ++index)
        {
            MotionRow& motion = m_motions[index];
            const asset::AnimatorMotion& item = node.motions[index];
            fillClips(ui, state, motion.clip, item.clip, motion.clips);
            const std::array<float, 2> values{node.blend == AnimatorBlend::Linear ? item.threshold : item.position.x, item.position.y};
            for (std::size_t axis = 0; axis < motion.numbers.size(); ++axis)
            {
                if (world.editedField() != motion.numbers[axis] && world.held() != motion.numbers[axis])
                {
                    ui.scene().get<scene::UiNumberField>(motion.numbers[axis]).value = values[axis];
                }
            }
        }
    }

    // Answers whether the state went, which ends the page.
    [[nodiscard]] bool answerState(InspectorUi& ui, ToolsState& state, AnimatorState& node)
    {
        AnimatorData& animator = state.animatorEditor.animator;
        const ui::UiWorld& world = ui.panel.world();
        const auto chosen = [&](Entity list) { return ui.scene().get<scene::UiDropdown>(list).selected; };
        if (world.editedField() == m_name)
        {
            m_naming = true;
        }
        else if (std::exchange(m_naming, false) && !ui.panel.input().cancelPressed)
        {
            const std::string typed = ui.scene().get<scene::UiText>(m_name).text;
            if (!typed.empty() && typed != node.name && animator.findState(typed) == nullptr)
            {
                renameState(animator, node.name, typed);
                m_dirty = true;
            }
        }
        if (world.wasChanged(m_entry) && ui.scene().get<scene::UiToggle>(m_entry).value)
        {
            animator.entry = node.name;
            m_dirty = true;
        }
        if (world.wasChanged(m_blend) && chosen(m_blend) >= 0)
        {
            const auto blend = static_cast<AnimatorBlend>(chosen(m_blend));
            if (blend != node.blend)
            {
                node.blend = blend;
                if (blend != AnimatorBlend::None && node.parameter.empty())
                {
                    node.parameter = numberParameter(animator, 0, blend == AnimatorBlend::Linear ? "Speed" : "X");
                }
                if (blend == AnimatorBlend::Planar && node.parameterY.empty())
                {
                    node.parameterY = numberParameter(animator, 1, "Y");
                }
                m_dirty = true;
            }
        }
        if (m_clip.isValid())
        {
            std::optional<asset::AssetId> clip;
            if (world.wasChanged(m_clip) && chosen(m_clip) >= 0 && static_cast<std::size_t>(chosen(m_clip)) < m_clips.size())
            {
                clip = m_clips[static_cast<std::size_t>(chosen(m_clip))];
            }
            if (world.wasDropped(m_clip) && world.dropped() != nullptr)
            {
                if (const std::optional<core::Uuid> uuid = core::Uuid::parse(world.dropped()->data))
                {
                    clip = asset::AssetId{*uuid};
                }
            }
            if (clip)
            {
                if (node.motions.empty())
                {
                    node.motions.push_back({});
                }
                node.motions.front().clip = *clip;
                m_dirty = true;
            }
        }
        const auto parameterChosen = [&](Entity list, const std::vector<std::string>& names, std::string& value) {
            if (list.isValid() && world.wasChanged(list) && chosen(list) >= 0 && static_cast<std::size_t>(chosen(list)) < names.size())
            {
                value = names[static_cast<std::size_t>(chosen(list))];
                m_dirty = true;
            }
        };
        parameterChosen(m_parameter, m_parameters, node.parameter);
        parameterChosen(m_parameterY, m_parametersY, node.parameterY);
        parameterChosen(m_speedParameter, m_speedParameters, node.speedParameter);
        if (world.editedField() == m_sprite)
        {
            m_typingSprite = true;
        }
        else if (std::exchange(m_typingSprite, false) && !ui.panel.input().cancelPressed)
        {
            node.spriteAnimation = ui.scene().get<scene::UiText>(m_sprite).text;
            m_dirty = true;
        }
        if (world.wasChanged(m_speed))
        {
            node.speed = ui.scene().get<scene::UiNumberField>(m_speed).value;
            m_dirty = true;
        }
        if (world.wasChanged(m_loop))
        {
            node.loop = ui.scene().get<scene::UiToggle>(m_loop).value;
            m_dirty = true;
        }

        // The clips of a blend.
        std::optional<std::size_t> removed;
        for (std::size_t index = 0; index < m_motions.size() && index < node.motions.size(); ++index)
        {
            MotionRow& motion = m_motions[index];
            asset::AnimatorMotion& item = node.motions[index];
            if (world.wasChanged(motion.clip) && chosen(motion.clip) >= 0 &&
                static_cast<std::size_t>(chosen(motion.clip)) < motion.clips.size())
            {
                item.clip = motion.clips[static_cast<std::size_t>(chosen(motion.clip))];
                m_dirty = true;
            }
            for (std::size_t axis = 0; axis < motion.numbers.size(); ++axis)
            {
                if (!world.wasChanged(motion.numbers[axis]))
                {
                    continue;
                }
                const float value = ui.scene().get<scene::UiNumberField>(motion.numbers[axis]).value;
                if (node.blend == AnimatorBlend::Linear)
                {
                    item.threshold = value;
                }
                else
                {
                    (axis == 0 ? item.position.x : item.position.y) = value;
                }
                m_dirty = true;
            }
            if (world.wasClicked(motion.remove.entity))
            {
                removed = index;
            }
        }
        if (removed)
        {
            node.motions.erase(node.motions.begin() + static_cast<std::ptrdiff_t>(*removed));
            m_dirty = true;
        }
        if (m_addClip.entity.isValid())
        {
            if (world.wasClicked(m_addClip.entity))
            {
                asset::AnimatorMotion added;
                if (!node.motions.empty())
                {
                    added.threshold = node.motions.back().threshold + 1.0f;
                    added.position = node.motions.back().position + math::Vec2{1.0f, 0.0f};
                }
                node.motions.push_back(added);
                m_dirty = true;
            }
            if (world.wasDropped(m_addClip.entity) && world.dropped() != nullptr)
            {
                if (const std::optional<core::Uuid> uuid = core::Uuid::parse(world.dropped()->data))
                {
                    node.motions.push_back({.clip = asset::AssetId{*uuid},
                                            .threshold = node.motions.empty() ? 0.0f : node.motions.back().threshold + 1.0f});
                    m_dirty = true;
                }
            }
        }
        for (const auto& [row, index] : m_leaving)
        {
            if (world.wasClicked(row))
            {
                select(state, AnimatorElement::Transition, static_cast<std::int32_t>(index));
                return true;
            }
        }
        if (world.wasClicked(m_delete.entity))
        {
            deleteSelection(state);
            return true;
        }
        return false;
    }

    void buildTransition(InspectorUi& ui, ToolsState& state, EditorUiKit& kit, const AnimatorTransition& transition)
    {
        const AnimatorData& animator = state.animatorEditor.animator;
        const std::array<std::string_view, 1> one{""};
        const Entity route = ui.note(nullptr, std::format("{}  ->  {}", transition.from.empty() ? "Any State" : transition.from, transition.to),
                                     "text");
        ui.scene().get<scene::UiText>(route).font = EditorUiKit::boldFont();
        Section& card = ui.card(kit, "Transition", EntityIcon{icons::Workflow, themeColors().animation});
        const FormRow durationRow = ui.formRow(card, "Duration");
        m_duration = ui.numbers(durationRow.editor, one, {.minValue = 0.0f, .maxValue = 10.0f, .dragSpeed = 0.01f, .decimals = 2, .format = "{} s"})
                         .front();
        ui.tooltip(durationRow.editor, "Seconds of the crossfade from one state to the other");
        const FormRow exitRow = ui.formRow(card, "Exit Time");
        m_hasExit = ui.toggle(exitRow.editor);
        ui.tooltip(exitRow.editor, "Waits until the state reaches a point of its cycle: 1 is its end");
        if (transition.exitTime >= 0.0f)
        {
            const FormRow atRow = ui.formRow(card, "");
            m_exit = ui.numbers(atRow.editor, one, {.minValue = 0.0f, .maxValue = 10.0f, .dragSpeed = 0.01f, .decimals = 2}).front();
        }
        else if (transition.conditions.empty())
        {
            ui.note(&card, "At the end, without conditions.");
        }

        Section& conditions = ui.card(kit, "Conditions");
        const float button = ui.line - 6.0f;
        const float valueWidth = ui.font * 4.5f;
        const float testWidth = ui.font * 6.5f;
        for (std::size_t index = 0; index < transition.conditions.size(); ++index)
        {
            ConditionRow condition;
            const Entity row = controlRow(ui, conditions);
            const Entity parameterBox =
                ui.add(row, "Parameter",
                       scene::UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {ui.font * 0.35f, 2.0f},
                                     .offsetMax = {-button - valueWidth - testWidth - ui.font * 0.9f, -2.0f}});
            condition.parameter = ui.choice(parameterBox);
            condition.test = ui.choice(placed(ui, row, button + valueWidth + ui.font * 0.6f, testWidth));
            condition.value = ui.numbers(placed(ui, row, button + ui.font * 0.3f, valueWidth), one, {.dragSpeed = 0.01f, .decimals = 2}).front();
            condition.remove = ui.toolButton(kit, row, Icon::Trash,
                                             scene::UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f},
                                                           .offsetMin = {-button, -button * 0.5f}, .offsetMax = {0.0f, button * 0.5f}});
            ui.tooltip(condition.remove.entity, "Remove the condition");
            m_conditions.push_back(std::move(condition));
        }
        if (animator.parameters.empty())
        {
            ui.note(&conditions, "Add parameters in the Animator panel to check them here.", "dim", 2.0f);
        }
        else
        {
            m_addCondition = ui.action(kit, ui.actions(&conditions), Icon::Plus, "Add Condition");
        }

        Section& order = ui.card(kit, "Order");
        ui.note(&order, "Transitions are checked in order: the first one whose conditions are met is taken.", "dim", 2.0f);
        const Entity moves = ui.actions(&order);
        m_earlier = ui.action(kit, moves, Icon::ArrowUpDown, "Earlier");
        m_later = ui.action(kit, moves, Icon::ArrowUpDown, "Later");
        const Entity actions = ui.actions(nullptr);
        m_delete = ui.action(kit, actions, Icon::Trash, "Delete Transition");
    }

    void syncTransition(InspectorUi& ui, const AnimatorData& animator, const AnimatorTransition& transition)
    {
        const ui::UiWorld& world = ui.panel.world();
        const auto number = [&](Entity box, float value) {
            if (box.isValid() && world.editedField() != box && world.held() != box)
            {
                ui.scene().get<scene::UiNumberField>(box).value = value;
            }
        };
        number(m_duration, transition.duration);
        ui.setToggle(m_hasExit, transition.exitTime >= 0.0f);
        number(m_exit, transition.exitTime);
        for (std::size_t index = 0; index < m_conditions.size() && index < transition.conditions.size(); ++index)
        {
            ConditionRow& row = m_conditions[index];
            const asset::AnimatorCondition& item = transition.conditions[index];
            fillParameters(ui, row.parameter, animator, item.parameter, false, nullptr, row.names);
            // The tests that fit the parameter.
            const asset::AnimatorParameter* const parameter = animator.findParameter(item.parameter);
            row.tests.clear();
            std::vector<std::string> labels;
            for (const AnimatorTest test : {AnimatorTest::Greater, AnimatorTest::Less, AnimatorTest::Equals, AnimatorTest::NotEquals,
                                            AnimatorTest::IsTrue, AnimatorTest::IsFalse, AnimatorTest::Triggered})
            {
                if (parameter == nullptr || asset::testFits(test, parameter->type))
                {
                    row.tests.push_back(test);
                    labels.emplace_back(testLabel(test));
                }
            }
            scene::UiDropdown& tests = ui.scene().get<scene::UiDropdown>(row.test);
            if (tests.options != labels)
            {
                tests.options = std::move(labels);
            }
            const auto found = std::ranges::find(row.tests, item.test);
            tests.selected = found != row.tests.end() ? static_cast<std::int32_t>(found - row.tests.begin()) : -1;
            tests.placeholder = testLabel(item.test);
            // Only the comparisons have a value; whole numbers for an integer.
            const bool compared = item.test <= AnimatorTest::NotEquals;
            ui.scene().get<scene::UiRect>(ui.scene().parent(row.value)).visible = compared;
            scene::UiNumberField& value = ui.scene().get<scene::UiNumberField>(row.value);
            const bool whole = parameter != nullptr && parameter->type == AnimatorParameterType::Integer;
            value.step = whole ? 1.0f : 0.0f;
            value.decimals = whole ? 0 : 2;
            value.dragSpeed = whole ? 0.1f : 0.01f;
            number(row.value, item.value);
        }
    }

    [[nodiscard]] bool answerTransition(InspectorUi& ui, ToolsState& state, AnimatorTransition& transition)
    {
        AnimatorEditor& editor = state.animatorEditor;
        AnimatorData& animator = editor.animator;
        const ui::UiWorld& world = ui.panel.world();
        const auto value = [&](Entity box) { return ui.scene().get<scene::UiNumberField>(box).value; };
        if (world.wasChanged(m_duration))
        {
            transition.duration = std::max(value(m_duration), 0.0f);
            m_dirty = true;
        }
        if (world.wasChanged(m_hasExit))
        {
            transition.exitTime = ui.scene().get<scene::UiToggle>(m_hasExit).value ? 1.0f : -1.0f;
            m_dirty = true;
        }
        if (m_exit.isValid() && world.wasChanged(m_exit))
        {
            transition.exitTime = std::max(value(m_exit), 0.0f);
            m_dirty = true;
        }
        std::optional<std::size_t> removed;
        for (std::size_t index = 0; index < m_conditions.size() && index < transition.conditions.size(); ++index)
        {
            ConditionRow& row = m_conditions[index];
            asset::AnimatorCondition& item = transition.conditions[index];
            const std::int32_t parameterChosen = ui.scene().get<scene::UiDropdown>(row.parameter).selected;
            if (world.wasChanged(row.parameter) && parameterChosen >= 0 && static_cast<std::size_t>(parameterChosen) < row.names.size())
            {
                item.parameter = row.names[static_cast<std::size_t>(parameterChosen)];
                const asset::AnimatorParameter* const parameter = animator.findParameter(item.parameter);
                if (parameter != nullptr && !asset::testFits(item.test, parameter->type))
                {
                    item.test = firstFittingTest(parameter->type);
                }
                m_dirty = true;
            }
            const std::int32_t testChosen = ui.scene().get<scene::UiDropdown>(row.test).selected;
            if (world.wasChanged(row.test) && testChosen >= 0 && static_cast<std::size_t>(testChosen) < row.tests.size())
            {
                item.test = row.tests[static_cast<std::size_t>(testChosen)];
                m_dirty = true;
            }
            if (world.wasChanged(row.value))
            {
                item.value = value(row.value);
                m_dirty = true;
            }
            if (world.wasClicked(row.remove.entity))
            {
                removed = index;
            }
        }
        if (removed)
        {
            transition.conditions.erase(transition.conditions.begin() + static_cast<std::ptrdiff_t>(*removed));
            m_dirty = true;
        }
        if (m_addCondition.entity.isValid() && world.wasClicked(m_addCondition.entity) && !animator.parameters.empty())
        {
            const asset::AnimatorParameter& first = animator.parameters.front();
            transition.conditions.push_back({.parameter = first.name, .test = firstFittingTest(first.type)});
            m_dirty = true;
        }
        const auto index = static_cast<std::size_t>(editor.index);
        ui.enable(m_earlier, index > 0);
        ui.enable(m_later, index + 1 < animator.transitions.size());
        if (world.wasClicked(m_earlier.entity) && index > 0)
        {
            std::swap(animator.transitions[index], animator.transitions[index - 1]);
            editor.index = static_cast<std::int32_t>(index - 1);
            commit(state);
            return true;
        }
        if (world.wasClicked(m_later.entity) && index + 1 < animator.transitions.size())
        {
            std::swap(animator.transitions[index], animator.transitions[index + 1]);
            editor.index = static_cast<std::int32_t>(index + 1);
            commit(state);
            return true;
        }
        if (world.wasClicked(m_delete.entity))
        {
            deleteSelection(state);
            return true;
        }
        return false;
    }

    bool m_built = false;
    Button m_back;
    Entity m_error;
    bool m_dirty = false;
    // A state.
    std::size_t m_card = 0;
    Entity m_name;
    bool m_naming = false;
    Entity m_entry;
    Entity m_blend;
    Entity m_clip;
    std::vector<asset::AssetId> m_clips;
    Entity m_parameter;
    std::vector<std::string> m_parameters;
    Entity m_parameterY;
    std::vector<std::string> m_parametersY;
    Entity m_sprite;
    bool m_typingSprite = false;
    Entity m_speed;
    Entity m_speedParameter;
    std::vector<std::string> m_speedParameters;
    Entity m_loop;
    std::vector<MotionRow> m_motions;
    Button m_addClip;
    std::vector<std::pair<Entity, std::size_t>> m_leaving;
    Button m_delete;
    // A transition.
    Entity m_duration;
    Entity m_hasExit;
    Entity m_exit;
    std::vector<ConditionRow> m_conditions;
    Button m_addCondition;
    Button m_earlier;
    Button m_later;
};

} // namespace

void drawAnimatorPanel(ToolsState& state, scene::Scene& scene)
{
    AnimatorEditor& editor = state.animatorEditor;
    if (!state.showAnimator)
    {
        editor.focused = false;
        return;
    }
    // Where it opens the first time: beside the Output panel, whose space a graph needs.
    if (const ImGuiWindow* const output = ImGui::FindWindowByName(consoleWindow); output != nullptr && output->DockId != 0)
    {
        ImGui::SetNextWindowDockID(output->DockId, ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 60.0f, ImGui::GetFontSize() * 26.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(animatorWindow, &state.showAnimator))
    {
        editor.focused = false;
        ImGui::End();
        return;
    }
    editor.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ThemeColors& colors = themeColors();
    if (state.database == nullptr)
    {
        ImGui::TextDisabled("Open a project to edit its animators.");
        ImGui::End();
        return;
    }

    // What the panel edits: the controller of the selected Animator, or the selected animator asset.
    asset::AssetId target;
    const scene::Entity selected = animatorOf(scene, scene.findEntity(state.selection.active()));
    if (selected.isValid())
    {
        target = scene.get<scene::Animator>(selected).controller;
        editor.entity = scene.uuid(selected);
        if (!target.isValid())
        {
            ImGui::TextDisabled("The Animator of %s has no controller.", std::string(scene.name(selected)).c_str());
            ImGui::TextWrapped("Choose one in its Controller field, or make one with New Animator in the FileSystem.");
            ImGui::End();
            return;
        }
    }
    else if (const asset::AssetInfo* const info = state.selectedAsset.isValid() ? state.database->find(state.selectedAsset) : nullptr;
             info != nullptr && info->type == asset::AssetType::Animator)
    {
        target = state.selectedAsset;
    }
    else
    {
        target = editor.asset;
    }
    if (!target.isValid() || state.database->find(target) == nullptr)
    {
        ImGui::TextDisabled("Select an entity with an Animator, or an animator in the FileSystem.");
        ImGui::End();
        return;
    }
    loadAnimator(state, target);

    // While the game plays, the state machine of the entity followed shows live.
    std::optional<animation::AnimatorStatus> status;
    const scene::Entity live = scene.findEntity(editor.entity);
    if (state.playState != PlayState::Editing && state.animationWorld != nullptr && live.isValid() &&
        scene.has<scene::Animator>(live) && scene.get<scene::Animator>(live).controller == editor.asset)
    {
        status = state.animationWorld->status(live);
    }

    // The toolbar: the asset, undo, and why it is not saved.
    const asset::AssetInfo* const info = state.database->find(editor.asset);
    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Workflow, colors.animation);
    boldText(info != nullptr ? info->name.c_str() : "Animator");
    if (live.isValid() && status)
    {
        ImGui::SameLine();
        ImGui::TextColored(uiColor(colors.success), "%s: %s", std::string(scene.name(live)).c_str(), status->state.c_str());
    }
    ImGui::SameLine();
    if (toolButton("undo", icons::Undo, "Undo (Ctrl+Z)", false, !editor.undo.empty()))
    {
        undoAnimator(state);
    }
    ImGui::SameLine();
    if (toolButton("redo", icons::Redo, "Redo (Ctrl+Y)", false, !editor.redo.empty()))
    {
        redoAnimator(state);
    }
    ImGui::SameLine();
    if (toolButton("frame", icons::Scan, "Frame the graph"))
    {
        editor.frame = true;
    }
    if (!editor.error.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(uiColor(colors.error), "%s", editor.error.c_str());
    }
    else
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Right-click: add states and transitions. Middle or right drag: move the view.");
    }

    const float sidebar = ImGui::GetFontSize() * 15.0f;
    if (ImGui::BeginChild("parameters", ImVec2(sidebar, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
    {
        drawParameters(state, status, live);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("graph", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove))
    {
        drawGraph(state, status);
    }
    ImGui::EndChild();

    // Shortcuts, while no field takes the keys.
    if (editor.focused && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        {
            undoAnimator(state);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        {
            redoAnimator(state);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            deleteSelection(state);
        }
    }
    ImGui::End();
}

std::unique_ptr<InspectorPage> makeAnimatorElementPage()
{
    return std::make_unique<AnimatorElementPage>();
}

std::unique_ptr<InspectorPage> makeAnimatorPage()
{
    return std::make_unique<AnimatorPage>();
}

core::Result<std::filesystem::path> createAnimatorFile(ToolsState& state, std::string_view folder)
{
    if (state.database == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "no project is open");
    }
    // Only the assets folder is imported.
    const std::string assets = std::string(asset::resourceScheme) + "assets";
    if (!folder.starts_with(assets))
    {
        folder = assets;
    }
    AnimatorData animator;
    animator.entry = "Idle";
    animator.states.push_back({.name = "Idle", .graphPosition = {0.0f, 0.0f}});
    const std::string base = std::string(folder) + (folder.ends_with('/') ? "" : "/");
    for (int number = 1; number < 1000; ++number)
    {
        const std::string name = number == 1 ? std::string("Animator") : std::format("Animator {}", number);
        const std::string resource = base + name + std::string(asset::animatorExtension);
        const std::optional<std::filesystem::path> file = state.database->project().absolutePath(resource);
        if (!file)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "{} is outside the project", resource);
        }
        std::error_code error;
        if (std::filesystem::exists(*file, error))
        {
            continue;
        }
        if (core::Result<void> written = core::writeTextFile(*file, asset::writeAnimatorFile(animator)); !written)
        {
            return std::unexpected(written.error());
        }
        state.database->refresh();
        state.assetToSelect = resource;
        return *file;
    }
    return core::makeError(core::ErrorCode::AlreadyExists, "too many animators are named Animator in {}", folder);
}

} // namespace devex::tools::detail
