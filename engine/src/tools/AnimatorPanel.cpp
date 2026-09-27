#include "ToolsState.hpp"

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

// ---- The inspector ----

// A combo of the parameters that fit, by name; an empty choice when allowed.
bool parameterCombo(const char* id, const AnimatorData& animator, std::string& value, bool numbersOnly, const char* none = nullptr)
{
    bool changed = false;
    if (beginCombo(id, value.empty() ? (none != nullptr ? none : "(none)") : value.c_str()))
    {
        if (none != nullptr && ImGui::Selectable(none, value.empty()))
        {
            value.clear();
            changed = true;
        }
        for (const asset::AnimatorParameter& parameter : animator.parameters)
        {
            if (numbersOnly && !isNumber(parameter.type))
            {
                continue;
            }
            if (ImGui::Selectable(std::format("{}  ({})", parameter.name, parameterTypeLabel(parameter.type)).c_str(),
                                  parameter.name == value))
            {
                value = parameter.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawStateInspector(ToolsState& state, std::size_t index)
{
    AnimatorEditor& editor = state.animatorEditor;
    AnimatorData& animator = editor.animator;
    AnimatorState& node = animator.states[index];
    bool changed = false;

    if (beginProperties("state"))
    {
        propertyName("Name");
        std::string name = node.name;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
            (ImGui::IsItemDeactivatedAfterEdit() && name != node.name))
        {
            if (!name.empty() && animator.findState(name) == nullptr)
            {
                renameState(animator, node.name, name);
                changed = true;
            }
        }
        propertyName("Entry State");
        bool entry = node.name == animator.entry;
        if (ImGui::Checkbox("##entry", &entry) && entry)
        {
            animator.entry = node.name;
            changed = true;
        }
        ImGui::SetItemTooltip("The state the animator starts in");

        propertyName("Motion");
        constexpr std::array<std::pair<AnimatorBlend, const char*>, 3> blends{
            {{AnimatorBlend::None, "Clip"}, {AnimatorBlend::Linear, "Blend Tree 1D"}, {AnimatorBlend::Planar, "Blend Tree 2D"}}};
        const auto chosen = std::ranges::find(blends, node.blend, &std::pair<AnimatorBlend, const char*>::first);
        if (beginCombo("##blend", chosen != blends.end() ? chosen->second : "?"))
        {
            for (const auto& [blend, label] : blends)
            {
                if (ImGui::Selectable(label, blend == node.blend) && blend != node.blend)
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
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (node.blend == AnimatorBlend::None)
        {
            propertyName("Clip");
            if (node.motions.empty())
            {
                node.motions.push_back({});
            }
            changed |= drawAssetPicker(state, "##clip", asset::AssetType::AnimationClip, node.motions.front().clip);
        }
        else
        {
            propertyName(node.blend == AnimatorBlend::Linear ? "Parameter" : "Parameter X");
            changed |= parameterCombo("##parameter", animator, node.parameter, true);
            if (node.blend == AnimatorBlend::Planar)
            {
                propertyName("Parameter Y");
                changed |= parameterCombo("##parameterY", animator, node.parameterY, true);
            }
        }
        propertyName("Sprite Animation");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##sprite", "none", &node.spriteAnimation);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("A named animation that the SpriteAnimator of the entity plays in this state");
        propertyName("Speed");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat("##speed", &node.speed, 0.01f, -10.0f, 10.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        propertyName("Speed Parameter");
        changed |= parameterCombo("##speedParameter", animator, node.speedParameter, true, "none");
        ImGui::SetItemTooltip("Multiplies the speed");
        propertyName("Loop");
        changed |= ImGui::Checkbox("##loop", &node.loop);
        endProperties();
    }

    if (node.blend != AnimatorBlend::None)
    {
        ImGui::SeparatorText(node.blend == AnimatorBlend::Linear ? "Clips along the parameter" : "Clips on the plane");
        std::optional<std::size_t> removed;
        const float numberWidth = ImGui::GetFontSize() * (node.blend == AnimatorBlend::Linear ? 4.5f : 8.0f);
        for (std::size_t motion = 0; motion < node.motions.size(); ++motion)
        {
            ImGui::PushID(static_cast<int>(motion));
            asset::AnimatorMotion& item = node.motions[motion];
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - numberWidth - toolButtonWidth() -
                                    ImGui::GetStyle().ItemSpacing.x * 2.0f);
            changed |= drawAssetPicker(state, "##clip", asset::AssetType::AnimationClip, item.clip);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(numberWidth);
            if (node.blend == AnimatorBlend::Linear)
            {
                ImGui::DragFloat("##threshold", &item.threshold, 0.01f, 0.0f, 0.0f, "%.2f");
                ImGui::SetItemTooltip("Where the clip plays alone, along %s", node.parameter.c_str());
            }
            else
            {
                std::array<float, 2> position{item.position.x, item.position.y};
                if (dragVector("##position", position.data(), 2, 0.01f, "%.2f"))
                {
                    item.position = {position[0], position[1]};
                }
                ImGui::SetItemTooltip("Where the clip plays alone: %s, %s", node.parameter.c_str(), node.parameterY.c_str());
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if (toolButton("remove", icons::Trash, "Remove the clip"))
            {
                removed = motion;
            }
            ImGui::PopID();
        }
        if (removed)
        {
            node.motions.erase(node.motions.begin() + static_cast<std::ptrdiff_t>(*removed));
            changed = true;
        }
        if (labelButton(icons::Plus, "Add Clip"))
        {
            asset::AnimatorMotion added;
            if (!node.motions.empty())
            {
                added.threshold = node.motions.back().threshold + 1.0f;
                added.position = node.motions.back().position + math::Vec2{1.0f, 0.0f};
            }
            node.motions.push_back(added);
            changed = true;
        }
        if (const std::optional<asset::AssetId> dropped = acceptDroppedAsset(asset::AssetType::AnimationClip))
        {
            node.motions.push_back({.clip = *dropped, .threshold = node.motions.empty() ? 0.0f : node.motions.back().threshold + 1.0f});
            changed = true;
        }
        ImGui::SetItemTooltip("Or drop animation clips on this button");
    }

    // The ways out of the state, in the order they are checked.
    ImGui::SeparatorText("Transitions");
    bool any = false;
    for (std::size_t transition = 0; transition < animator.transitions.size(); ++transition)
    {
        const AnimatorTransition& leaving = animator.transitions[transition];
        if (leaving.from != node.name)
        {
            continue;
        }
        any = true;
        ImGui::PushID(static_cast<int>(transition));
        if (ImGui::Selectable(std::format("-> {}   {}", leaving.to, transitionText(leaving)).c_str()))
        {
            select(state, AnimatorElement::Transition, static_cast<std::int32_t>(transition));
        }
        ImGui::PopID();
    }
    if (!any)
    {
        ImGui::TextDisabled("None: right-click the state in the graph, then Make Transition.");
    }
    ImGui::Spacing();
    if (labelButton(icons::Trash, "Delete State"))
    {
        deleteSelection(state);
        return;
    }
    if (changed)
    {
        commit(state);
    }
}

void drawTransitionInspector(ToolsState& state, std::size_t index)
{
    AnimatorEditor& editor = state.animatorEditor;
    AnimatorData& animator = editor.animator;
    AnimatorTransition& transition = animator.transitions[index];
    const ThemeColors& colors = themeColors();
    bool changed = false;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(transition.from.empty() ? "Any State" : transition.from.c_str());
    ImGui::SameLine();
    ImGui::TextColored(uiColor(colors.textDim), "->");
    ImGui::SameLine();
    ImGui::TextUnformatted(transition.to.c_str());

    if (beginProperties("transition"))
    {
        propertyName("Duration");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat("##duration", &transition.duration, 0.01f, 0.0f, 10.0f, "%.2f s");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Seconds of the crossfade from one state to the other");
        propertyName("Exit Time");
        bool exit = transition.exitTime >= 0.0f;
        if (ImGui::Checkbox("##hasExit", &exit))
        {
            transition.exitTime = exit ? 1.0f : -1.0f;
            changed = true;
        }
        ImGui::SetItemTooltip("Waits until the state reaches a point of its cycle: 1 is its end");
        if (exit)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##exit", &transition.exitTime, 0.01f, 0.0f, 10.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        else if (transition.conditions.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("at the end, without conditions");
        }
        endProperties();
    }

    ImGui::SeparatorText("Conditions");
    std::optional<std::size_t> removed;
    const float valueWidth = ImGui::GetFontSize() * 4.0f;
    const float testWidth = ImGui::GetFontSize() * 5.5f;
    for (std::size_t condition = 0; condition < transition.conditions.size(); ++condition)
    {
        asset::AnimatorCondition& item = transition.conditions[condition];
        ImGui::PushID(static_cast<int>(condition));
        const asset::AnimatorParameter* parameter = animator.findParameter(item.parameter);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - testWidth - valueWidth - toolButtonWidth() -
                                ImGui::GetStyle().ItemSpacing.x * 3.0f);
        if (parameterCombo("##parameter", animator, item.parameter, false))
        {
            parameter = animator.findParameter(item.parameter);
            if (parameter != nullptr && !asset::testFits(item.test, parameter->type))
            {
                item.test = firstFittingTest(parameter->type);
            }
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(testWidth);
        if (beginCombo("##test", testLabel(item.test)))
        {
            for (const AnimatorTest test : {AnimatorTest::Greater, AnimatorTest::Less, AnimatorTest::Equals, AnimatorTest::NotEquals,
                                            AnimatorTest::IsTrue, AnimatorTest::IsFalse, AnimatorTest::Triggered})
            {
                if (parameter != nullptr && !asset::testFits(test, parameter->type))
                {
                    continue;
                }
                if (ImGui::Selectable(testLabel(test), test == item.test))
                {
                    item.test = test;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(valueWidth);
        if (item.test <= AnimatorTest::NotEquals)
        {
            if (parameter != nullptr && parameter->type == AnimatorParameterType::Integer)
            {
                int whole = static_cast<int>(std::lround(item.value));
                if (ImGui::DragInt("##value", &whole))
                {
                    item.value = static_cast<float>(whole);
                }
            }
            else
            {
                ImGui::DragFloat("##value", &item.value, 0.01f, 0.0f, 0.0f, "%.2f");
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        else
        {
            ImGui::Dummy(ImVec2(valueWidth, ImGui::GetFrameHeight()));
        }
        ImGui::SameLine();
        if (toolButton("remove", icons::Trash, "Remove the condition"))
        {
            removed = condition;
        }
        ImGui::PopID();
    }
    if (removed)
    {
        transition.conditions.erase(transition.conditions.begin() + static_cast<std::ptrdiff_t>(*removed));
        changed = true;
    }
    if (animator.parameters.empty())
    {
        ImGui::TextDisabled("Add parameters in the Animator panel to check them here.");
    }
    else if (labelButton(icons::Plus, "Add Condition"))
    {
        const asset::AnimatorParameter& first = animator.parameters.front();
        transition.conditions.push_back({.parameter = first.name, .test = firstFittingTest(first.type)});
        changed = true;
    }

    ImGui::SeparatorText("Order");
    ImGui::TextWrapped("Transitions are checked in order: the first one whose conditions are met is taken.");
    if (labelButton(icons::ArrowUpDown, "Earlier", 0.0f, index > 0))
    {
        std::swap(animator.transitions[index], animator.transitions[index - 1]);
        editor.index = static_cast<std::int32_t>(index - 1);
        commit(state);
        return;
    }
    ImGui::SameLine();
    if (labelButton(icons::ArrowUpDown, "Later", 0.0f, index + 1 < animator.transitions.size()))
    {
        std::swap(animator.transitions[index], animator.transitions[index + 1]);
        editor.index = static_cast<std::int32_t>(index + 1);
        commit(state);
        return;
    }
    ImGui::Spacing();
    if (labelButton(icons::Trash, "Delete Transition"))
    {
        deleteSelection(state);
        return;
    }
    if (changed)
    {
        commit(state);
    }
}

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

bool drawAnimatorElementInspector(ToolsState& state)
{
    AnimatorEditor& editor = state.animatorEditor;
    // Choosing another entity or asset gives the inspector back to it.
    if (editor.inspecting && (state.selection.active() != editor.inspectedEntity || state.selectedAsset != editor.inspectedAsset))
    {
        editor.inspecting = false;
    }
    clampSelection(editor);
    if (!editor.inspecting || !state.showAnimator ||
        (editor.selected != AnimatorElement::State && editor.selected != AnimatorElement::Transition))
    {
        return false;
    }
    const ThemeColors& colors = themeColors();
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(editor.asset) : nullptr;
    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Workflow, colors.animation);
    boldText(editor.selected == AnimatorElement::State ? "State" : "Transition");
    ImGui::SameLine();
    ImGui::TextDisabled("in %s", info != nullptr ? info->name.c_str() : "the animator");
    alignRight(toolButtonWidth());
    if (toolButton("back", icons::Close, "Show the selection again"))
    {
        editor.inspecting = false;
        return false;
    }
    ImGui::Separator();
    if (editor.selected == AnimatorElement::State)
    {
        drawStateInspector(state, static_cast<std::size_t>(editor.index));
    }
    else
    {
        drawTransitionInspector(state, static_cast<std::size_t>(editor.index));
    }
    if (!editor.error.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(uiColor(colors.error), "%s", editor.error.c_str());
    }
    return true;
}

void drawAnimatorInspector(ToolsState& state)
{
    const ThemeColors& colors = themeColors();
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(state.selectedAsset) : nullptr;
    const std::optional<asset::SourceFile> source =
        state.database != nullptr ? state.database->sourceOf(state.selectedAsset) : std::nullopt;
    if (info == nullptr || !source)
    {
        state.selectedAsset = {};
        return;
    }
    loadAnimator(state, state.selectedAsset);
    const AnimatorData& animator = state.animatorEditor.animator;
    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Workflow, colors.animation);
    boldText(info->name.c_str());
    ImGui::TextDisabled("%s", source->path.c_str());
    ImGui::Spacing();
    if (!state.animatorEditor.error.empty())
    {
        ImGui::TextColored(uiColor(colors.error), "%s", state.animatorEditor.error.c_str());
    }
    ImGui::Text("%zu states, %zu transitions, %zu parameters", animator.states.size(), animator.transitions.size(),
                animator.parameters.size());
    if (!animator.entry.empty())
    {
        ImGui::Text("Starts in %s", animator.entry.c_str());
    }
    ImGui::Spacing();
    if (labelButton(icons::Workflow, "Open in the Animator Panel"))
    {
        state.showAnimator = true;
        ImGui::SetWindowFocus(animatorWindow);
    }
    ImGui::Spacing();
    ImGui::TextWrapped("An Animator component plays it when its Controller names it; game code sets its parameters "
                       "(Animation.SetFloat, SetBool, SetTrigger in C#).");
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
