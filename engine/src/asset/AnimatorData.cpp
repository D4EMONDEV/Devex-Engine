#include <devex/asset/AnimatorData.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <unordered_set>
#include <utility>

namespace devex::asset {
namespace {

constexpr std::array<std::pair<AnimatorParameterType, std::string_view>, 4> parameterTypeNames{{
    {AnimatorParameterType::Float, "float"},
    {AnimatorParameterType::Integer, "int"},
    {AnimatorParameterType::Bool, "bool"},
    {AnimatorParameterType::Trigger, "trigger"},
}};

constexpr std::array<std::pair<AnimatorBlend, std::string_view>, 3> blendNames{{
    {AnimatorBlend::None, "none"},
    {AnimatorBlend::Linear, "1d"},
    {AnimatorBlend::Planar, "2d"},
}};

constexpr std::array<std::pair<AnimatorLayerBlend, std::string_view>, 2> layerBlendNames{{
    {AnimatorLayerBlend::Override, "override"},
    {AnimatorLayerBlend::Additive, "additive"},
}};

constexpr std::array<std::pair<AnimatorTest, std::string_view>, 7> testNames{{
    {AnimatorTest::Greater, "greater"},
    {AnimatorTest::Less, "less"},
    {AnimatorTest::Equals, "equals"},
    {AnimatorTest::NotEquals, "not_equals"},
    {AnimatorTest::IsTrue, "is_true"},
    {AnimatorTest::IsFalse, "is_false"},
    {AnimatorTest::Triggered, "triggered"},
}};

template <typename T, std::size_t Count>
[[nodiscard]] std::string_view nameOf(const std::array<std::pair<T, std::string_view>, Count>& names, T value) noexcept
{
    for (const auto& [candidate, name] : names)
    {
        if (candidate == value)
        {
            return name;
        }
    }
    return names.front().second;
}

template <typename T, std::size_t Count>
[[nodiscard]] std::optional<T> valueOf(const std::array<std::pair<T, std::string_view>, Count>& names,
                                       std::string_view text) noexcept
{
    for (const auto& [value, name] : names)
    {
        if (name == text)
        {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool isNumber(AnimatorParameterType type) noexcept
{
    return type == AnimatorParameterType::Float || type == AnimatorParameterType::Integer;
}

} // namespace

const AnimatorState* AnimatorLayer::findState(std::string_view stateName) const noexcept
{
    const auto found = std::ranges::find(states, stateName, &AnimatorState::name);
    return found != states.end() ? &*found : nullptr;
}

std::optional<std::size_t> AnimatorData::findLayer(std::string_view name) const noexcept
{
    if (name.empty() && !layers.empty())
    {
        return 0;
    }
    const auto found = std::ranges::find(layers, name, &AnimatorLayer::name);
    return found != layers.end() ? std::optional(static_cast<std::size_t>(found - layers.begin())) : std::nullopt;
}

const AnimatorParameter* AnimatorData::findParameter(std::string_view name) const noexcept
{
    const auto found = std::ranges::find(parameters, name, &AnimatorParameter::name);
    return found != parameters.end() ? &*found : nullptr;
}

std::string_view toString(AnimatorParameterType type) noexcept
{
    return nameOf(parameterTypeNames, type);
}

std::optional<AnimatorParameterType> parseAnimatorParameterType(std::string_view text) noexcept
{
    return valueOf(parameterTypeNames, text);
}

std::string_view toString(AnimatorBlend blend) noexcept
{
    return nameOf(blendNames, blend);
}

std::optional<AnimatorBlend> parseAnimatorBlend(std::string_view text) noexcept
{
    return valueOf(blendNames, text);
}

std::string_view toString(AnimatorTest test) noexcept
{
    return nameOf(testNames, test);
}

std::optional<AnimatorTest> parseAnimatorTest(std::string_view text) noexcept
{
    return valueOf(testNames, text);
}

std::string_view toString(AnimatorLayerBlend blending) noexcept
{
    return nameOf(layerBlendNames, blending);
}

std::optional<AnimatorLayerBlend> parseAnimatorLayerBlend(std::string_view text) noexcept
{
    return valueOf(layerBlendNames, text);
}

std::optional<bool> maskNames(const AnimatorLayer& layer, std::string_view bone) noexcept
{
    if (std::ranges::find(layer.maskExcluded, bone) != layer.maskExcluded.end())
    {
        return false;
    }
    if (std::ranges::find(layer.mask, bone) != layer.mask.end())
    {
        return true;
    }
    return std::nullopt;
}

bool testFits(AnimatorTest test, AnimatorParameterType type) noexcept
{
    switch (test)
    {
    case AnimatorTest::Greater:
    case AnimatorTest::Less:
        return isNumber(type);
    case AnimatorTest::Equals:
    case AnimatorTest::NotEquals:
        return type == AnimatorParameterType::Integer;
    case AnimatorTest::IsTrue:
    case AnimatorTest::IsFalse:
        return type == AnimatorParameterType::Bool;
    case AnimatorTest::Triggered:
        return type == AnimatorParameterType::Trigger;
    }
    return false;
}

namespace {

// The states and transitions of one layer; `where` names the layer in the errors of the others.
core::Result<void> validateGraph(const AnimatorData& animator, const AnimatorLayer& layer, std::string_view where)
{
    const auto numberParameter = [&](std::string_view name) {
        const AnimatorParameter* const parameter = animator.findParameter(name);
        return parameter != nullptr && isNumber(parameter->type);
    };
    std::unordered_set<std::string_view> names;
    for (const AnimatorState& state : layer.states)
    {
        if (state.name.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a state{} has no name", where);
        }
        if (!names.insert(state.name).second)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "two states{} are named {}", where, state.name);
        }
        if (state.blend > AnimatorBlend::Planar)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the state {}{} blends in an unknown way", state.name, where);
        }
        if (state.blend != AnimatorBlend::None && !numberParameter(state.parameter))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the blend of {}{} needs a float or int parameter",
                                   state.name, where);
        }
        if (state.blend == AnimatorBlend::Planar && !numberParameter(state.parameterY))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the 2D blend of {}{} needs a second parameter",
                                   state.name, where);
        }
        if (!state.speedParameter.empty() && !numberParameter(state.speedParameter))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the speed of {}{} is multiplied by an unknown parameter",
                                   state.name, where);
        }
        if (!std::isfinite(state.speed))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the speed of {}{} is not a number", state.name, where);
        }
    }
    if (!layer.states.empty() && layer.findState(layer.entry) == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the entry state {}{} does not exist", layer.entry, where);
    }

    for (const AnimatorTransition& transition : layer.transitions)
    {
        if (!transition.from.empty() && layer.findState(transition.from) == nullptr)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a transition{} leaves the unknown state {}", where,
                                   transition.from);
        }
        if (layer.findState(transition.to) == nullptr)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a transition{} goes to the unknown state {}", where,
                                   transition.to);
        }
        if (!(transition.duration >= 0.0f) || !std::isfinite(transition.duration) || !std::isfinite(transition.exitTime))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the transition from {} to {}{} lasts no valid time",
                                   transition.from.empty() ? "any state" : transition.from, transition.to, where);
        }
        for (const AnimatorCondition& condition : transition.conditions)
        {
            const AnimatorParameter* const parameter = animator.findParameter(condition.parameter);
            if (parameter == nullptr)
            {
                return core::makeError(core::ErrorCode::InvalidArgument, "a condition{} checks the unknown parameter {}", where,
                                       condition.parameter);
            }
            if (!testFits(condition.test, parameter->type))
            {
                return core::makeError(core::ErrorCode::InvalidArgument, "the {} test does not apply to the {} parameter {}",
                                       toString(condition.test), toString(parameter->type), parameter->name);
            }
        }
    }
    return {};
}

} // namespace

