// The Animator panel, made with the interface of the engine: the parameters of a controller at the left,
// and the graph of its states and of the transitions between them, whose links are lines of that
// interface. The pages the inspector shows for a state or a transition are at the end.
#include "InspectorUi.hpp"
#include "SettingsUi.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/animation/AnimationWorld.hpp>
#include <devex/asset/import/AnimatorFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/AnimationComponents.hpp>

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <format>
#include <iterator>
#include <optional>
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

constexpr std::uint32_t animatorSurface = 19;
// The size of nodes in the graph, in units that are pixels at a zoom of 1 with a font of 16 pixels.
constexpr math::Vec2 stateSize{170.0f, 48.0f};
constexpr math::Vec2 markerSize{110.0f, 30.0f};
constexpr std::size_t maxUndo = 200;

[[nodiscard]] std::filesystem::file_time_type writeTime(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    return error ? std::filesystem::file_time_type{} : time;
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

// ---- The graph ----

// Where the graph stands in its area: its origin there, in units of the panel, and how many of them
// one unit of the graph takes.
struct Graph
{
    math::Vec2 origin{0.0f};
    float scale = 1.0f;

    [[nodiscard]] math::Vec2 toArea(math::Vec2 point) const noexcept
    {
        return origin + point * scale;
    }

    [[nodiscard]] math::Vec2 toGraph(math::Vec2 point) const noexcept
    {
        return (point - origin) / scale;
    }
};

struct Node
{
    AnimatorElement element = AnimatorElement::None;
    std::int32_t index = -1;
    math::Vec2 center{0.0f};
    math::Vec2 half{0.0f};

    [[nodiscard]] bool contains(math::Vec2 point) const noexcept
    {
        return std::abs(point.x - center.x) <= half.x && std::abs(point.y - center.y) <= half.y;
    }

    // Where a line from the center towards a direction leaves the node.
    [[nodiscard]] math::Vec2 edge(math::Vec2 direction) const noexcept
    {
        const float x = std::abs(direction.x) > 1e-6f ? half.x / std::abs(direction.x) : FLT_MAX;
        const float y = std::abs(direction.y) > 1e-6f ? half.y / std::abs(direction.y) : FLT_MAX;
        return center + direction * std::min(x, y);
    }
};

struct Arrow
{
    std::int32_t transition = -1;
    math::Vec2 start{0.0f};
    math::Vec2 end{0.0f};
};

[[nodiscard]] float distanceToLink(math::Vec2 point, math::Vec2 start, math::Vec2 end) noexcept
{
    const math::Vec2 segment = end - start;
    const float length2 = segment.x * segment.x + segment.y * segment.y;
    const float along =
        length2 > 0.0f ? std::clamp(((point.x - start.x) * segment.x + (point.y - start.y) * segment.y) / length2, 0.0f, 1.0f) : 0.0f;
    return math::length(point - (start + segment * along));
}

// The nodes of a controller, where the graph puts them in its area: Entry, Any State, then its states.
[[nodiscard]] std::vector<Node> nodesOf(const AnimatorData& animator, const Graph& graph)
{
    std::vector<Node> nodes;
    nodes.reserve(animator.states.size() + 2);
    nodes.push_back({AnimatorElement::Entry, -1, graph.toArea(animator.entryPosition), markerSize * 0.5f * graph.scale});
    nodes.push_back({AnimatorElement::AnyState, -1, graph.toArea(animator.anyStatePosition), markerSize * 0.5f * graph.scale});
    for (std::size_t index = 0; index < animator.states.size(); ++index)
    {
        nodes.push_back({AnimatorElement::State, static_cast<std::int32_t>(index), graph.toArea(animator.states[index].graphPosition),
                         stateSize * 0.5f * graph.scale});
    }
    return nodes;
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
        math::Vec2 direction = to->center - from->center;
        const float size = math::length(direction);
        if (size < 1.0f)
        {
            continue;
        }
        direction = direction / size;
        const math::Vec2 side = math::Vec2{-direction.y, direction.x} * (6.0f * scale);
        const Node shiftedFrom{from->element, from->index, from->center + side, from->half};
        const Node shiftedTo{to->element, to->index, to->center + side, to->half};
        arrows.push_back({static_cast<std::int32_t>(index), shiftedFrom.edge(direction), shiftedTo.edge(direction * -1.0f)});
    }
    return arrows;
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
            state.focusAnimator = true;
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

// ---- The panel ----

using scene::UiRect;
using namespace rects;

struct AnimatorUi : FormUi
{
    AnimatorUi()
        : FormUi(animatorSurface)
    {
    }

    // A node of the graph as it shows: a frame in the colour of its border, its inside, its two lines
    // of text and how far its state is while the game plays.
    struct NodeView
    {
        Entity border;
        Entity fill;
        Entity title;
        Entity subtitle;
        Entity progress;
    };
    // A parameter in the list: its type, its name, and its value as its type edits it.
    struct ParameterRow
    {
        Entity row;
        Entity name;
        Entity number;
        Entity flag;
        Button fire;
    };

    bool built = false;
    float builtFont = 0.0f;

    Entity toolbar;
    Entity glyph;
    Entity title;
    Entity liveText;
    Button undo;
    Button redo;
    Button frame;
    Entity hint;
    Entity message;

    Entity body;
    Entity side;
    Button addParameter;
    Entity liveNote;
    Entity list;
    Entity rows;
    Entity emptyNote;
    Entity addMenu;
    std::array<Button, 4> addItems{};
    Entity rowMenu;
    Button removeItem;
    std::vector<ParameterRow> parameterRows;
    std::string parametersKey;
    // A value dragged or typed, saved once the pointer and the keys let go of it.
    bool valuesDirty = false;

    Entity graph;
    Entity gridLayer;
    Entity arrowsLayer;
    Entity nodesLayer;
    Entity entryArrow;
    Entity connecting;
    Entity graphHint;
    std::vector<Entity> gridLines;
    std::vector<Entity> arrowViews;
    std::vector<NodeView> nodeViews;
    Entity graphMenu;
    Button newState;
    Button newLinear;
    Button newPlanar;
    Button makeTransition;
    Button setEntry;
    Button removeElement;
    // What the menu of the graph was opened on, and where.
    AnimatorElement menuElement = AnimatorElement::None;
    std::int32_t menuIndex = -1;
    math::Vec2 menuPosition{0.0f};
    // The area of the graph at the last update, and the pointer in it.
    math::Vec2 areaSize{0.0f};
    math::Vec2 pointer{0.0f};
    bool pointed = false;
    std::int32_t pointedTransition = -1;
    // A press of the right or of the middle button that began in the graph.
    bool grabbed = false;

    [[nodiscard]] float barHeight() const noexcept
    {
        return std::round(font * 2.5f);
    }
    // How many units of the panel a unit of the graph takes at a zoom of 1.
    [[nodiscard]] float graphUnit() const noexcept
    {
        return regularFontPixels(font) / 16.0f;
    }
    [[nodiscard]] bool menuOpen()
    {
        ui::UiWorld& world = panel.world();
        return world.isPopupOpen(scene(), graphMenu) || world.isPopupOpen(scene(), addMenu) || world.isPopupOpen(scene(), rowMenu);
    }

    void build(EditorUiKit& kit);
    // A sentence, and another under it, in the place of everything.
    void say(std::string sentence);
    void fillParameters(ToolsState& state, EditorUiKit& kit, bool live);
    void showParameters(ToolsState& state, EditorUiKit& kit, const std::optional<animation::AnimatorStatus>& status);
    void answerParameters(ToolsState& state, const std::optional<animation::AnimatorStatus>& status, scene::Entity followed);
    void showGraph(ToolsState& state, const std::optional<animation::AnimatorStatus>& status);
    void answerGraph(ToolsState& state, EditorUiKit& kit, const ui::LaidOutRect& area, bool menuWasOpen);
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
};

void AnimatorUi::build(EditorUiKit& kit)
{
    ui::UiWorld& world = panel.world();
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        world.closePopup(scene(), child);
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    parameterRows.clear();
    parametersKey.clear();
    gridLines.clear();
    arrowViews.clear();
    nodeViews.clear();
    panel.setKeyboardNavigation(false);
    // Clips come from FileSystem, named by their type.
    panel.setDragIn([](const EditorDrag& payload) -> std::optional<std::pair<std::string, std::string>> {
        if (payload.is(assetPayload, sizeof(AssetPayload)))
        {
            AssetPayload asset;
            std::memcpy(&asset, payload.payload.data(), sizeof(asset));
            return std::pair{std::format("asset:{}", asset::toString(asset.type)), uuidFromBytes(asset.uuid).toString()};
        }
        return std::nullopt;
    });

    const float edge = std::round(font * 0.5f);
    const float tall = std::round(font * 1.85f);
    const float iconSize = std::round(font * 1.2f);
    const float tool = std::round(font * 1.75f);

    // The toolbar: the controller, what it does in the game, undo, and why it is not saved.
    toolbar = add({}, "Toolbar", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, 0.0f}, .offsetMax = {-edge, barHeight()}});
    scene().add<scene::UiLayout>(toolbar, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.5f, .align = scene::TextAlign::Left});
    glyph = icon(kit, toolbar, middle({iconSize, iconSize}), Icon::Workflow, {});
    title = text(toolbar, middle({font * 6.0f, tall}), "", "text", true);
    liveText = text(toolbar, middle({font * 6.0f, tall}), "", "success");
    undo = toolButton(kit, toolbar, Icon::Undo, middle({tool, tool}));
    tooltip(undo.entity, "Undo (Ctrl+Z)");
    redo = toolButton(kit, toolbar, Icon::Redo, middle({tool, tool}));
    tooltip(redo.entity, "Redo (Ctrl+Y)");
    frame = toolButton(kit, toolbar, Icon::Scan, middle({tool, tool}));
    tooltip(frame.entity, "Frame the graph");
    hint = text(toolbar, grow(tall), "", "dim");

    message = text({}, whole(math::Vec4{edge}), "", "dim", false, scene::TextAlign::Center);
    scene().get<scene::UiText>(message).wrap = true;

    // The parameters at the left of a bar that is dragged, the graph at its right.
    body = add({}, "Body", whole(math::Vec4{0.0f, barHeight(), 0.0f, 0.0f}));
    scene().add<scene::UiSplitter>(body, scene::UiSplitter{.position = std::round(font * 16.0f), .minSize = std::round(font * 9.0f)});
    side = add(body, "Parameters", whole());
    const Entity header = add(side, "Header", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, 0.0f}, .offsetMax = {-edge, tall}});
    text(header, whole(), "Parameters", "text", true);
    addParameter = toolButton(kit, header, Icon::Plus,
                              UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-tool, -tool * 0.5f}, .offsetMax = {0.0f, tool * 0.5f}});
    tooltip(addParameter.entity, "Add a parameter that game code sets");
    liveNote = text(side, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, tall}, .offsetMax = {-edge, tall * 1.9f}},
                    "Live: values of the game", "success");
    list = add(side, "List", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, tall + 4.0f}, .offsetMax = {0.0f, 0.0f}, .clipChildren = true},
               "scroll");
    scene().add<scene::UiScroll>(list, scene::UiScroll{});
    rows = add(list, "Rows", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, 0.0f}, .offsetMax = {-edge - 6.0f, 1.0f}});
    scene().add<scene::UiLayout>(rows, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = 2.0f, .align = scene::TextAlign::Left});
    emptyNote = text(side, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, tall + 6.0f}, .offsetMax = {-edge, tall + 6.0f + font * 5.0f}},
                     "Game code sets parameters, and transitions check them: add one with +.", "dim");
    scene().get<scene::UiText>(emptyNote).wrap = true;
    scene().get<scene::UiText>(emptyNote).verticalAlign = scene::TextVerticalAlign::Top;

    addMenu = menu("New parameter", font * 9.0f);
    const std::array<AnimatorParameterType, 4> types{AnimatorParameterType::Float, AnimatorParameterType::Integer, AnimatorParameterType::Bool,
                                                     AnimatorParameterType::Trigger};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        addItems[index] = menuItem(kit, addMenu, std::nullopt, parameterTypeLabel(types[index]));
    }
    fitMenu(addMenu, font * 9.0f);
    rowMenu = menu("Parameter menu", font * 9.0f);
    removeItem = menuItem(kit, rowMenu, Icon::Trash, "Remove");
    fitMenu(rowMenu, font * 9.0f);

    // The graph: a grid, the links, the nodes over them, and the link being drawn.
    graph = add(body, "Graph", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}, .clipChildren = true},
                "window");
    scene().add<scene::UiImage>(graph);
    const ThemeColors& colors = themeColors();
    scene().add<scene::UiDropTarget>(graph, scene::UiDropTarget{.accepts = {clipDrop()},
                                                                .highlightColor = linearColor(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.08f))});
    gridLayer = add(graph, "Grid", whole());
    arrowsLayer = add(graph, "Links", whole());
    nodesLayer = add(graph, "Nodes", whole());
    entryArrow = add(arrowsLayer, "Entry", whole());
    scene().add<scene::UiLine>(entryArrow, scene::UiLine{.arrow = scene::UiLineArrow::Middle});
    connecting = add(graph, "Link drawn", whole());
    scene().add<scene::UiLine>(connecting, scene::UiLine{.arrow = scene::UiLineArrow::Middle});
    graphHint = text(graph, whole(), "Right-click to add a state, or drop animation clips here.", "dim", false, scene::TextAlign::Center);

    graphMenu = menu("Graph menu", font * 13.0f);
    newState = menuItem(kit, graphMenu, Icon::Plus, "New State");
    newLinear = menuItem(kit, graphMenu, Icon::Sliders, "New Blend Tree 1D");
    tooltip(newLinear.entity, "Clips along a line that a parameter moves on: walk to run");
    newPlanar = menuItem(kit, graphMenu, Icon::Move, "New Blend Tree 2D");
    tooltip(newPlanar.entity, "Clips on a plane that two parameters move on: the directions of a walk");
    makeTransition = menuItem(kit, graphMenu, Icon::Link, "Make Transition");
    tooltip(makeTransition.entity, "Then click the state it goes to");
    setEntry = menuItem(kit, graphMenu, Icon::Play, "Set as Entry State");
    removeElement = menuItem(kit, graphMenu, Icon::Trash, "Delete", "Delete");
}

