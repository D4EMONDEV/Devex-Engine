#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace devex::asset {

// Kinds of imported assets. The values are stored in cooked files: never reorder them.
enum class AssetType : std::uint8_t
{
    Mesh = 1,
    Texture = 2,
    Material = 3,
    // A hierarchy of nodes referring to meshes, instantiated as entities.
    Model = 4,
    // A .dvxscene file: entities and components, opened by the editor or loaded as a level.
    Scene = 5,
    // A sound: .wav, .ogg, .mp3 or .flac.
    AudioClip = 6,
};

// Whether a stored value is one of the types above: update it with them.
[[nodiscard]] constexpr bool isAssetType(std::uint8_t value) noexcept
{
    return value >= static_cast<std::uint8_t>(AssetType::Mesh) && value <= static_cast<std::uint8_t>(AssetType::AudioClip);
}

// "mesh", "texture", "material", "model", "scene" or "audio", as written in .dvxmeta files.
[[nodiscard]] std::string_view toString(AssetType type) noexcept;
[[nodiscard]] std::optional<AssetType> parseAssetType(std::string_view text) noexcept;

} // namespace devex::asset
