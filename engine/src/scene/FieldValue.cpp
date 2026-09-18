#include <devex/asset/AssetId.hpp>
#include <devex/core/Assert.hpp>
#include <devex/scene/EntityRef.hpp>
#include <devex/scene/FieldValue.hpp>

#include <array>
#include <charconv>
#include <format>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace devex::scene {
namespace {

using serialization::TextCall;
using serialization::TextValue;

// Returns the double closest to the shortest decimal form of the float, so that files show 0.1
// rather than 0.10000000149011612 while still reading back to the same float.
[[nodiscard]] double shortestDouble(float value) noexcept
{
    std::array<char, 32> text{};
    const auto [end, status] = std::to_chars(text.data(), text.data() + text.size(), value);
    double result = value;
    std::from_chars(text.data(), end, result);
    return result;
}

[[nodiscard]] TextValue numbers(std::string name, std::span<const float> components)
{
    std::vector<TextValue> arguments;
    for (const float component : components)
    {
        arguments.emplace_back(shortestDouble(component));
    }
    return serialization::makeCall(std::move(name), std::move(arguments));
}

// Reads a call such as vec3(x, y, z) whose arguments are all numbers.
[[nodiscard]] core::Result<void> readNumbers(const TextValue& value, std::string_view name,
                                             std::span<float> components)
{
    const TextCall* const call = serialization::asCall(value, name);
    if (call == nullptr || call->arguments.size() != components.size())
    {
        return core::makeError(core::ErrorCode::Parse, "expected {}() with {} numbers", name,
                               components.size());
    }
    for (std::size_t index = 0; index < components.size(); ++index)
    {
        const std::optional<double> number = serialization::asNumber(call->arguments[index]);
        if (!number)
        {
            return core::makeError(core::ErrorCode::Parse, "{}() arguments must be numbers", name);
        }
        components[index] = static_cast<float>(*number);
    }
    return {};
}

[[nodiscard]] core::Result<std::int64_t> readInteger(const TextValue& value, std::int64_t minimum,
                                                     std::int64_t maximum)
{
    const std::optional<std::int64_t> integer = serialization::asInteger(value);
    if (!integer || *integer < minimum || *integer > maximum)
    {
        return core::makeError(core::ErrorCode::Parse, "expected an integer in [{}, {}]", minimum,
                               maximum);
    }
    return *integer;
}

[[nodiscard]] core::Result<core::Uuid> readUuid(const TextValue& value)
{
    const std::string* const text = serialization::asString(value);
    const std::optional<core::Uuid> uuid =
        text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    if (!uuid)
    {
        return core::makeError(core::ErrorCode::Parse, "expected a UUID string");
    }
    return *uuid;
}

} // namespace

TextValue writeFieldValue(reflection::ValueKind kind, const void* address)
{
    using reflection::ValueKind;
    switch (kind)
    {
    case ValueKind::Bool:
        return TextValue(*static_cast<const bool*>(address));
    case ValueKind::Int32:
        return TextValue(std::int64_t{*static_cast<const std::int32_t*>(address)});
    case ValueKind::UInt32:
        return TextValue(std::int64_t{*static_cast<const std::uint32_t*>(address)});
    case ValueKind::Float:
        return TextValue(shortestDouble(*static_cast<const float*>(address)));
    case ValueKind::String:
        return TextValue(*static_cast<const std::string*>(address));
    case ValueKind::Vec2: {
        const auto& vector = *static_cast<const math::Vec2*>(address);
        return numbers("vec2", std::array{vector.x, vector.y});
    }
    case ValueKind::Vec3: {
        const auto& vector = *static_cast<const math::Vec3*>(address);
        return numbers("vec3", std::array{vector.x, vector.y, vector.z});
    }
    case ValueKind::Vec4: {
        const auto& vector = *static_cast<const math::Vec4*>(address);
        return numbers("vec4", std::array{vector.x, vector.y, vector.z, vector.w});
    }
    case ValueKind::Quat: {
        const auto& quaternion = *static_cast<const math::Quat*>(address);
        return numbers("quat", std::array{quaternion.x, quaternion.y, quaternion.z, quaternion.w});
    }
    case ValueKind::Uuid:
        return TextValue(static_cast<const core::Uuid*>(address)->toString());
    case ValueKind::AssetId:
        return serialization::makeCall(
            "asset", {TextValue(static_cast<const asset::AssetId*>(address)->uuid.toString())});
    case ValueKind::Enum:
        DEVEX_ASSERT_MSG(false, "enumerations are written through their field");
        return TextValue(std::int64_t{0});
    case ValueKind::Entity: {
        // entity() for an empty reference.
        const EntityRef& reference = *static_cast<const EntityRef*>(address);
        return reference.isNil() ? serialization::makeCall("entity", {})
                                 : serialization::makeCall("entity", {TextValue(reference.uuid.toString())});
    }
    }
    DEVEX_UNREACHABLE();
}