core::Result<void> validate(const AnimatorData& animator)
{
    std::unordered_set<std::string_view> names;
    for (const AnimatorParameter& parameter : animator.parameters)
    {
        if (parameter.name.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a parameter has no name");
        }
        if (!names.insert(parameter.name).second)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "two parameters are named {}", parameter.name);
        }
        if (parameter.type > AnimatorParameterType::Trigger || !std::isfinite(parameter.defaultValue))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the parameter {} is not valid", parameter.name);
        }
    }

    if (animator.layers.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the animator has no base layer");
    }
    names.clear();
    for (std::size_t index = 0; index < animator.layers.size(); ++index)
    {
        const AnimatorLayer& layer = animator.layers[index];
        if (layer.name.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a layer has no name");
        }
        if (!names.insert(layer.name).second)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "two layers are named {}", layer.name);
        }
        if (layer.blending > AnimatorLayerBlend::Additive)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the layer {} blends in an unknown way", layer.name);
        }
        if (!(layer.weight >= 0.0f && layer.weight <= 1.0f))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the weight of the layer {} is not from 0 to 1", layer.name);
        }
        if (!layer.weightParameter.empty())
        {
            const AnimatorParameter* const parameter = animator.findParameter(layer.weightParameter);
            if (parameter == nullptr || !isNumber(parameter->type))
            {
                return core::makeError(core::ErrorCode::InvalidArgument, "the weight of the layer {} is multiplied by an unknown parameter",
                                       layer.name);
            }
        }
        const std::string where = index == 0 ? std::string{} : std::format(" of the layer {}", layer.name);
        if (core::Result<void> graph = validateGraph(animator, layer, where); !graph)
        {
            return graph;
        }
    }
    return {};
}

} // namespace devex::asset
