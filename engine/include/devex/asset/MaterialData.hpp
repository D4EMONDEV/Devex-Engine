#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace devex::asset {

// The values are stored in cooked files: never reorder them.
enum class AlphaMode : std::uint8_t
{
    Opaque = 0,
    // Fully transparent below the cutoff, opaque above.
    Mask = 1,
    // Blended with what is behind. Drawn as opaque until transparency is rendered.
    Blend = 2,
};

[[nodiscard]] std::string_view toString(AlphaMode mode) noexcept;
[[nodiscard]] std::optional<AlphaMode> parseAlphaMode(std::string_view text) noexcept;

// Metallic-roughness material with the parameters of glTF 2.0. Textures multiply their factors;
// an invalid texture identifier leaves the factor alone. Until physically based rendering exists,
// the renderer uses the base color, alpha, emission and ambient occlusion.
struct MaterialData
{
    // Linear RGBA.
    math::Vec4 baseColorFactor{1.0f};
    AssetId baseColorTexture;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    // Roughness in green, metalness in blue.
    AssetId metallicRoughnessTexture;
    AssetId normalTexture;
    float normalScale = 1.0f;
    // Occlusion in red.
    AssetId occlusionTexture;
    float occlusionStrength = 1.0f;
    // Linear RGB.
    math::Vec3 emissiveFactor{0.0f};
    AssetId emissiveTexture;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    // Renders back faces too, lit as seen from their side.
    bool doubleSided = false;

    bool operator==(const MaterialData&) const = default;
};

} // namespace devex::asset