core::Result<void> readFieldValue(reflection::ValueKind kind, const TextValue& value,
                                  void* address)
{
    using reflection::ValueKind;
    switch (kind)
    {
    case ValueKind::Bool: {
        const std::optional<bool> boolean = serialization::asBool(value);
        if (!boolean)
        {
            return core::makeError(core::ErrorCode::Parse, "expected true or false");
        }
        *static_cast<bool*>(address) = *boolean;
        return {};
    }
    case ValueKind::Int32: {
        const core::Result<std::int64_t> integer =
            readInteger(value, std::numeric_limits<std::int32_t>::min(),
                        std::numeric_limits<std::int32_t>::max());
        if (!integer)
        {
            return std::unexpected(integer.error());
        }
        *static_cast<std::int32_t*>(address) = static_cast<std::int32_t>(*integer);
        return {};
    }
    case ValueKind::UInt32: {
        const core::Result<std::int64_t> integer =
            readInteger(value, 0, std::numeric_limits<std::uint32_t>::max());
        if (!integer)
        {
            return std::unexpected(integer.error());
        }
        *static_cast<std::uint32_t*>(address) = static_cast<std::uint32_t>(*integer);
        return {};
    }
    case ValueKind::Float: {
        const std::optional<double> number = serialization::asNumber(value);
        if (!number)
        {
            return core::makeError(core::ErrorCode::Parse, "expected a number");
        }
        *static_cast<float*>(address) = static_cast<float>(*number);
        return {};
    }
    case ValueKind::String: {
        const std::string* const text = serialization::asString(value);
        if (text == nullptr)
        {
            return core::makeError(core::ErrorCode::Parse, "expected a string");
        }
        *static_cast<std::string*>(address) = *text;
        return {};
    }
    case ValueKind::Vec2: {
        std::array<float, 2> xy{};
        if (core::Result<void> read = readNumbers(value, "vec2", xy); !read)
        {
            return read;
        }
        *static_cast<math::Vec2*>(address) = math::Vec2(xy[0], xy[1]);
        return {};
    }
    case ValueKind::Vec3: {
        std::array<float, 3> xyz{};
        if (core::Result<void> read = readNumbers(value, "vec3", xyz); !read)
        {
            return read;
        }
        *static_cast<math::Vec3*>(address) = math::Vec3(xyz[0], xyz[1], xyz[2]);
        return {};
    }
    case ValueKind::Vec4: {
        std::array<float, 4> xyzw{};
        if (core::Result<void> read = readNumbers(value, "vec4", xyzw); !read)
        {
            return read;
        }
        *static_cast<math::Vec4*>(address) = math::Vec4(xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
        return {};
    }
    case ValueKind::Quat: {
        std::array<float, 4> xyzw{};
        if (core::Result<void> read = readNumbers(value, "quat", xyzw); !read)
        {
            return read;
        }
        *static_cast<math::Quat*>(address) = math::Quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]);
        return {};
    }
    case ValueKind::Uuid: {
        const core::Result<core::Uuid> uuid = readUuid(value);
        if (!uuid)
        {
            return std::unexpected(uuid.error());
        }
        *static_cast<core::Uuid*>(address) = *uuid;
        return {};
    }
    case ValueKind::AssetId: {
        const TextCall* const call = serialization::asCall(value, "asset");
        const core::Result<core::Uuid> uuid =
            call != nullptr && call->arguments.size() == 1
                ? readUuid(call->arguments.front())
                : core::makeError(core::ErrorCode::Parse, "not an asset reference");
        if (!uuid)
        {
            return core::makeError(core::ErrorCode::Parse, "expected asset(\"<uuid>\")");
        }
        *static_cast<asset::AssetId*>(address) = asset::AssetId{*uuid};
        return {};
    }
    case ValueKind::Enum:
        DEVEX_ASSERT_MSG(false, "enumerations are read through their field");
        return core::makeError(core::ErrorCode::InvalidArgument, "the enumeration names are unknown");
    case ValueKind::Entity: {
        const TextCall* const call = serialization::asCall(value, "entity");
        if (call == nullptr || call->arguments.size() > 1)
        {
            return core::makeError(core::ErrorCode::Parse, "expected entity(\"<uuid>\") or entity()");
        }
        if (call->arguments.empty())
        {
            *static_cast<EntityRef*>(address) = EntityRef{};
            return {};
        }
        const core::Result<core::Uuid> uuid = readUuid(call->arguments.front());
        if (!uuid)
        {
            return core::makeError(core::ErrorCode::Parse, "expected entity(\"<uuid>\") or entity()");
        }
        *static_cast<EntityRef*>(address) = EntityRef{*uuid};
        return {};
    }
    }
    DEVEX_UNREACHABLE();
}

