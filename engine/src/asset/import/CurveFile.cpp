#include <devex/asset/import/CurveFile.hpp>

#include <devex/serialization/Text.hpp>

#include <format>
#include <optional>

namespace devex::asset {
namespace {

constexpr std::int64_t curveFormat = 1;

[[nodiscard]] std::optional<float> number(const serialization::TextSection& section, std::string_view key)
{
    const serialization::TextValue* const value = section.findAttribute(key);
    const std::optional<double> read = value != nullptr ? serialization::asNumber(*value) : std::nullopt;
    return read ? std::optional(static_cast<float>(*read)) : std::nullopt;
}

} // namespace

core::Result<CurveData> parseCurveFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "curve")
    {
        return core::makeError(core::ErrorCode::Parse, "a curve file starts with [curve]");
    }
    const serialization::TextValue* const format = document->sections.front().findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format).value_or(curveFormat + 1) > curveFormat)
    {
        return core::makeError(core::ErrorCode::Unsupported, "the curve needs a newer version of Devex");
    }
    CurveData curve;
    for (const serialization::TextSection& section : document->sections)
    {
        if (section.type != "key")
        {
            continue;
        }
        const std::optional<float> time = number(section, "time");
        const std::optional<float> value = number(section, "value");
        if (!time || !value)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: a key needs a time and a value", section.line);
        }
        curve.keys.push_back({.time = *time,
                              .value = *value,
                              .inTangent = number(section, "in").value_or(0.0f),
                              .outTangent = number(section, "out").value_or(0.0f)});
    }
    if (core::Result<void> valid = validate(curve); !valid)
    {
        return std::unexpected(valid.error());
    }
    return curve;
}

std::string writeCurveFile(const CurveData& curve)
{
    serialization::TextDocument document;
    serialization::TextSection& header = document.sections.emplace_back();
    header.type = "curve";
    header.attributes.push_back({"format", serialization::TextValue(curveFormat)});
    for (const CurveKey& key : curve.keys)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "key";
        section.attributes.push_back({"time", serialization::shortestNumber(key.time)});
        section.attributes.push_back({"value", serialization::shortestNumber(key.value)});
        section.attributes.push_back({"in", serialization::shortestNumber(key.inTangent)});
        section.attributes.push_back({"out", serialization::shortestNumber(key.outTangent)});
    }
    return serialization::writeText(document);
}

} // namespace devex::asset