void AnimatorUi::say(std::string sentence)
{
    scene().get<UiRect>(message).visible = true;
    scene().get<UiRect>(toolbar).visible = false;
    scene().get<UiRect>(body).visible = false;
    scene::UiText& shown = scene().get<scene::UiText>(message);
    if (shown.text != sentence)
    {
        shown.text = std::move(sentence);
    }
}

void AnimatorUi::fillParameters(ToolsState& state, EditorUiKit& kit, bool live)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(rows); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    parameterRows.clear();
    const ThemeColors& colors = themeColors();
    const AnimatorData& animator = state.animatorEditor.animator;
    const float tall = line - 4.0f;
    const float valueWidth = std::round(font * 5.0f);
    for (const asset::AnimatorParameter& parameter : animator.parameters)
    {
        ParameterRow made;
        made.row = add(rows, "Parameter", wide(line));
        scene().add<scene::UiLayout>(made.row, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.35f, .align = scene::TextAlign::Left});
        // Its menu removes it.
        scene().add<scene::UiContextMenu>(made.row, scene::UiContextMenu{.popup = scene().reference(rowMenu)});
        const Entity letter = text(made.row, middle({font * 0.9f, tall}), std::string(1, parameterTypeLabel(parameter.type)[0]), "dim");
        tooltip(letter, parameterTypeLabel(parameter.type));
        made.name = field(made.row, grow(tall), parameter.name, "");
        const Entity value = add(made.row, "Value", middle({valueWidth, tall}));
        switch (parameter.type)
        {
        case AnimatorParameterType::Float:
        case AnimatorParameterType::Integer: {
            const bool integer = parameter.type == AnimatorParameterType::Integer;
            made.number = numberBox(value, {}, colors.textDim,
                                    integer ? scene::UiNumberField{.step = 1.0f, .dragSpeed = 0.1f, .decimals = 0}
                                            : scene::UiNumberField{.dragSpeed = 0.01f, .decimals = 3});
            UiRect& rect = scene().get<UiRect>(made.number);
            rect.anchorMax = {1.0f, 1.0f};
            break;
        }
        case AnimatorParameterType::Bool:
            made.flag = toggle(value);
            break;
        case AnimatorParameterType::Trigger:
            // A trigger has no value to start with: the game fires it, and so does the panel while it plays.
            if (live)
            {
                made.fire = button(kit, value, std::nullopt, "Fire", "button", -1.0f, tall);
            }
            break;
        }
        if (!live && (made.number.isValid() || made.flag.isValid()))
        {
            tooltip(made.number.isValid() ? made.number : made.flag, "The value it starts with");
        }
        parameterRows.push_back(made);
    }
    scene().get<UiRect>(rows).offsetMax.y = (line + 2.0f) * static_cast<float>(animator.parameters.size());
}

