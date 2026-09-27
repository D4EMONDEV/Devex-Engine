#include <devex/asset/import/AnimatorFile.hpp>

#include <devex/serialization/Text.hpp>

#include <format>
#include <optional>

namespace devex::asset {
namespace {

using serialization::TextSection;
using serialization::TextValue;

constexpr std::int64_t animatorFormat = 1;

// The shortest text that reads back as the same float, rather than the digits of its double.
[[nodiscard]] TextValue number(float value)
{
    return TextValue(std::stod(std::format("{}", value)));
}

[[nodiscard]] TextValue vector(math::Vec2 value)
{
    return serialization::makeCall("vec2", {number(value.x), number(value.y)});
}

[[nodiscard]] TextValue assetValue(AssetId id)
{
    return serialization::makeCall("asset", {TextValue(id.uuid.toString())});
}

[[nodiscard]] std::optional<AssetId> readAsset(const TextValue& value)
{
    const serialization::TextCall* const call = serialization::asCall(value, "asset");
    if (call == nullptr || call->arguments.size() != 1)
    {
        return std::nullopt;
    }
    const std::string* const text = serialization::asString(call->arguments.front());
    const std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    return uuid ? std::optional(AssetId{*uuid}) : std::nullopt;
}

[[nodiscard]] std::optional<math::Vec2> readVector(const TextValue& value)
{
    const serialization::TextCall* const call = serialization::asCall(value, "vec2");
    if (call == nullptr || call->arguments.size() != 2)
    {
        return std::nullopt;
    }
    const std::optional<double> x = serialization::asNumber(call->arguments[0]);
    const std::optional<double> y = serialization::asNumber(call->arguments[1]);
    return x && y ? std::optional(math::Vec2{static_cast<float>(*x), static_cast<float>(*y)}) : std::nullopt;
}

[[nodiscard]] const serialization::TextCall* readList(const TextSection& section, std::string_view key)
{
    const TextValue* const value = section.findProperty(key);
    return value != nullptr ? serialization::asCall(*value, "list") : nullptr;
}

[[nodiscard]] std::string stringOr(const TextSection& section, std::string_view key, bool attribute)
{
    const TextValue* const value = attribute ? section.findAttribute(key) : section.findProperty(key);
    const std::string* const text = value != nullptr ? serialization::asString(*value) : nullptr;
    return text != nullptr ? *text : std::string{};
}

[[nodiscard]] std::optional<float> numberOf(const TextSection& section, std::string_view key, bool attribute)
{
    const TextValue* const value = attribute ? section.findAttribute(key) : section.findProperty(key);
    const std::optional<double> read = value != nullptr ? serialization::asNumber(*value) : std::nullopt;
    return read ? std::optional(static_cast<float>(*read)) : std::nullopt;
}

[[nodiscard]] core::Error parseError(const TextSection& section, std::string_view problem)
{
    return core::Error{core::ErrorCode::Parse, std::format("line {}: {}", section.line, problem)};
}

[[nodiscard]] core::Result<AnimatorState> readState(const TextSection& section)
{
    AnimatorState state;
    state.name = stringOr(section, "name", true);
    const std::string blend = stringOr(section, "blend", true);
    if (!blend.empty())
    {
        const std::optional<AnimatorBlend> parsed = parseAnimatorBlend(blend);
        if (!parsed)
        {
            return std::unexpected(parseError(section, "blend is \"none\", \"1d\" or \"2d\""));
        }
        state.blend = *parsed;
    }
    state.parameter = stringOr(section, "parameter", true);
    state.parameterY = stringOr(section, "parameter_y", true);
    if (const TextValue* const position = section.findProperty("position"))
    {
        const std::optional<math::Vec2> read = readVector(*position);
        if (!read)
        {
            return std::unexpected(parseError(section, "the position is a vec2(x, y)"));
        }
        state.graphPosition = *read;
    }
    if (const serialization::TextCall* const clips = readList(section, "clips"))
    {
        for (const TextValue& clip : clips->arguments)
        {
            const std::optional<AssetId> id = readAsset(clip);
            if (!id)
            {
                return std::unexpected(parseError(section, "a clip is an asset(\"uuid\")"));
            }
            state.motions.push_back({.clip = *id});
        }
    }
    else if (section.findProperty("clips") != nullptr)
    {
        return std::unexpected(parseError(section, "the clips are a list(...)"));
    }
    if (const serialization::TextCall* const thresholds = readList(section, "thresholds"))
    {
        if (thresholds->arguments.size() != state.motions.size())
        {
            return std::unexpected(parseError(section, "there are as many thresholds as clips"));
        }
        for (std::size_t index = 0; index < state.motions.size(); ++index)
        {
            const std::optional<double> threshold = serialization::asNumber(thresholds->arguments[index]);
            if (!threshold)
            {
                return std::unexpected(parseError(section, "a threshold is a number"));
            }
            state.motions[index].threshold = static_cast<float>(*threshold);
        }
    }
    if (const serialization::TextCall* const positions = readList(section, "positions"))
    {
        if (positions->arguments.size() != state.motions.size())
        {
            return std::unexpected(parseError(section, "there are as many positions as clips"));
        }
        for (std::size_t index = 0; index < state.motions.size(); ++index)
        {
            const std::optional<math::Vec2> position = readVector(positions->arguments[index]);
            if (!position)
            {
                return std::unexpected(parseError(section, "a position is a vec2(x, y)"));
            }
            state.motions[index].position = *position;
        }
    }
    state.spriteAnimation = stringOr(section, "sprite", false);
    state.speed = numberOf(section, "speed", false).value_or(1.0f);
    state.speedParameter = stringOr(section, "speed_parameter", false);
    if (const TextValue* const loop = section.findProperty("loop"))
    {
        const std::optional<bool> value = serialization::asBool(*loop);
        if (!value)
        {
            return std::unexpected(parseError(section, "loop is true or false"));
        }
        state.loop = *value;
    }
    return state;
}

[[nodiscard]] core::Result<AnimatorCondition> readCondition(const TextSection& section, const TextValue& value)
{
    const auto* const call = std::get_if<serialization::TextCall>(&value);
    const std::string* const parameter =
        call != nullptr && !call->arguments.empty() ? serialization::asString(call->arguments.front()) : nullptr;
    if (parameter == nullptr)
    {
        return std::unexpected(parseError(section, "a condition is greater(\"parameter\", value), is(...), trigger(...)..."));
    }
    AnimatorCondition condition{.parameter = *parameter};
    if (call->name == "trigger" && call->arguments.size() == 1)
    {
        condition.test = AnimatorTest::Triggered;
        return condition;
    }
    if (call->name == "is" && call->arguments.size() == 2)
    {
        const std::optional<bool> expected = serialization::asBool(call->arguments[1]);
        if (!expected)
        {
            return std::unexpected(parseError(section, "is() compares a bool parameter to true or false"));
        }
        condition.test = *expected ? AnimatorTest::IsTrue : AnimatorTest::IsFalse;
        return condition;
    }
    const std::optional<AnimatorTest> test = parseAnimatorTest(call->name);
    const std::optional<double> compared = call->arguments.size() == 2 ? serialization::asNumber(call->arguments[1]) : std::nullopt;
    if (!test || !compared || *test > AnimatorTest::NotEquals)
    {
        return std::unexpected(parseError(section, std::format("unknown condition {}", call->name)));
    }
    condition.test = *test;
    condition.value = static_cast<float>(*compared);
    return condition;
}

[[nodiscard]] TextValue conditionValue(const AnimatorCondition& condition)
{
    const TextValue parameter(condition.parameter);
    switch (condition.test)
    {
    case AnimatorTest::Triggered:
        return serialization::makeCall("trigger", {parameter});
    case AnimatorTest::IsTrue:
        return serialization::makeCall("is", {parameter, TextValue(true)});
    case AnimatorTest::IsFalse:
        return serialization::makeCall("is", {parameter, TextValue(false)});
    case AnimatorTest::Greater:
    case AnimatorTest::Less:
    case AnimatorTest::Equals:
    case AnimatorTest::NotEquals:
        break;
    }
    return serialization::makeCall(std::string(toString(condition.test)), {parameter, number(condition.value)});
}

} // namespace

core::Result<AnimatorData> parseAnimatorFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "animator")
    {
        return core::makeError(core::ErrorCode::Parse, "an animator file starts with [animator]");
    }
    const TextSection& header = document->sections.front();
    const TextValue* const format = header.findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format).value_or(animatorFormat + 1) > animatorFormat)
    {
        return core::makeError(core::ErrorCode::Unsupported, "the animator needs a newer version of Devex");
    }
    AnimatorData animator;
    animator.entry = stringOr(header, "entry", true);
    if (const TextValue* const position = header.findProperty("entry_position"))
    {
        animator.entryPosition = readVector(*position).value_or(animator.entryPosition);
    }
    if (const TextValue* const position = header.findProperty("any_state_position"))
    {
        animator.anyStatePosition = readVector(*position).value_or(animator.anyStatePosition);
    }

    for (const TextSection& section : document->sections)
    {
        if (section.type == "parameter")
        {
            AnimatorParameter parameter{.name = stringOr(section, "name", true)};
            const std::string type = stringOr(section, "type", true);
            const std::optional<AnimatorParameterType> parsed = parseAnimatorParameterType(type.empty() ? "float" : type);
            if (!parsed)
            {
                return std::unexpected(parseError(section, "type is \"float\", \"int\", \"bool\" or \"trigger\""));
            }
            parameter.type = *parsed;
            if (const TextValue* const value = section.findAttribute("default"))
            {
                if (const std::optional<bool> flag = serialization::asBool(*value))
                {
                    parameter.defaultValue = *flag ? 1.0f : 0.0f;
                }
                else
                {
                    parameter.defaultValue = static_cast<float>(serialization::asNumber(*value).value_or(0.0));
                }
            }
            animator.parameters.push_back(std::move(parameter));
        }
        else if (section.type == "state")
        {
            core::Result<AnimatorState> state = readState(section);
            if (!state)
            {
                return std::unexpected(state.error());
            }
            animator.states.push_back(std::move(*state));
        }
        else if (section.type == "transition")
        {
            AnimatorTransition transition{
                .from = stringOr(section, "from", true),
                .to = stringOr(section, "to", true),
                .duration = numberOf(section, "duration", true).value_or(0.2f),
                .exitTime = numberOf(section, "exit_time", false).value_or(-1.0f),
            };
            if (const serialization::TextCall* const conditions = readList(section, "conditions"))
            {
                for (const TextValue& value : conditions->arguments)
                {
                    core::Result<AnimatorCondition> condition = readCondition(section, value);
                    if (!condition)
                    {
                        return std::unexpected(condition.error());
                    }
                    transition.conditions.push_back(std::move(*condition));
                }
            }
            animator.transitions.push_back(std::move(transition));
        }
    }
    if (core::Result<void> valid = validate(animator); !valid)
    {
        return std::unexpected(valid.error());
    }
    return animator;
}

