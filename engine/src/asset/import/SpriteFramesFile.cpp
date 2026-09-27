#include <devex/asset/import/SpriteFramesFile.hpp>

#include <devex/serialization/Text.hpp>

#include <format>
#include <optional>

namespace devex::asset {
namespace {

constexpr std::int64_t framesFormat = 1;

// The shortest text of the float, not of the double it widens to.
[[nodiscard]] serialization::TextValue shortest(float value)
{
    return serialization::TextValue(std::stod(std::format("{}", value)));
}

[[nodiscard]] std::optional<AssetId> readAsset(const serialization::TextValue& value)
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

} // namespace

core::Result<SpriteFramesData> parseSpriteFramesFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "frames")
    {
        return core::makeError(core::ErrorCode::Parse, "a sprite frames file starts with [frames]");
    }
    const serialization::TextValue* const format = document->sections.front().findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format).value_or(framesFormat + 1) > framesFormat)
    {
        return core::makeError(core::ErrorCode::Unsupported, "the sprite frames need a newer version of Devex");
    }
    SpriteFramesData frames;
    for (const serialization::TextSection& section : document->sections)
    {
        if (section.type != "animation")
        {
            continue;
        }
        SpriteAnimationData animation;
        const serialization::TextValue* const name = section.findAttribute("name");
        if (name == nullptr || serialization::asString(*name) == nullptr)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: an animation needs a name", section.line);
        }
        animation.name = *serialization::asString(*name);
        if (const serialization::TextValue* const fps = section.findAttribute("fps"))
        {
            animation.fps = static_cast<float>(serialization::asNumber(*fps).value_or(0.0));
        }
        if (const serialization::TextValue* const loop = section.findAttribute("loop"))
        {
            animation.loop = serialization::asBool(*loop).value_or(true);
        }
        if (const serialization::TextValue* const list = section.findProperty("frames"))
        {
            const serialization::TextCall* const call = serialization::asCall(*list, "list");
            if (call == nullptr)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: the frames are a list(...)", section.line);
            }
            for (const serialization::TextValue& frame : call->arguments)
            {
                const std::optional<AssetId> sprite = readAsset(frame);
                if (!sprite)
                {
                    return core::makeError(core::ErrorCode::Parse, "line {}: a frame is an asset(\"uuid\")", section.line);
                }
                animation.frames.push_back(*sprite);
            }
        }
        frames.animations.push_back(std::move(animation));
    }
    if (core::Result<void> valid = validate(frames); !valid)
    {
        return std::unexpected(valid.error());
    }
    return frames;
}

std::string writeSpriteFramesFile(const SpriteFramesData& frames)
{
    serialization::TextDocument document;
    serialization::TextSection& header = document.sections.emplace_back();
    header.type = "frames";
    header.attributes.push_back({"format", serialization::TextValue(framesFormat)});
    for (const SpriteAnimationData& animation : frames.animations)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "animation";
        section.attributes.push_back({"name", serialization::TextValue(animation.name)});
        section.attributes.push_back({"fps", shortest(animation.fps)});
        section.attributes.push_back({"loop", serialization::TextValue(animation.loop)});
        std::vector<serialization::TextValue> list;
        list.reserve(animation.frames.size());
        for (const AssetId frame : animation.frames)
        {
            list.push_back(serialization::makeCall("asset", {serialization::TextValue(frame.uuid.toString())}));
        }
        section.properties.push_back({"frames", serialization::makeCall("list", std::move(list))});
    }
    return serialization::writeText(document);
}

} // namespace devex::asset
