#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] DEVEX_API std::string_view toString(AlphaMode mode) noexcept;
[[nodiscard]] DEVEX_API std::optional<AlphaMode> parseAlphaMode(std::string_view text) noexcept;

// A value a material gives a uniform of its shader: numbers, or a texture for a sampler2D. A number
// fills x; vectors fill as many components as they have; true is 1.
struct DEVEX_API MaterialParameter
{
    std::string name;
    math::Vec4 value{0.0f};
    // How many numbers of the value were given, 1 to 4, as they are written back.
    std::uint8_t components = 1;
    AssetId texture;

    bool operator==(const MaterialParameter&) const = default;
};

// Metallic-roughness material with the parameters of glTF 2.0. Textures multiply their factors;
// an invalid texture identifier leaves the factor alone. Until physically based rendering exists,
// the renderer uses the base color, alpha, emission and ambient occlusion.
struct DEVEX_API MaterialData
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
    // A shader of the project that draws the material instead of the standard one: the values above
    // then go unused, and `parameters` give its uniforms by name. Those it leaves out keep the
    // defaults of the shader; those the shader does not have are kept, unused.
    AssetId shader;
    std::vector<MaterialParameter> parameters;

    // The value given to a uniform, or null.
    [[nodiscard]] const MaterialParameter* findParameter(std::string_view name) const noexcept;

    bool operator==(const MaterialData&) const = default;
};

} // namespace devex::asset