void AnimatorUi::showParameters(ToolsState& state, EditorUiKit& kit, const std::optional<animation::AnimatorStatus>& status)
{
    const AnimatorData& animator = state.animatorEditor.animator;
    const bool live = status.has_value() && state.animationWorld != nullptr;
    // The rows are made again when the parameters, their names or their types change.
    std::string key = live ? "live" : "file";
    for (const asset::AnimatorParameter& parameter : animator.parameters)
    {
        std::format_to(std::back_inserter(key), "|{}:{}", static_cast<int>(parameter.type), parameter.name);
    }
    if (key != parametersKey)
    {
        fillParameters(state, kit, live);
        parametersKey = std::move(key);
    }
    const float tall = std::round(font * 1.85f);
    scene().get<UiRect>(liveNote).visible = status.has_value();
    scene().get<UiRect>(list).offsetMin.y = tall + 4.0f + (status ? tall * 0.9f : 0.0f);
    scene().get<UiRect>(emptyNote).visible = animator.parameters.empty();

    for (std::size_t index = 0; index < parameterRows.size() && index < animator.parameters.size(); ++index)
    {
        const asset::AnimatorParameter& parameter = animator.parameters[index];
        const ParameterRow& row = parameterRows[index];
        setText(row.name, parameter.name);
        // The value: the one it starts with, or the one the game gives it while it plays.
        float value = parameter.defaultValue;
        if (live)
        {
            const auto found = std::ranges::find(status->parameters, parameter.name, &animation::AnimatorStatus::Parameter::name);
            value = found != status->parameters.end() ? found->value : value;
        }
        if (row.number.isValid())
        {
            setNumber(row.number, value);
        }
        if (row.flag.isValid())
        {
            setToggle(row.flag, value != 0.0f);
        }
        if (row.fire.entity.isValid())
        {
            relabel(kit, row.fire, value != 0.0f ? "Set" : "Fire");
        }
    }
}

