#include <devex/asset/AnimationEvents.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace devex::asset {
namespace {

using serialization::TextCall;
using serialization::TextValue;

// The arguments of each event(...) of a list(...), a number then a name.
template <typename Make>
[[nodiscard]] core::Result<void> readEvents(const TextValue& value, const Make& make)
{
    const TextCall* const list = serialization::asCall(value, "list");
    if (list == nullptr)
    {
        return core::makeError(core::ErrorCode::Parse, "events are a list(event(time, \"name\"), ...)");
    }
    for (const TextValue& item : list->arguments)
    {
        const TextCall* const event = serialization::asCall(item, "event");
        const std::optional<double> at =
            event != nullptr && event->arguments.size() == 2 ? serialization::asNumber(event->arguments[0]) : std::nullopt;
        const std::string* const name = at ? serialization::asString(event->arguments[1]) : nullptr;
        if (name == nullptr || name->empty() || !std::isfinite(*at) || *at < 0.0)
        {
            return core::makeError(core::ErrorCode::Parse, "an event reads event(time, \"name\"), with a name and a time from 0");
        }
        make(*at, *name);
    }
    return {};
}

// The shortest text of the float, not of the double it widens to.
[[nodiscard]] TextValue shortest(float value)
{
    return TextValue(std::stod(std::format("{}", value)));
}

} // namespace

core::Result<std::vector<AnimationEvent>> readAnimationEvents(const TextValue& value)
{
    std::vector<AnimationEvent> events;
    if (core::Result<void> read = readEvents(value, [&](double time, const std::string& name) {
            events.push_back({.time = static_cast<float>(time), .name = name});
        });
        !read)
    {
        return std::unexpected(read.error());
    }
    std::ranges::stable_sort(events, {}, &AnimationEvent::time);
    return events;
}

TextValue writeAnimationEvents(std::span<const AnimationEvent> events)
{
    std::vector<TextValue> list;
    for (const AnimationEvent& event : events)
    {
        list.push_back(serialization::makeCall("event", {shortest(event.time), TextValue(event.name)}));
    }
    return serialization::makeCall("list", std::move(list));
}

core::Result<std::vector<SpriteAnimationEvent>> readSpriteAnimationEvents(const TextValue& value)
{
    std::vector<SpriteAnimationEvent> events;
    bool whole = true;
    if (core::Result<void> read = readEvents(value, [&](double frame, const std::string& name) {
            whole = whole && frame == std::floor(frame) && frame < 4294967296.0;
            events.push_back({.frame = static_cast<std::uint32_t>(frame), .name = name});
        });
        !read)
    {
        return std::unexpected(read.error());
    }
    if (!whole)
    {
        return core::makeError(core::ErrorCode::Parse, "the events of a sprite animation are on frames: event(2, \"name\")");
    }
    std::ranges::stable_sort(events, {}, &SpriteAnimationEvent::frame);
    return events;
}

TextValue writeSpriteAnimationEvents(std::span<const SpriteAnimationEvent> events)
{
    std::vector<TextValue> list;
    for (const SpriteAnimationEvent& event : events)
    {
        list.push_back(serialization::makeCall("event", {TextValue(static_cast<std::int64_t>(event.frame)), TextValue(event.name)}));
    }
    return serialization::makeCall("list", std::move(list));
}

} // namespace devex::asset
