#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AnimationData.hpp>
#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/asset/FontData.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/math/Math.hpp>
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
struct DEVEX_API ImportedArtifact
{
    AssetId id;
    AssetType type = AssetType::Mesh;
    std::string name;
    std::vector<std::byte> bytes;
};

// Identifiers of the assets inside a source file, as listed in its .dvxmeta. An importer names
// each sub-asset with a key that survives changes to the file, such as a mesh name: a known key
// keeps its identifier, and a new key receives a generated one.
class DEVEX_API SubAssetIds
{
public:
    SubAssetIds() = default;
    explicit SubAssetIds(std::vector<MetaSubAsset> entries) noexcept;

    [[nodiscard]] AssetId acquire(AssetType type, std::string_view key);
    // The entry of a sub-asset, with its options; null when the file never had it.
    [[nodiscard]] const MetaSubAsset* find(AssetType type, std::string_view key) const noexcept;

    // Known entries first, in their original order, then the ones added by acquire. Entries that
    // an import no longer uses are kept, so that a key coming back finds its identifier again.
    [[nodiscard]] const std::vector<MetaSubAsset>& entries() const noexcept;
    [[nodiscard]] bool hasNewEntries() const noexcept;

private:
    std::vector<MetaSubAsset> m_entries;
    bool m_hasNewEntries = false;
};

// Everything an importer receives. It may run on a worker thread: it must not touch engine state.
struct DEVEX_API ImportContext
{
    std::filesystem::path source;
    // Identifier of the main asset of the file, from its .dvxmeta.
    AssetId mainId;
    std::string name;
    std::vector<serialization::TextProperty> options;
    SubAssetIds subAssets;
    // The tables of translations of the project, for an importer that reads them.
    std::vector<std::filesystem::path> translations;
    // Available for parallel work when set.
    core::JobSystem* jobs = nullptr;
    const std::atomic<bool>* cancelled = nullptr;

    [[nodiscard]] bool isCancelled() const noexcept;
    [[nodiscard]] bool boolOption(std::string_view key, bool fallback) const noexcept;
    [[nodiscard]] double numberOption(std::string_view key, double fallback) const noexcept;
    [[nodiscard]] std::string stringOption(std::string_view key, std::string_view fallback) const;
    // The numbers of a vec2(...), vec3(...) or vec4(...) option, in order; the components the
    // option does not give keep those of the fallback.
    [[nodiscard]] math::Vec4 vectorOption(std::string_view key, math::Vec4 fallback) const noexcept;
    // The asset an asset("uuid") option names; invalid without one.
    [[nodiscard]] AssetId assetOption(std::string_view key) const noexcept;
    // The events the import settings give the animation clip of that key, those within its duration,
    // in order; the others are left out with a warning.
    [[nodiscard]] std::vector<AnimationEvent> animationEvents(std::string_view key, float duration) const;
};

struct DEVEX_API ImportResult
{
    // The main asset first, then the sub-assets.
    std::vector<ImportedArtifact> artifacts;
    // Other files the import read, such as the buffers and images of a .gltf file. A change to
    // one of them imports the source again.
    std::vector<std::filesystem::path> dependencies;
};

struct DEVEX_API Importer
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
    // Whether the importer reads the tables of translations of the project, which the context
    // gives: a source imports again when one of them changes, comes or goes.
    bool readsTranslations = false;
    // The other files a source reads, found without importing it, such as the images next to a
    // model; they are copied with it into the project. Null for formats that read none.
    core::Result<std::vector<std::filesystem::path>> (*findDependencies)(const std::filesystem::path& file) = nullptr;
};

// Texture images, .dvxmat materials, glTF, FBX and OBJ models, sounds, fonts, curves, sprite
// frames, tilesets and .dvxscene scenes.
[[nodiscard]] DEVEX_API std::span<const Importer> importers();
// The extension is compared without regard to case.
[[nodiscard]] DEVEX_API const Importer* findImporterForExtension(std::string_view extension);
[[nodiscard]] DEVEX_API const Importer* findImporter(std::string_view name);

// Importers of the built-in formats, also usable directly.
[[nodiscard]] DEVEX_API core::Result<ImportResult> importTextureFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importMaterialFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importGltfFile(ImportContext& context);
// FBX and OBJ files, read by ufbx and converted to the engine conventions: Y up, right-handed, one
// unit per meter, times the "scale" option.
[[nodiscard]] DEVEX_API core::Result<ImportResult> importFbxFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importSceneFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importCurveFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importSpriteFramesFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importTilesetFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importAnimatorFile(ImportContext& context);
[[nodiscard]] DEVEX_API core::Result<ImportResult> importNavMeshFile(ImportContext& context);
// Sounds keep their file; the "loading" option chooses "decoded", "streamed" or "auto".
[[nodiscard]] DEVEX_API core::Result<ImportResult> importAudioFile(ImportContext& context);
// Fonts are baked into an atlas of distances at the "size" of the option, with the "spread" of the
// distances around each outline, and the characters of the translations of the project.
[[nodiscard]] DEVEX_API core::Result<ImportResult> importFontFile(ImportContext& context);
// Shaders of the project, .dvxshader files, compiled by slangc. One that does not compile imports
// with its errors and without code.
[[nodiscard]] DEVEX_API core::Result<ImportResult> importShaderFile(ImportContext& context);
// Shader graphs, .dvxshadergraph files, turned into code and compiled as a shader is.
[[nodiscard]] DEVEX_API core::Result<ImportResult> importShaderGraphFile(ImportContext& context);
// Tables of translations, .csv files: the "delimiter" option is "auto", "comma", "semicolon" or "tab".
[[nodiscard]] DEVEX_API core::Result<ImportResult> importTranslationFile(ImportContext& context);
// Bakes the letters of a TrueType font into an atlas of distances, as the font importer does: the
// em at `size` pixels, the distances spread over `spread` pixels, the Latin alphabet with the
// `characters` asked for. The editor bakes its own fonts so.
[[nodiscard]] DEVEX_API core::Result<FontData> bakeFont(std::span<const std::byte> file, std::string family, float size = 48.0f,
                                                        float spread = 6.0f, std::span<const std::uint32_t> characters = {});

// Local files that a .gltf or .glb file refers to, such as external buffers and images.
[[nodiscard]] DEVEX_API core::Result<std::vector<std::filesystem::path>> findGltfDependencies(
              const std::filesystem::path& file);
// The images an FBX or OBJ file refers to and finds beside it, and the .mtl of an OBJ file.
[[nodiscard]] DEVEX_API core::Result<std::vector<std::filesystem::path>> findFbxDependencies(
              const std::filesystem::path& file);

} // namespace devex::asset
