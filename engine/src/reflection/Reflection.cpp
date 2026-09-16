#include <devex/reflection/Reflection.hpp>

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
    }
    return "unknown";
}

} // namespace devex::reflection
