#include <devex/asset/ShaderData.hpp>

#include <array>
#include <utility>

namespace devex::asset {
namespace {

constexpr std::array<std::pair<ShaderKind, std::string_view>, 4> kindNames{{
    {ShaderKind::Spatial, "spatial"},
    {ShaderKind::CanvasItem, "canvas_item"},
    {ShaderKind::Particles, "particles"},
    {ShaderKind::Sky, "sky"},
}};

constexpr std::array<std::pair<ShaderParameterType, std::string_view>, 7> typeNames{{
    {ShaderParameterType::Float, "float"},
    {ShaderParameterType::Float2, "float2"},
    {ShaderParameterType::Float3, "float3"},
    {ShaderParameterType::Float4, "float4"},
    {ShaderParameterType::Int, "int"},
    {ShaderParameterType::Bool, "bool"},
    {ShaderParameterType::Texture, "sampler2D"},
}};

template <typename Value, std::size_t Count>
[[nodiscard]] std::string_view nameOf(const std::array<std::pair<Value, std::string_view>, Count>& names, Value value) noexcept
{
    for (const auto& [candidate, name] : names)
    {
        if (candidate == value)
        {
            return name;
        }
    }
    return "unknown";
}

template <typename Value, std::size_t Count>
[[nodiscard]] std::optional<Value> valueOf(const std::array<std::pair<Value, std::string_view>, Count>& names,
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

} // namespace

std::string_view toString(ShaderKind kind) noexcept
{
    return nameOf(kindNames, kind);
}

std::optional<ShaderKind> parseShaderKind(std::string_view text) noexcept
{
    return valueOf(kindNames, text);
}

std::string_view toString(ShaderParameterType type) noexcept
{
    return nameOf(typeNames, type);
}

std::optional<ShaderParameterType> parseShaderParameterType(std::string_view text) noexcept
{
    return valueOf(typeNames, text);
}

std::uint32_t componentCount(ShaderParameterType type) noexcept
{
    switch (type)
    {
    case ShaderParameterType::Float2:
        return 2;
    case ShaderParameterType::Float3:
        return 3;
    case ShaderParameterType::Float4:
        return 4;
    case ShaderParameterType::Float:
    case ShaderParameterType::Int:
    case ShaderParameterType::Bool:
    case ShaderParameterType::Texture:
        return 1;
    }
    return 1;
}

const ShaderParameter* ShaderData::findParameter(std::string_view name) const noexcept
{
    for (const ShaderParameter& parameter : parameters)
    {
        if (parameter.name == name)
        {
            return &parameter;
        }
    }
    return nullptr;
}

} // namespace devex::asset
