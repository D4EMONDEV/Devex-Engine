#pragma once

#include <devex/core/Export.hpp>

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
    // An animation of a skeleton, imported from a model file.
    AnimationClip = 7,
    // A font baked into an atlas of distances, for the text of interfaces.
    Font = 8,
    // The look of an interface: named styles a canvas hands to its elements.
    Theme = 9,
    // A curve drawn by hand, which eases tweens.
    Curve = 10,
    // A rectangle of a texture drawn flat in the world, cut from its texture as it imports.
    Sprite = 11,
    // Named animations of sprites, a .dvxframes file.
    SpriteFrames = 12,
    // The tiles a tilemap paints with, a .dvxtileset file.
    Tileset = 13,
    // The state machine an Animator plays, a .dvxanimator file.
    Animator = 14,
    // Where agents walk in a scene, baked by the editor into a .dvxnavmesh file.
    NavMesh = 15,
    // The messages of keys in several languages, a .csv file.
    Translation = 16,
    // A shader of the project, a .dvxshader file compiled to SPIR-V at its import.
    Shader = 17,
};

// Whether a stored value is one of the types above: update it with them.
[[nodiscard]] constexpr bool isAssetType(std::uint8_t value) noexcept
{
    return value >= static_cast<std::uint8_t>(AssetType::Mesh) &&
           value <= static_cast<std::uint8_t>(AssetType::Shader);
}

// "mesh", "texture", "material", "model", "scene", "audio", "animation" or "font", as written in
// .dvxmeta files.
[[nodiscard]] DEVEX_API std::string_view toString(AssetType type) noexcept;
[[nodiscard]] DEVEX_API std::optional<AssetType> parseAssetType(std::string_view text) noexcept;

} // namespace devex::asset
