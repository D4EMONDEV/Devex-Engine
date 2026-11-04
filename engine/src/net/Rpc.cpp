#include <devex/net/Rpc.hpp>

#include <devex/net/Bytes.hpp>

#include <bit>
#include <initializer_list>

namespace devex::net {

namespace {

// A call on the network, numbers in little endian: the 16 bytes of the target, the length of the name
// on 2 bytes, the name, the count of arguments on 1 byte, then each argument: its type on 1 byte, then
// its value. Text and bytes start with their length on 4 bytes. The format C# writes too.
constexpr std::size_t targetSize = 16;

void writeFloats(ByteWriter& writer, std::initializer_list<float> values)
{
    for (const float value : values)
    {
        writer.decimal(value);
    }
}

void writeValue(ByteWriter& writer, const RpcValue& value)
{
    writer.number(value.index(), 1);
    if (const bool* const flag = std::get_if<bool>(&value))
    {
        writer.number(*flag ? 1 : 0, 1);
    }
    else if (const std::int64_t* const whole = std::get_if<std::int64_t>(&value))
    {
        writer.number(static_cast<std::uint64_t>(*whole), sizeof(std::int64_t));
    }
    else if (const double* const decimal = std::get_if<double>(&value))
    {
        writer.wide(*decimal);
    }
    else if (const std::string* const text = std::get_if<std::string>(&value))
    {
        writer.text(*text);
    }
    else if (const math::Vec2* const vec2 = std::get_if<math::Vec2>(&value))
    {
        writeFloats(writer, {vec2->x, vec2->y});
    }
    else if (const math::Vec3* const vec3 = std::get_if<math::Vec3>(&value))
    {
        writeFloats(writer, {vec3->x, vec3->y, vec3->z});
    }
    else if (const math::Vec4* const vec4 = std::get_if<math::Vec4>(&value))
    {
        writeFloats(writer, {vec4->x, vec4->y, vec4->z, vec4->w});
    }
    else if (const math::Quat* const quat = std::get_if<math::Quat>(&value))
    {
        writeFloats(writer, {quat->x, quat->y, quat->z, quat->w});
    }
    else if (const core::Uuid* const uuid = std::get_if<core::Uuid>(&value))
    {
        writer.uuid(*uuid);
    }
    else if (const std::vector<std::byte>* const raw = std::get_if<std::vector<std::byte>>(&value))
    {
        writer.sized(*raw);
    }
}

[[nodiscard]] std::optional<RpcValue> readValue(ByteReader& reader)
{
    const auto type = static_cast<std::size_t>(reader.number(1));
    switch (type)
    {
    case 0:
        return RpcValue(std::in_place_type<bool>, reader.number(1) != 0);
    case 1:
        return RpcValue(std::in_place_type<std::int64_t>, static_cast<std::int64_t>(reader.number(sizeof(std::int64_t))));
    case 2:
        return RpcValue(std::in_place_type<double>, std::bit_cast<double>(reader.number(sizeof(double))));
    case 3: {
        const std::span<const std::byte> text = reader.sized();
        return RpcValue(std::in_place_type<std::string>, reinterpret_cast<const char*>(text.data()), text.size());
    }
    case 4: {
        math::Vec2 value;
        value.x = reader.decimal();
        value.y = reader.decimal();
        return RpcValue(value);
    }
    case 5: {
        math::Vec3 value;
        value.x = reader.decimal();
        value.y = reader.decimal();
        value.z = reader.decimal();
        return RpcValue(value);
    }
    case 6: {
        math::Vec4 value;
        value.x = reader.decimal();
        value.y = reader.decimal();
        value.z = reader.decimal();
        value.w = reader.decimal();
        return RpcValue(value);
    }
    case 7: {
        math::Quat value;
        value.x = reader.decimal();
        value.y = reader.decimal();
        value.z = reader.decimal();
        value.w = reader.decimal();
        return RpcValue(value);
    }
    case 8:
        return RpcValue(reader.uuid());
    case 9: {
        const std::span<const std::byte> raw = reader.sized();
        return RpcValue(std::in_place_type<std::vector<std::byte>>, raw.begin(), raw.end());
    }
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<RpcHeader> readHeader(ByteReader& reader)
{
    const core::Uuid target = reader.uuid();
    const auto length = static_cast<std::size_t>(reader.number(sizeof(std::uint16_t)));
    const std::span<const std::byte> name = reader.take(length);
    if (reader.failed() || length == 0 || length > maxRpcNameLength)
    {
        return std::nullopt;
    }
    return RpcHeader{.target = target, .name = {reinterpret_cast<const char*>(name.data()), name.size()}};
}

} // namespace

core::Result<std::vector<std::byte>> encodeRpc(const RpcCall& call)
{
    if (call.name.empty() || call.name.size() > maxRpcNameLength)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "A remote method has a name of 1 to {} bytes: '{}'", maxRpcNameLength, call.name);
    }
    if (call.arguments.size() > maxRpcArguments)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "A remote call has at most {} arguments: {} has {}", maxRpcArguments, call.name,
                               call.arguments.size());
    }
    std::vector<std::byte> bytes;
    bytes.reserve(targetSize + sizeof(std::uint16_t) + call.name.size() + 1);
    ByteWriter writer(bytes);
    writer.uuid(call.target);
    writer.number(call.name.size(), sizeof(std::uint16_t));
    writer.raw(std::as_bytes(std::span(call.name)));
    writer.number(call.arguments.size(), 1);
    for (const RpcValue& argument : call.arguments)
    {
        writeValue(writer, argument);
    }
    return bytes;
}

std::optional<RpcHeader> decodeRpcHeader(std::span<const std::byte> bytes)
{
    ByteReader reader(bytes);
    return readHeader(reader);
}

core::Result<RpcCall> decodeRpc(std::span<const std::byte> bytes)
{
    ByteReader reader(bytes);
    const std::optional<RpcHeader> header = readHeader(reader);
    if (!header)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "A remote call does not say what it is for");
    }
    RpcCall call{.target = header->target, .name = std::string(header->name), .arguments = {}};
    const auto count = static_cast<std::size_t>(reader.number(1));
    if (count > maxRpcArguments)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "The remote call {} has too many arguments", call.name);
    }
    call.arguments.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        std::optional<RpcValue> value = readValue(reader);
        if (!value || reader.failed())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "The remote call {} is cut short or malformed", call.name);
        }
        call.arguments.push_back(std::move(*value));
    }
    if (reader.failed())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "The remote call {} is cut short", call.name);
    }
    return call;
}

} // namespace devex::net