void AnimatorUi::answerParameters(ToolsState& state, const std::optional<animation::AnimatorStatus>& status, scene::Entity followed)
{
    AnimatorEditor& editor = state.animatorEditor;
    AnimatorData& animator = editor.animator;
    ui::UiWorld& world = panel.world();
    const bool live = status.has_value() && state.animationWorld != nullptr;

    // The + opens the list of the types under itself.
    if (world.wasClicked(addParameter.entity))
    {
        const ui::LaidOutRect* const placed = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(addParameter.entity);
        world.openPopup(scene(), addMenu, placed != nullptr ? std::optional(math::Vec2{placed->min.x, placed->max.y}) : std::nullopt);
    }
    const std::array<AnimatorParameterType, 4> types{AnimatorParameterType::Float, AnimatorParameterType::Integer, AnimatorParameterType::Bool,
                                                     AnimatorParameterType::Trigger};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        if (world.wasClicked(addItems[index].entity))
        {
            animator.parameters.push_back(
                {.name = uniqueName(animator.parameters, parameterName, std::format("New {}", parameterTypeLabel(types[index]))), .type = types[index]});
            commit(state);
            return;
        }
    }

    std::optional<std::size_t> removed;
    const Entity target = world.contextTarget();
    for (std::size_t index = 0; index < parameterRows.size() && index < animator.parameters.size(); ++index)
    {
        asset::AnimatorParameter& parameter = animator.parameters[index];
        const ParameterRow& row = parameterRows[index];
        if (world.wasClicked(removeItem.entity) && target == row.row)
        {
            removed = index;
        }
        // The name, renamed everywhere once typed.
        if (endedField == row.name)
        {
            const std::string typed = scene().get<scene::UiText>(row.name).text;
            if (!typed.empty() && typed != parameter.name && animator.findParameter(typed) == nullptr)
            {
                renameParameter(animator, parameter.name, typed);
                commit(state);
                return;
            }
        }
        if (row.number.isValid() && world.wasChanged(row.number))
        {
            const float value = scene().get<scene::UiNumberField>(row.number).value;
            const bool integer = parameter.type == AnimatorParameterType::Integer;
            if (live)
            {
                if (integer)
                {
                    state.animationWorld->setInteger(followed, parameter.name, static_cast<std::int32_t>(std::lround(value)));
                }
                else
                {
                    state.animationWorld->setFloat(followed, parameter.name, value);
                }
            }
            else
            {
                parameter.defaultValue = integer ? std::round(value) : value;
                valuesDirty = true;
            }
        }
        if (row.flag.isValid() && world.wasChanged(row.flag))
        {
            const bool value = scene().get<scene::UiToggle>(row.flag).value;
            if (live)
            {
                state.animationWorld->setBool(followed, parameter.name, value);
            }
            else
            {
                parameter.defaultValue = value ? 1.0f : 0.0f;
                valuesDirty = true;
            }
        }
        if (live && row.fire.entity.isValid() && world.wasClicked(row.fire.entity))
        {
            state.animationWorld->setTrigger(followed, parameter.name);
        }
    }
    if (removed)
    {
        removeParameter(animator, *removed);
        commit(state);
        return;
    }
    // One step of the history for a value, once it is let go.
    if (valuesDirty && !world.held().isValid() && !world.isEditing())
    {
        valuesDirty = false;
        commit(state);
    }
}

