#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/core/Error.hpp>
#include <devex/serialization/Text.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

inline constexpr std::string_view metaExtension = ".dvxmeta";

// An asset inside a source file, such as a mesh of a glTF file. The key names it the same way
// across imports, so that it keeps its identifier when the file changes.
struct MetaSubAsset
{
    AssetType type = AssetType::Mesh;
    std::string key;
    AssetId id;

    bool operator==(const MetaSubAsset&) const = default;
};

// Contents of the .dvxmeta file stored next to each source file and versioned with it:
//
//     [asset format=1 uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" importer="texture"]
//     srgb = true
//
//     [subasset type="mesh" key="Crate" uuid="b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"]
struct MetaFile
{
    AssetId id;
    std::string importer;
    // Importer options, the properties of the [asset] section.
    std::vector<serialization::TextProperty> options;
    std::vector<MetaSubAsset> subAssets;
};

[[nodiscard]] core::Result<MetaFile> parseMetaFile(std::string_view text);
[[nodiscard]] std::string writeMetaFile(const MetaFile& meta);

// Changes when the importer or its options change, which requires importing the source again.
[[nodiscard]] std::uint64_t importSettingsHash(const MetaFile& meta);

} // namespace devex::asset