namespace {

// A value of the field, or an element of it when the field is a list.
[[nodiscard]] TextValue writeElement(const reflection::FieldInfo& field, const void* address)
{
    if (field.kind != reflection::ValueKind::Enum)
    {
        return writeFieldValue(field.kind, address);
    }
    const std::uint32_t index = reflection::readEnumIndex(field, address);
    return index < field.enumNames.size() ? TextValue(std::string(field.enumNames[index]))
                                          : TextValue(std::int64_t{index});
}

[[nodiscard]] core::Result<void> readElement(const reflection::FieldInfo& field, const TextValue& value,
                                             void* address);

// list(a, b, c): the list takes the number of values given, and keeps its previous value when one
// of them is wrong.
[[nodiscard]] core::Result<void> readList(const reflection::FieldInfo& field, const TextValue& value, void* list)
{
    const TextCall* const call = serialization::asCall(value, "list");
    if (call == nullptr)
    {
        return core::makeError(core::ErrorCode::Parse, "expected list(...)");
    }
    const TextValue previous = writeFieldValue(field, list);
    field.list->resize(list, call->arguments.size());
    for (std::size_t index = 0; index < call->arguments.size(); ++index)
    {
        if (core::Result<void> read = readElement(field, call->arguments[index], field.list->element(list, index));
            !read)
        {
            static_cast<void>(readList(field, previous, list));
            return core::makeError(read.error().code, "element {}: {}", index, read.error().message);
        }
    }
    return {};
}

} // namespace

TextValue writeFieldValue(const reflection::FieldInfo& field, const void* address)
{
    if (field.list == nullptr)
    {
        return writeElement(field, address);
    }
    void* const list = const_cast<void*>(address);
    const std::size_t count = field.list->size(list);
    std::vector<TextValue> elements;
    elements.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        elements.push_back(writeElement(field, field.list->element(list, index)));
    }
    return serialization::makeCall("list", std::move(elements));
}

core::Result<void> readFieldValue(const reflection::FieldInfo& field, const TextValue& value,
                                  void* address)
{
    return field.list != nullptr ? readList(field, value, address) : readElement(field, value, address);
}

namespace {

core::Result<void> readElement(const reflection::FieldInfo& field, const TextValue& value, void* address)
{
    if (field.kind != reflection::ValueKind::Enum)
    {
        return readFieldValue(field.kind, value, address);
    }
    if (const std::string* const name = serialization::asString(value))
    {
        for (std::size_t index = 0; index < field.enumNames.size(); ++index)
        {
            if (field.enumNames[index] == *name)
            {
                reflection::writeEnumIndex(field, address, static_cast<std::uint32_t>(index));
                return {};
            }
        }
    }
    std::string expected;
    for (const std::string_view name : field.enumNames)
    {
        expected += expected.empty() ? "" : ", ";
        expected += std::format("\"{}\"", name);
    }
    return core::makeError(core::ErrorCode::Parse, "expected one of {}", expected);
}

} // namespace

} // namespace devex::scene