void AnimatorUi::showGraph(ToolsState& state, const std::optional<animation::AnimatorStatus>& status)
{
    AnimatorEditor& editor = state.animatorEditor;
    const AnimatorData& animator = editor.animator;
    const ThemeColors& colors = themeColors();
    const float unit = graphUnit();
    if (editor.frame && areaSize.x > 1.0f && areaSize.y > 1.0f)
    {
        // Every node in view, no larger than at a zoom of 1.
        math::Vec2 low = animator.entryPosition - markerSize * 0.5f;
        math::Vec2 high = animator.entryPosition + markerSize * 0.5f;
        const auto include = [&](math::Vec2 center, math::Vec2 size) {
            low = math::min(low, center - size * 0.5f);
            high = math::max(high, center + size * 0.5f);
        };
        include(animator.anyStatePosition, markerSize);
        for (const AnimatorState& node : animator.states)
        {
            include(node.graphPosition, stateSize);
        }
        const math::Vec2 extent = (high - low) + math::Vec2{60.0f, 60.0f};
        editor.zoom = std::clamp(std::min(areaSize.x / (extent.x * unit), areaSize.y / (extent.y * unit)), 0.35f, 1.0f);
        editor.pan = areaSize * 0.5f - (low + high) * 0.5f * (editor.zoom * unit);
        editor.frame = false;
    }
    const Graph view{editor.pan, editor.zoom * unit};

    // The grid, a line in two once they come too close.
    float step = 32.0f * view.scale;
    while (step < 18.0f)
    {
        step *= 2.0f;
    }
    const math::Vec4 gridColor = linearColor(ImVec4(colors.border.x, colors.border.y, colors.border.z, 0.16f));
    std::size_t linesUsed = 0;
    const auto gridLine = [&](math::Vec2 from, math::Vec2 to) {
        if (linesUsed == gridLines.size())
        {
            const Entity made = add(gridLayer, "Line", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}});
            scene().add<scene::UiImage>(made, scene::UiImage{.raycastTarget = false});
            gridLines.push_back(made);
        }
        const Entity shown = gridLines[linesUsed++];
        UiRect& rect = scene().get<UiRect>(shown);
        rect.visible = true;
        rect.offsetMin = from;
        rect.offsetMax = to;
        scene().get<scene::UiImage>(shown).color = gridColor;
    };
    for (float x = std::fmod(view.origin.x, step); x < areaSize.x && linesUsed < 400; x += step)
    {
        gridLine({std::round(x), 0.0f}, {std::round(x) + 1.0f, areaSize.y});
    }
    for (float y = std::fmod(view.origin.y, step); y < areaSize.y && linesUsed < 400; y += step)
    {
        gridLine({0.0f, std::round(y)}, {areaSize.x, std::round(y) + 1.0f});
    }
    for (std::size_t index = linesUsed; index < gridLines.size(); ++index)
    {
        scene().get<UiRect>(gridLines[index]).visible = false;
    }

    // The links: the one selected in the accent, the one the game takes in green.
    const std::vector<Node> nodes = nodesOf(animator, view);
    const std::vector<Arrow> arrows = arrowsOf(animator, nodes, view.scale);
    const std::string current = status ? status->state : std::string{};
    const std::string previous = status ? status->previousState : std::string{};
    const auto link = [&](Entity entity, math::Vec2 from, math::Vec2 to, ImVec4 color, float width) {
        scene().get<UiRect>(entity).visible = true;
        scene::UiLine& drawn = scene().get<scene::UiLine>(entity);
        drawn.points.assign({from, to});
        drawn.width = width;
        drawn.color = linearColor(color);
        drawn.arrowSize = 11.2f * view.scale;
    };
    for (std::size_t index = 0; index < arrows.size(); ++index)
    {
        if (index == arrowViews.size())
        {
            const Entity made = add(arrowsLayer, "Link", whole());
            scene().add<scene::UiLine>(made, scene::UiLine{.arrow = scene::UiLineArrow::Middle});
            arrowViews.push_back(made);
        }
        const Arrow& arrow = arrows[index];
        const AnimatorTransition& transition = animator.transitions[static_cast<std::size_t>(arrow.transition)];
        const bool selected = editor.selected == AnimatorElement::Transition && editor.index == arrow.transition;
        const bool running = !previous.empty() && transition.to == current && (transition.from == previous || transition.from.empty());
        const ImVec4 color = selected ? colors.accent : running ? colors.success : arrow.transition == pointedTransition ? colors.text : colors.textDim;
        link(arrowViews[index], arrow.start, arrow.end, color, (selected || running ? 2.5f : 1.5f) * view.scale);
    }
    for (std::size_t index = arrows.size(); index < arrowViews.size(); ++index)
    {
        scene().get<UiRect>(arrowViews[index]).visible = false;
    }
    // Entry points at the entry state.
    scene().get<UiRect>(entryArrow).visible = false;
    if (const AnimatorState* const entry = animator.findState(animator.entry))
    {
        const Node& from = nodes[0];
        const Node to{AnimatorElement::State, -1, view.toArea(entry->graphPosition), stateSize * 0.5f * view.scale};
        math::Vec2 direction = to.center - from.center;
        if (const float length = math::length(direction); length > 1.0f)
        {
            direction = direction / length;
            link(entryArrow, from.edge(direction), to.edge(direction * -1.0f), colors.warning, 1.5f * view.scale);
        }
    }
    // The link being drawn follows the pointer.
    scene().get<UiRect>(connecting).visible = false;
    if (editor.connectingFrom)
    {
        const auto from = std::ranges::find_if(nodes, [&](const Node& node) {
            return *editor.connectingFrom >= 0 ? node.element == AnimatorElement::State && node.index == *editor.connectingFrom
                                               : node.element == AnimatorElement::AnyState;
        });
        if (from == nodes.end())
        {
            editor.connectingFrom.reset();
        }
        else if (pointed)
        {
            link(connecting, from->center, pointer, colors.accent, 2.0f * view.scale);
        }
    }

    // The nodes, each in a view taken from the reserve.
    const float rounding = 6.0f * view.scale;
    const float textSize = font * editor.zoom;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (index == nodeViews.size())
        {
            NodeView made;
            made.border = add(nodesLayer, "Node", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}});
            scene().add<scene::UiImage>(made.border, scene::UiImage{.raycastTarget = false});
            made.fill = add(made.border, "Inside", whole());
            scene().get<UiRect>(made.fill).clipChildren = true;
            scene().add<scene::UiImage>(made.fill, scene::UiImage{.raycastTarget = false});
            made.title = text(made.fill, whole(), "", "text", true, scene::TextAlign::Center);
            made.subtitle = text(made.fill, whole(), "", "dim", false, scene::TextAlign::Center);
            scene().get<scene::UiText>(made.subtitle).verticalAlign = scene::TextVerticalAlign::Top;
            made.progress = add(made.border, "Progress", UiRect{.anchorMin = {0.0f, 1.0f}, .anchorMax = {0.0f, 1.0f}});
            scene().add<scene::UiImage>(made.progress, scene::UiImage{.raycastTarget = false});
            nodeViews.push_back(made);
        }
        const Node& node = nodes[index];
        const NodeView& shown = nodeViews[index];
        const bool selected = editor.selected == node.element && (node.element != AnimatorElement::State || editor.index == node.index);
        std::string_view name;
        std::string summary;
        ImVec4 fill = colors.panel;
        ImVec4 border = colors.border;
        float progress = -1.0f;
        if (node.element == AnimatorElement::Entry)
        {
            name = "Entry";
            fill = ImVec4(colors.warning.x * 0.45f, colors.warning.y * 0.45f, colors.warning.z * 0.45f, 1.0f);
        }
        else if (node.element == AnimatorElement::AnyState)
        {
            name = "Any State";
            fill = ImVec4(colors.animation.x * 0.4f, colors.animation.y * 0.4f, colors.animation.z * 0.4f, 1.0f);
        }
        else
        {
            const AnimatorState& animatorState = animator.states[static_cast<std::size_t>(node.index)];
            name = animatorState.name;
            summary = motionSummary(state, animatorState);
            if (animatorState.name == animator.entry)
            {
                border = colors.warning;
            }
            if (status && animatorState.name == current)
            {
                fill = ImVec4(colors.success.x * 0.35f, colors.success.y * 0.35f, colors.success.z * 0.35f, 1.0f);
                progress = animatorState.loop ? status->normalizedTime - std::floor(status->normalizedTime) : std::min(status->normalizedTime, 1.0f);
            }
        }
        if (selected)
        {
            border = colors.accent;
        }
        const float thickness = std::max((selected ? 2.5f : 1.5f) * view.scale, 1.0f);
        UiRect& frameRect = scene().get<UiRect>(shown.border);
        frameRect.visible = true;
        frameRect.offsetMin = node.center - node.half;
        frameRect.offsetMax = node.center + node.half;
        scene::UiImage& frameImage = scene().get<scene::UiImage>(shown.border);
        frameImage.color = linearColor(border);
        frameImage.cornerRadius = rounding;
        UiRect& inside = scene().get<UiRect>(shown.fill);
        inside.offsetMin = {thickness, thickness};
        inside.offsetMax = {-thickness, -thickness};
        scene::UiImage& insideImage = scene().get<scene::UiImage>(shown.fill);
        // The border is a line of its own on a panel: over the colour of the node, its alpha would hide it.
        insideImage.color = linearColor(fill);
        insideImage.cornerRadius = std::max(rounding - thickness, 0.0f);

        // The name, over what the state plays when it says so.
        scene::UiText& nameText = scene().get<scene::UiText>(shown.title);
        if (nameText.text != name)
        {
            nameText.text = std::string(name);
        }
        nameText.size = textSize;
        nameText.verticalAlign = summary.empty() ? scene::TextVerticalAlign::Middle : scene::TextVerticalAlign::Bottom;
        scene().get<UiRect>(shown.title).anchorMax = {1.0f, summary.empty() ? 1.0f : 0.5f};
        UiRect& summaryRect = scene().get<UiRect>(shown.subtitle);
        summaryRect.visible = !summary.empty();
        summaryRect.anchorMin = {0.0f, 0.5f};
        summaryRect.offsetMin = {0.0f, view.scale};
        scene::UiText& summaryText = scene().get<scene::UiText>(shown.subtitle);
        if (summaryText.text != summary)
        {
            summaryText.text = std::move(summary);
        }
        summaryText.size = textSize * 0.85f;

        UiRect& bar = scene().get<UiRect>(shown.progress);
        bar.visible = progress >= 0.0f;
        if (bar.visible)
        {
            const float height = 3.0f * view.scale;
            bar.offsetMin = {rounding, -height * 2.0f};
            bar.offsetMax = {rounding + (node.half.x * 2.0f - rounding * 2.0f) * progress, -height};
            scene().get<scene::UiImage>(shown.progress).color = linearColor(colors.success);
        }
    }
    for (std::size_t index = nodes.size(); index < nodeViews.size(); ++index)
    {
        scene().get<UiRect>(nodeViews[index].border).visible = false;
    }
    scene().get<UiRect>(graphHint).visible = animator.states.empty();
}

