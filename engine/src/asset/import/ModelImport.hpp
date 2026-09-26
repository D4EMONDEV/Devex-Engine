#pragma once

// What the importers of model files (glTF, FBX, OBJ) share: the keys of their sub-assets in the
// .dvxmeta, and the building of their textures.

#include <devex/asset/AssetId.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/Error.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset::detail {

enum class TextureRole : std::uint8_t
{
    // sRGB colors: base color and emission.
    Color,
    // Linear values: metallic-roughness and occlusion.
    Data,
    Normal,
};

// Appended to the key of an image: one image may be built as several textures.
[[nodiscard]] std::string_view keySuffix(TextureRole role) noexcept;

// Names used as keys in the .dvxmeta: the name from the file when present, a fallback otherwise,
// and the index appended to names shared by several elements.
[[nodiscard]] std::vector<std::string> uniqueKeys(std::vector<std::string> names);

// A texture of a model file, whose image is decoded when it is built.
struct TextureRequest
{
    AssetId id;
    // Key and name of the texture, with the suffix of its role.
    std::string key;
    TextureRole role = TextureRole::Color;
    std::function<core::Result<Image>()> decode;
};

// Builds the textures, on the jobs of the context when it has some, with its "compress_textures"
// and "texture_quality" options. A texture that cannot be built is logged and left empty, so
// that the materials using it fall back to their factors.
[[nodiscard]] std::vector<std::optional<ImportedArtifact>> buildTextures(
    const ImportContext& context, std::span<const TextureRequest> requests);

} // namespace devex::asset::detail
