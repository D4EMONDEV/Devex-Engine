#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetSource.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/serialization/Text.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

enum class ImportStatus : std::uint8_t
{
    // Queued or running.
    Importing,
    Ready,
    // The last import failed; the assets of the previous successful import stay available.
    Failed,
};

[[nodiscard]] std::string_view toString(ImportStatus status) noexcept;

// A file of the assets folder that an importer handles.
struct SourceFile
{
    AssetId id;
    // res:// path.
    std::string path;
    std::string importer;
    ImportStatus status = ImportStatus::Importing;
    // Message of the last failed import.
    std::string error;
    // Assets of the last successful import, the main asset first.
    std::vector<AssetId> assets;
};

enum class AssetChange : std::uint8_t
{
    // New, or imported again with different data.
    Imported,
    Removed,
};

struct AssetEvent
{
    AssetId id;
    AssetType type = AssetType::Mesh;
    AssetChange change = AssetChange::Imported;
};

struct AssetDatabaseConfig
{
    // Watches the assets folder and imports files again when they change.
    bool watchFiles = true;
    // Changes are applied once the folder has been quiet this long, since editors often save a
    // file in several steps.
    std::chrono::milliseconds settleTime{250};
};

// Keeps the assets folder of a project imported. Every source file gets a .dvxmeta holding its
// identifiers and import options; imports run on the jobs and write cooked .dvxasset files and an
// import record into the .devex cache, so that unchanged files are never imported twice. Every
// member function must be called from the same thread, normally the main thread.
class AssetDatabase final : public AssetSource
{
public:
    // Reads the cache, then scans the folder and queues the imports it needs.
    [[nodiscard]] static core::Result<std::unique_ptr<AssetDatabase>> open(
        const Project& project, core::JobSystem& jobs, const AssetDatabaseConfig& config = {});

    // Cancels the imports in progress; their results are discarded.
    ~AssetDatabase() override;

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    [[nodiscard]] const Project& project() const noexcept override;
    // Changes the settings of the project and writes them to its file.
    [[nodiscard]] core::Result<void> updateProject(const Project& project);
    // Accept settings edited on disk without rewriting the project file.
    [[nodiscard]] core::Result<void> reloadProject();

    // Scans the assets folder: writes missing .dvxmeta files, forgets deleted sources and queues
    // the import of new and changed ones.
    void refresh();

    // Call once per frame: scans again once watched changes settle, then applies finished imports.
    // Returns the assets imported or removed since the previous call.
    [[nodiscard]] std::vector<AssetEvent> update();

    // Blocks until no import is queued or running. The next update reports their results.
    void waitForImports();
    [[nodiscard]] std::size_t pendingImports() const noexcept;

    // Imports again the source file that produced the asset, even when nothing changed.
    [[nodiscard]] core::Result<void> reimport(AssetId id);
    // Sets an import option in the .dvxmeta of the source file of the asset, and imports it again.
    [[nodiscard]] core::Result<void> setImportOption(AssetId id, std::string_view key, serialization::TextValue value);
    // An import option in the .dvxmeta of the source file of the asset; nullopt when it is not set.
    [[nodiscard]] std::optional<serialization::TextValue> importOption(AssetId id, std::string_view key) const;

    [[nodiscard]] const AssetInfo* find(AssetId id) const override;
    // Sorted by name, optionally of one type.
    [[nodiscard]] std::vector<AssetInfo> assets(std::optional<AssetType> type = std::nullopt) const override;
    // Sorted by path.
    [[nodiscard]] std::vector<SourceFile> sources() const;
    // The source file of any asset it produced.
    [[nodiscard]] std::optional<SourceFile> sourceOf(AssetId id) const;
    // The main asset of a source file, by res:// path.
    [[nodiscard]] std::optional<AssetId> findByPath(std::string_view resourcePath) const override;

    [[nodiscard]] std::filesystem::path artifactPath(AssetId id) const;
    [[nodiscard]] core::Result<std::vector<std::byte>> loadArtifact(AssetId id) const override;
    // Reads the artifact file found now: imports change the list of assets meanwhile.
    [[nodiscard]] ArtifactReader artifactReader(AssetId id) const override;
    // The source file of the scene when it can be read, otherwise its import.
    [[nodiscard]] core::Result<std::string> sceneText(AssetId id) const override;

    // Copies a file from outside the project into a folder of it, given as a res:// path, together
    // with the files a .gltf refers to. Returns the identifier of the main asset, which becomes
    // available once the import finishes.
    [[nodiscard]] core::Result<AssetId> addFile(const std::filesystem::path& file,
                                                std::string_view folder);

private:
    class Impl;
    explicit AssetDatabase(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace devex::asset
