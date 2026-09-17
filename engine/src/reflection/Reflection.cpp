#include <devex/reflection/Reflection.hpp>

#include <cstring>

namespace devex::reflection {

std::string_view toString(ValueKind kind) noexcept
{
    switch (kind)
    {
    case ValueKind::Bool:
        return "bool";
    case ValueKind::Int32:
        return "int32";
    case ValueKind::UInt32:
        return "uint32";
    case ValueKind::Float:
        return "float";
    case ValueKind::String:
        return "string";
    case ValueKind::Vec2:
        return "vec2";
    case ValueKind::Vec3:
        return "vec3";
    case ValueKind::Vec4:
        return "vec4";
    case ValueKind::Quat:
        return "quat";
    case ValueKind::Uuid:
        return "uuid";
    case ValueKind::AssetId:
        return "asset";
    case ValueKind::Enum:
        return "enum";
    }
    return "unknown";
}

std::uint32_t readEnumIndex(const FieldInfo& field, const void* address) noexcept
{
    if (field.enumSize == 1)
    {
        return *static_cast<const std::uint8_t*>(address);
    }
    std::uint32_t value = 0;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

void writeEnumIndex(const FieldInfo& field, void* address, std::uint32_t index) noexcept
{
    if (field.enumSize == 1)
    {
        *static_cast<std::uint8_t*>(address) = static_cast<std::uint8_t>(index);
        return;
    }
    std::memcpy(address, &index, sizeof(index));
}

} // namespace devex::reflection