std::string writeAnimatorFile(const AnimatorData& animator)
{
    serialization::TextDocument document;
    TextSection& header = document.sections.emplace_back();
    header.type = "animator";
    header.attributes.push_back({"format", TextValue(animatorFormat)});
    header.attributes.push_back({"entry", TextValue(animator.entry)});
    header.properties.push_back({"entry_position", vector(animator.entryPosition)});
    header.properties.push_back({"any_state_position", vector(animator.anyStatePosition)});

    for (const AnimatorParameter& parameter : animator.parameters)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "parameter";
        section.attributes.push_back({"name", TextValue(parameter.name)});
        section.attributes.push_back({"type", TextValue(std::string(toString(parameter.type)))});
        switch (parameter.type)
        {
        case AnimatorParameterType::Float:
            section.attributes.push_back({"default", number(parameter.defaultValue)});
            break;
        case AnimatorParameterType::Integer:
            section.attributes.push_back({"default", TextValue(static_cast<std::int64_t>(parameter.defaultValue))});
            break;
        case AnimatorParameterType::Bool:
            section.attributes.push_back({"default", TextValue(parameter.defaultValue != 0.0f)});
            break;
        case AnimatorParameterType::Trigger:
            break;
        }
    }

    for (const AnimatorState& state : animator.states)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "state";
        section.attributes.push_back({"name", TextValue(state.name)});
        if (state.blend != AnimatorBlend::None)
        {
            section.attributes.push_back({"blend", TextValue(std::string(toString(state.blend)))});
            section.attributes.push_back({"parameter", TextValue(state.parameter)});
        }
        if (state.blend == AnimatorBlend::Planar)
        {
            section.attributes.push_back({"parameter_y", TextValue(state.parameterY)});
        }
        section.properties.push_back({"position", vector(state.graphPosition)});
        if (!state.motions.empty())
        {
            std::vector<TextValue> clips;
            std::vector<TextValue> thresholds;
            std::vector<TextValue> positions;
            for (const AnimatorMotion& motion : state.motions)
            {
                clips.push_back(assetValue(motion.clip));
                thresholds.push_back(number(motion.threshold));
                positions.push_back(vector(motion.position));
            }
            section.properties.push_back({"clips", serialization::makeCall("list", std::move(clips))});
            if (state.blend == AnimatorBlend::Linear)
            {
                section.properties.push_back({"thresholds", serialization::makeCall("list", std::move(thresholds))});
            }
            if (state.blend == AnimatorBlend::Planar)
            {
                section.properties.push_back({"positions", serialization::makeCall("list", std::move(positions))});
            }
        }
        if (!state.spriteAnimation.empty())
        {
            section.properties.push_back({"sprite", TextValue(state.spriteAnimation)});
        }
        if (state.speed != 1.0f)
        {
            section.properties.push_back({"speed", number(state.speed)});
        }
        if (!state.speedParameter.empty())
        {
            section.properties.push_back({"speed_parameter", TextValue(state.speedParameter)});
        }
        if (!state.loop)
        {
            section.properties.push_back({"loop", TextValue(false)});
        }
    }

    for (const AnimatorTransition& transition : animator.transitions)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "transition";
        if (!transition.from.empty())
        {
            section.attributes.push_back({"from", TextValue(transition.from)});
        }
        section.attributes.push_back({"to", TextValue(transition.to)});
        section.attributes.push_back({"duration", number(transition.duration)});
        if (transition.exitTime >= 0.0f)
        {
            section.properties.push_back({"exit_time", number(transition.exitTime)});
        }
        if (!transition.conditions.empty())
        {
            std::vector<TextValue> conditions;
            for (const AnimatorCondition& condition : transition.conditions)
            {
                conditions.push_back(conditionValue(condition));
            }
            section.properties.push_back({"conditions", serialization::makeCall("list", std::move(conditions))});
        }
    }
    return serialization::writeText(document);
}

} // namespace devex::asset
