#include <devex/asset/AnimatorData.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

const AnimatorState* AnimatorData::findState(std::string_view name) const noexcept
{
    const auto found = std::ranges::find(states, name, &AnimatorState::name);
    return found != states.end() ? &*found : nullptr;
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
    const auto numberParameter = [&](std::string_view name) {
        const AnimatorParameter* const parameter = animator.findParameter(name);
        return parameter != nullptr && isNumber(parameter->type);
    };

    names.clear();
    for (const AnimatorState& state : animator.states)
    {
        if (state.name.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a state has no name");
        }
        if (!names.insert(state.name).second)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "two states are named {}", state.name);
        }
        if (state.blend > AnimatorBlend::Planar)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the state {} blends in an unknown way", state.name);
        }
        if (state.blend != AnimatorBlend::None && !numberParameter(state.parameter))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the blend of {} needs a float or int parameter",
                                   state.name);
        }
        if (state.blend == AnimatorBlend::Planar && !numberParameter(state.parameterY))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the 2D blend of {} needs a second parameter",
                                   state.name);
        }
        if (!state.speedParameter.empty() && !numberParameter(state.speedParameter))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the speed of {} is multiplied by an unknown parameter",
                                   state.name);
        }
        if (!std::isfinite(state.speed))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the speed of {} is not a number", state.name);
        }
    }
    if (!animator.states.empty() && animator.findState(animator.entry) == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the entry state {} does not exist", animator.entry);
    }

    for (const AnimatorTransition& transition : animator.transitions)
    {
        if (!transition.from.empty() && animator.findState(transition.from) == nullptr)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a transition leaves the unknown state {}",
                                   transition.from);
        }
        if (animator.findState(transition.to) == nullptr)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a transition goes to the unknown state {}",
                                   transition.to);
        }
        if (!(transition.duration >= 0.0f) || !std::isfinite(transition.duration) || !std::isfinite(transition.exitTime))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the transition from {} to {} lasts no valid time",
                                   transition.from.empty() ? "any state" : transition.from, transition.to);
        }
        for (const AnimatorCondition& condition : transition.conditions)
        {
            const AnimatorParameter* const parameter = animator.findParameter(condition.parameter);
            if (parameter == nullptr)
            {
                return core::makeError(core::ErrorCode::InvalidArgument, "a condition checks the unknown parameter {}",
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

} // namespace devex::asset
