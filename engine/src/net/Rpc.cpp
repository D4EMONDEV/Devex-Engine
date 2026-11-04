#include <devex/net/Rpc.hpp>

#include <bit>
#include <initializer_list>

namespace devex::net {

namespace {

// A call on the network, numbers in little endian: the 16 bytes of the target, the length of the name
// on 2 bytes, the name, the count of arguments on 1 byte, then each argument: its type on 1 byte, then
// its value. Text and bytes start with their length on 4 bytes. The format C# writes too.
constexpr std::size_t targetSize = 16;

void writeNumber(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        bytes.push_back(static_cast<std::byte>(value >> (8 * index)));
    }
}

void writeFloats(std::vector<std::byte>& bytes, std::initializer_list<float> values)
{
    for (const float value : values)
    {
        writeNumber(bytes, std::bit_cast<std::uint32_t>(value), sizeof(float));
    }
}

void writeBytes(std::vector<std::byte>& bytes, std::span<const std::byte> value)
{
    writeNumber(bytes, value.size(), sizeof(std::uint32_t));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void writeValue(std::vector<std::byte>& bytes, const RpcValue& value)
{
    bytes.push_back(static_cast<std::byte>(value.index()));
    if (const bool* const flag = std::get_if<bool>(&value))
    {
        bytes.push_back(static_cast<std::byte>(*flag ? 1 : 0));
    }
    else if (const std::int64_t* const whole = std::get_if<std::int64_t>(&value))
    {
        writeNumber(bytes, static_cast<std::uint64_t>(*whole), sizeof(std::int64_t));
    }
    else if (const double* const decimal = std::get_if<double>(&value))
    {
        writeNumber(bytes, std::bit_cast<std::uint64_t>(*decimal), sizeof(double));
    }
    else if (const std::string* const text = std::get_if<std::string>(&value))
    {
        writeBytes(bytes, std::as_bytes(std::span(*text)));
    }
    else if (const math::Vec2* const vec2 = std::get_if<math::Vec2>(&value))
    {
        writeFloats(bytes, {vec2->x, vec2->y});
    }
    else if (const math::Vec3* const vec3 = std::get_if<math::Vec3>(&value))
    {
        writeFloats(bytes, {vec3->x, vec3->y, vec3->z});
    }
    else if (const math::Vec4* const vec4 = std::get_if<math::Vec4>(&value))
    {
        writeFloats(bytes, {vec4->x, vec4->y, vec4->z, vec4->w});
    }
    else if (const math::Quat* const quat = std::get_if<math::Quat>(&value))
    {
        writeFloats(bytes, {quat->x, quat->y, quat->z, quat->w});
    }
    else if (const core::Uuid* const uuid = std::get_if<core::Uuid>(&value))
    {
        const std::span<const std::byte> identifier = std::as_bytes(std::span(uuid->bytes()));
        bytes.insert(bytes.end(), identifier.begin(), identifier.end());
    }
    else if (const std::vector<std::byte>* const raw = std::get_if<std::vector<std::byte>>(&value))
    {
        writeBytes(bytes, *raw);
    }
}

// Reads what comes from the network: past the end, every read gives nothing and it stays failed.
class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes)
        : m_bytes(bytes)
    {
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return m_failed;
    }

    [[nodiscard]] std::span<const std::byte> take(std::size_t size) noexcept
    {
        if (m_failed || size > m_bytes.size() - m_at)
        {
            m_failed = true;
            return {};
        }
        const std::span<const std::byte> taken = m_bytes.subspan(m_at, size);
        m_at += size;
        return taken;
    }

    [[nodiscard]] std::uint64_t number(std::size_t size) noexcept
    {
        std::uint64_t value = 0;
        const std::span<const std::byte> taken = take(size);
        for (std::size_t index = 0; index < taken.size(); ++index)
        {
            value |= std::to_integer<std::uint64_t>(taken[index]) << (8 * index);
        }
        return value;
    }

    [[nodiscard]] float decimal() noexcept
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(number(sizeof(float))));
    }

    [[nodiscard]] core::Uuid uuid() noexcept
    {
        const std::uint64_t high = bigNumber();
        const std::uint64_t low = bigNumber();
        return core::Uuid::fromParts(high, low);
    }

    [[nodiscard]] std::span<const std::byte> sized() noexcept
    {
        return take(static_cast<std::size_t>(number(sizeof(std::uint32_t))));
    }

private:
    // The identifier keeps its bytes in the order the engine stores them, the first one the highest.
    [[nodiscard]] std::uint64_t bigNumber() noexcept
    {
        std::uint64_t value = 0;
        for (const std::byte byte : take(sizeof(std::uint64_t)))
        {
            value = (value << 8) | std::to_integer<std::uint64_t>(byte);
        }
        return value;
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_at = 0;
    bool m_failed = false;
};

[[nodiscard]] std::optional<RpcValue> readValue(Reader& reader)
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

[[nodiscard]] std::optional<RpcHeader> readHeader(Reader& reader)
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
    const std::span<const std::byte> target = std::as_bytes(std::span(call.target.bytes()));
    bytes.insert(bytes.end(), target.begin(), target.end());
    writeNumber(bytes, call.name.size(), sizeof(std::uint16_t));
    const std::span<const std::byte> name = std::as_bytes(std::span(call.name));
    bytes.insert(bytes.end(), name.begin(), name.end());
    writeNumber(bytes, call.arguments.size(), 1);
    for (const RpcValue& argument : call.arguments)
    {
        writeValue(bytes, argument);
    }
    return bytes;
}

std::optional<RpcHeader> decodeRpcHeader(std::span<const std::byte> bytes)
{
    Reader reader(bytes);
    return readHeader(reader);
}

core::Result<RpcCall> decodeRpc(std::span<const std::byte> bytes)
{
    Reader reader(bytes);
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