void AnimatorUi::answerGraph(ToolsState& state, EditorUiKit& kit, const ui::LaidOutRect& area, bool menuWasOpen)
{
    AnimatorEditor& editor = state.animatorEditor;
    AnimatorData& animator = editor.animator;
    ui::UiWorld& world = panel.world();
    const ui::UiInput& input = panel.input();
    areaSize = area.size();
    const math::Vec2 mouse = input.pointer - area.min;
    pointer = mouse;
    const bool inside = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < areaSize.x && mouse.y < areaSize.y;
    pointed = panel.hovered() && inside;
    // A menu takes the pointer while it is open, and the press that closes it.
    const bool hovered = pointed && !menuWasOpen && !menuOpen();
    const Graph view{editor.pan, editor.zoom * graphUnit()};
    const float unitsPerPoint = panel.unitsOf(ImVec2(1.0f, 0.0f)).x - panel.unitsOf(ImVec2(0.0f, 0.0f)).x;

    const std::vector<Node> nodes = nodesOf(animator, view);
    const std::vector<Arrow> arrows = arrowsOf(animator, nodes, view.scale);
    const Node* hoveredNode = nullptr;
    for (const Node& node : nodes)
    {
        hoveredNode = pointed && node.contains(mouse) ? &node : hoveredNode;
    }
    const Arrow* hoveredArrow = nullptr;
    if (pointed && hoveredNode == nullptr)
    {
        for (const Arrow& arrow : arrows)
        {
            if (distanceToLink(mouse, arrow.start, arrow.end) < 6.0f)
            {
                hoveredArrow = &arrow;
            }
        }
    }
    pointedTransition = hovered && hoveredArrow != nullptr ? hoveredArrow->transition : -1;

    // Clips dropped on the graph become states; on a state, its clip.
    if (const ui::Drop* const drop = world.dropped(); drop != nullptr && drop->target == graph)
    {
        if (const std::optional<core::Uuid> uuid = core::Uuid::parse(drop->data))
        {
            const asset::AssetId clip{*uuid};
            if (hoveredNode != nullptr && hoveredNode->element == AnimatorElement::State)
            {
                AnimatorState& target = animator.states[static_cast<std::size_t>(hoveredNode->index)];
                if (target.blend == AnimatorBlend::None && !target.motions.empty())
                {
                    target.motions.front().clip = clip;
                }
                else
                {
                    target.motions.push_back({.clip = clip, .threshold = target.motions.empty() ? 0.0f : target.motions.back().threshold + 1.0f});
                }
                select(state, AnimatorElement::State, hoveredNode->index);
                commit(state);
            }
            else
            {
                const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(clip) : nullptr;
                addState(state, view.toGraph(mouse), AnimatorBlend::None, clip, info != nullptr ? info->name : "New State");
            }
            return;
        }
    }

    // Selection, moves, transitions drawn from a state.
    if (hovered && input.pointerPressed)
    {
        if (editor.connectingFrom)
        {
            if (hoveredNode != nullptr && hoveredNode->element == AnimatorElement::State && hoveredNode->index != *editor.connectingFrom)
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
        if (input.pointerDown)
        {
            const math::Vec2 moved = math::Vec2{state.input.mouseDelta().x, state.input.mouseDelta().y} * (unitsPerPoint / view.scale);
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

    // The view moves with the middle or the right button, and zooms around the pointer.
    if (hovered && (state.input.clicked(Mouse::Right) || state.input.clicked(Mouse::Middle)))
    {
        grabbed = true;
    }
    if (grabbed && (state.input.dragging(Mouse::Middle) || state.input.dragging(Mouse::Right)))
    {
        editor.pan += math::Vec2{state.input.mouseDelta().x, state.input.mouseDelta().y} * unitsPerPoint;
        editor.panning = true;
    }
    if (hovered && input.wheel != 0.0f)
    {
        const math::Vec2 under = view.toGraph(mouse);
        editor.zoom = std::clamp(editor.zoom * std::pow(1.15f, input.wheel), 0.35f, 2.5f);
        editor.pan = mouse - under * (editor.zoom * graphUnit());
    }
    // A right click that did not move the view opens the menu of what it is on.
    if (grabbed && state.input.released(Mouse::Right))
    {
        if (hovered && !editor.panning && !editor.connectingFrom)
        {
            menuElement = hoveredNode != nullptr ? hoveredNode->element : hoveredArrow != nullptr ? AnimatorElement::Transition : AnimatorElement::None;
            menuIndex = hoveredNode != nullptr ? hoveredNode->index : hoveredArrow != nullptr ? hoveredArrow->transition : -1;
            menuPosition = view.toGraph(mouse);
            if (menuElement != AnimatorElement::None)
            {
                select(state, menuElement, menuIndex);
            }
            const bool onState = menuElement == AnimatorElement::State && menuIndex >= 0 && static_cast<std::size_t>(menuIndex) < animator.states.size();
            const auto shown = [&](const Button& item, bool visible) { scene().get<UiRect>(item.entity).visible = visible; };
            shown(newState, menuElement == AnimatorElement::None);
            shown(newLinear, menuElement == AnimatorElement::None);
            shown(newPlanar, menuElement == AnimatorElement::None);
            shown(makeTransition, menuElement == AnimatorElement::State || menuElement == AnimatorElement::AnyState);
            shown(setEntry, onState);
            if (onState)
            {
                enable(setEntry, animator.states[static_cast<std::size_t>(menuIndex)].name != animator.entry);
            }
            shown(removeElement, menuElement == AnimatorElement::State || menuElement == AnimatorElement::Transition);
            // Entry has nothing to offer.
            if (menuElement != AnimatorElement::Entry)
            {
                fitMenu(graphMenu, font * 13.0f);
                world.openPopup(scene(), graphMenu, input.pointer);
            }
        }
        editor.connectingFrom.reset();
    }
    if (!state.input.down(Mouse::Right) && !state.input.down(Mouse::Middle))
    {
        editor.panning = false;
        grabbed = false;
    }
    if (editor.connectingFrom && panel.focused() && state.input.pressed(platform::Key::Escape, true))
    {
        editor.connectingFrom.reset();
    }

    // What a link asks for, next to the pointer.
    if (hovered && hoveredArrow != nullptr)
    {
        const AnimatorTransition& transition = animator.transitions[static_cast<std::size_t>(hoveredArrow->transition)];
        kit.showTooltip(std::format("{} to {}\n{}", transition.from.empty() ? "Any State" : transition.from, transition.to, transitionText(transition)),
                        pointOf(state.input.mouse()));
    }

    // The menu of the graph.
    if (world.wasClicked(newState.entity))
    {
        addState(state, menuPosition, AnimatorBlend::None);
    }
    else if (world.wasClicked(newLinear.entity))
    {
        addState(state, menuPosition, AnimatorBlend::Linear);
    }
    else if (world.wasClicked(newPlanar.entity))
    {
        addState(state, menuPosition, AnimatorBlend::Planar);
    }
    else if (world.wasClicked(makeTransition.entity))
    {
        editor.connectingFrom = menuElement == AnimatorElement::State ? menuIndex : -1;
    }
    else if (world.wasClicked(setEntry.entity) && menuIndex >= 0 && static_cast<std::size_t>(menuIndex) < animator.states.size())
    {
        animator.entry = animator.states[static_cast<std::size_t>(menuIndex)].name;
        commit(state);
    }
    else if (world.wasClicked(removeElement.entity))
    {
        deleteSelection(state);
    }
}

void AnimatorUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        setFont(state.theme.fontSize);
        build(kit);
    }
    styleTooltips(colors);
    AnimatorEditor& editor = state.animatorEditor;
    ui::UiWorld& world = panel.world();
    const float zoom = UiPanel::zoomFor(font);
    const auto sayOnly = [&](std::string sentence) {
        say(std::move(sentence));
        panel.update(kit, delta, zoom);
        editor.focused = panel.focused();
    };
    if (state.database == nullptr)
    {
        sayOnly("Open a project to edit its animators.");
        return;
    }

    // What the panel edits: the controller of the selected Animator, or the selected animator asset.
    asset::AssetId target;
    const scene::Entity selected = animatorOf(edited, edited.findEntity(state.selection.active()));
    if (selected.isValid())
    {
        target = edited.get<scene::Animator>(selected).controller;
        editor.entity = edited.uuid(selected);
        if (!target.isValid())
        {
            sayOnly(std::format("The Animator of {} has no controller.\nChoose one in its Controller field, or make one with New Animator in the FileSystem.",
                                edited.name(selected)));
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
        sayOnly("Select an entity with an Animator, or an animator in the FileSystem.");
        return;
    }
    loadAnimator(state, target);
    scene().get<UiRect>(message).visible = false;
    scene().get<UiRect>(toolbar).visible = true;
    scene().get<UiRect>(body).visible = true;

    // While the game plays, the state machine of the entity followed shows live.
    std::optional<animation::AnimatorStatus> status;
    const scene::Entity followed = edited.findEntity(editor.entity);
    if (state.playState != PlayState::Editing && state.animationWorld != nullptr && followed.isValid() && edited.has<scene::Animator>(followed) &&
        edited.get<scene::Animator>(followed).controller == editor.asset)
    {
        status = state.animationWorld->status(followed);
    }

    // The toolbar: the asset, undo, and why it is not saved.
    const asset::AssetInfo* const info = state.database->find(editor.asset);
    scene().get<scene::UiImage>(glyph).color = linearColor(colors.animation);
    fitText(kit, title, info != nullptr ? info->name : std::string("Animator"), true);
    scene().get<UiRect>(liveText).visible = followed.isValid() && status.has_value();
    if (followed.isValid() && status)
    {
        fitText(kit, liveText, std::format("{}: {}", edited.name(followed), status->state));
    }
    enable(undo, !editor.undo.empty());
    enable(redo, !editor.redo.empty());
    {
        scene::UiText& shown = scene().get<scene::UiText>(hint);
        const std::string_view said =
            !editor.error.empty() ? std::string_view(editor.error) : "Right-click: add states and transitions. Middle or right drag: move the view.";
        if (shown.text != said)
        {
            shown.text = std::string(said);
        }
        scene().get<UiRect>(hint).style = editor.error.empty() ? "dim" : "error";
    }
    showParameters(state, kit, status);
    showGraph(state, status);

    const bool menuWasOpen = menuOpen();
    panel.update(kit, delta, zoom);
    answerForm();
    editor.focused = panel.focused();

    if (world.wasClicked(undo.entity))
    {
        undoAnimator(state);
    }
    else if (world.wasClicked(redo.entity))
    {
        redoAnimator(state);
    }
    if (world.wasClicked(frame.entity))
    {
        editor.frame = true;
    }
    answerParameters(state, status, followed);
    if (const ui::LaidOutRect* const area = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(graph))
    {
        answerGraph(state, kit, *area, menuWasOpen);
    }

    // Shortcuts, while no field takes the keys.
    if (editor.focused && !world.isEditing())
    {
        if (state.input.chord(KeyModifiers{.ctrl = true}, 'z'))
        {
            undoAnimator(state);
        }
        if (state.input.chord(KeyModifiers{.ctrl = true}, 'y') || state.input.chord(KeyModifiers{.ctrl = true, .shift = true}, 'z'))
        {
            redoAnimator(state);
        }
        if (state.input.pressed(platform::Key::Delete, false))
        {
            deleteSelection(state);
        }
    }
}

void drawAnimatorPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Animator panel");
    AnimatorEditor& editor = state.animatorEditor;
    if (!state.showAnimator)
    {
        editor.focused = false;
        return;
    }
    if (std::exchange(state.focusAnimator, false))
    {
        focusPanel(state, animatorWindow);
    }
    if (!beginDockedPanel(state, animatorWindow))
    {
        editor.focused = false;
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.animatorUi)
    {
        state.animatorUi = std::make_shared<AnimatorUi>();
    }
    state.animatorUi->update(state, kit, scene, core::Duration(state.input.delta()));
    endDockedPanel(state);
}

void renderAnimatorPanel(ToolsState& state, render::RenderWorld& world)
{
    if (state.animatorUi && state.uiKit)
    {
        state.animatorUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

std::unique_ptr<InspectorPage> makeAnimatorElementPage()
{
    return std::make_unique<AnimatorElementPage>();
}

std::unique_ptr<InspectorPage> makeAnimatorPage()
{
    return std::make_unique<AnimatorPage>();
}

core::Result<std::filesystem::path> createAnimatorFile(ToolsState& state, std::string_view folder, std::string_view name)
{
    AnimatorData animator;
    animator.entry = "Idle";
    animator.states.push_back({.name = "Idle", .graphPosition = {0.0f, 0.0f}});
    return writeNewAssetFile(state, folder, name, "Animator", asset::animatorExtension, asset::writeAnimatorFile(animator));
}

} // namespace devex::tools::detail
