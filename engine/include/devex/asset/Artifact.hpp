#pragma once

#include <devex/asset/AssetType.hpp>
#include <devex/asset/MaterialData.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/asset/ModelData.hpp>
#include <devex/asset/TextureData.hpp>
#include <devex/core/Error.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Cooked asset files: what an import produces, and what a game loads without parsing. Each file
// starts with a header naming the asset type and the version of its layout, followed by the data
// in the little-endian layout of serialization::BinaryWriter.
namespace devex::asset {

inline constexpr std::string_view artifactExtension = ".dvxasset";

// Bumped whenever the layout of a type changes: older files are then imported again.
[[nodiscard]] std::uint32_t artifactVersion(AssetType type) noexcept;

// Reads the header only.
[[nodiscard]] core::Result<AssetType> artifactType(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encodeMesh(const MeshData& mesh);
[[nodiscard]] std::vector<std::byte> encodeTexture(const TextureData& texture);
[[nodiscard]] std::vector<std::byte> encodeMaterial(const MaterialData& material);
[[nodiscard]] std::vector<std::byte> encodeModel(const ModelData& model);
// Scenes keep the text of their .dvxscene file, validated by the import.
[[nodiscard]] std::vector<std::byte> encodeScene(std::string_view text);

// Decoding validates the header, the version and the data itself.
[[nodiscard]] core::Result<MeshData> decodeMesh(std::span<const std::byte> bytes);
[[nodiscard]] core::Result<TextureData> decodeTexture(std::span<const std::byte> bytes);
[[nodiscard]] core::Result<MaterialData> decodeMaterial(std::span<const std::byte> bytes);
[[nodiscard]] core::Result<ModelData> decodeModel(std::span<const std::byte> bytes);
[[nodiscard]] core::Result<std::string> decodeScene(std::span<const std::byte> bytes);

} // namespace devex::asset
