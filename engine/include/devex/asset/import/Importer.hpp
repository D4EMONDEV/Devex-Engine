#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/serialization/Text.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

// A cooked asset produced by an import, written to the cache as <uuid>.dvxasset.
struct ImportedArtifact
{
    AssetId id;
    AssetType type = AssetType::Mesh;
    std::string name;
    std::vector<std::byte> bytes;
};

// Identifiers of the assets inside a source file, as listed in its .dvxmeta. An importer names
// each sub-asset with a key that survives changes to the file, such as a mesh name: a known key
// keeps its identifier, and a new key receives a generated one.
class SubAssetIds
{
public:
    SubAssetIds() = default;
    explicit SubAssetIds(std::vector<MetaSubAsset> entries) noexcept;

    [[nodiscard]] AssetId acquire(AssetType type, std::string_view key);

    // Known entries first, in their original order, then the ones added by acquire. Entries that
    // an import no longer uses are kept, so that a key coming back finds its identifier again.
    [[nodiscard]] const std::vector<MetaSubAsset>& entries() const noexcept;
    [[nodiscard]] bool hasNewEntries() const noexcept;

private:
    std::vector<MetaSubAsset> m_entries;
    bool m_hasNewEntries = false;
};

// Everything an importer receives. It may run on a worker thread: it must not touch engine state.
struct ImportContext
{
    std::filesystem::path source;
    // Identifier of the main asset of the file, from its .dvxmeta.
    AssetId mainId;
    std::string name;
    std::vector<serialization::TextProperty> options;
    SubAssetIds subAssets;
    // Available for parallel work when set.
    core::JobSystem* jobs = nullptr;
    const std::atomic<bool>* cancelled = nullptr;

    [[nodiscard]] bool isCancelled() const noexcept;
    [[nodiscard]] bool boolOption(std::string_view key, bool fallback) const noexcept;
    [[nodiscard]] std::string stringOption(std::string_view key, std::string_view fallback) const;
};

struct ImportResult
{
    // The main asset first, then the sub-assets.
    std::vector<ImportedArtifact> artifacts;
    // Other files the import read, such as the buffers and images of a .gltf file. A change to
    // one of them imports the source again.
    std::vector<std::filesystem::path> dependencies;
};

struct Importer
{
    std::string_view name;
    // Increased whenever the importer produces different data, so that sources import again.
    std::uint32_t version = 1;
    AssetType mainType = AssetType::Mesh;
    // Lowercase, with the leading dot.
    std::vector<std::string_view> extensions;
    // Written into new .dvxmeta files.
    std::vector<serialization::TextProperty> defaultOptions;
    core::Result<ImportResult> (*run)(ImportContext& context) = nullptr;
};

// Texture images, .dvxmat materials, glTF models and .dvxscene scenes.
[[nodiscard]] std::span<const Importer> importers();
// The extension is compared without regard to case.
[[nodiscard]] const Importer* findImporterForExtension(std::string_view extension);
[[nodiscard]] const Importer* findImporter(std::string_view name);

// Importers of the built-in formats, also usable directly.
[[nodiscard]] core::Result<ImportResult> importTextureFile(ImportContext& context);
[[nodiscard]] core::Result<ImportResult> importMaterialFile(ImportContext& context);
[[nodiscard]] core::Result<ImportResult> importGltfFile(ImportContext& context);
[[nodiscard]] core::Result<ImportResult> importSceneFile(ImportContext& context);

// Local files that a .gltf or .glb file refers to, such as external buffers and images.
[[nodiscard]] core::Result<std::vector<std::filesystem::path>> findGltfDependencies(
    const std::filesystem::path& file);

} // namespace devex::asset
