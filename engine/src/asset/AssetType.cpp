#include <devex/asset/AssetType.hpp>

#include <array>
#include <utility>

namespace devex::asset {
namespace {

constexpr std::array<std::pair<AssetType, std::string_view>, 6> typeNames{{
    {AssetType::Mesh, "mesh"},
    {AssetType::Texture, "texture"},
    {AssetType::Material, "material"},
    {AssetType::Model, "model"},
    {AssetType::Scene, "scene"},
    {AssetType::AudioClip, "audio"},
}};

} // namespace

std::string_view toString(AssetType type) noexcept
{
    for (const auto& [value, name] : typeNames)
    {
        if (value == type)
        {
            return name;
        }
    }
    return "unknown";
}

std::optional<AssetType> parseAssetType(std::string_view text) noexcept
{
    for (const auto& [value, name] : typeNames)
    {
        if (name == text)
        {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace devex::asset
