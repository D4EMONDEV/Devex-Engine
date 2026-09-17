#pragma once

#include <devex/asset/AssetSource.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/MappedFile.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The package of an exported game (.dvxpak): its cooked assets and its settings in one file.
//
//     header     "DVXPAK", format version, offset and size of the index
//     data       the artifacts, each compressed with zstd when that makes it smaller
//     index      the text of the project, the icon, then one record per asset: identifier, type,
//                name, res:// path of its source file (main assets), source asset, and where its
//                bytes are
//
// The index comes last, so that assets are written as they are read. Readers map the file and
// decompress an asset when it is loaded.
namespace devex::asset {

inline constexpr std::string_view packageExtension = ".dvxpak";
inline constexpr std::uint32_t packageFormatVersion = 1;

// The icon of a game: 8-bit RGBA pixels, row by row from the top-left corner.
struct PackageIcon
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

struct PackageStatistics
{
    std::size_t assets = 0;
    // Bytes of the artifacts before and after compression.
    std::uint64_t artifactBytes = 0;
    std::uint64_t storedBytes = 0;
    // Size of the whole file.
    std::uint64_t fileBytes = 0;
};

// Writes a package to a temporary file next to its destination, which replaces the destination
// once finished.
class PackageWriter
{
public:
    [[nodiscard]] static core::Result<PackageWriter> create(const std::filesystem::path& path);

    PackageWriter(PackageWriter&&) noexcept = default;
    PackageWriter& operator=(PackageWriter&&) noexcept = default;
    // A writer destroyed before finish removes its temporary file.
    ~PackageWriter();

    // Adds an artifact. resourcePath is the res:// path of the source file of a main asset, empty
    // for the other assets of a file. Adding an identifier twice is an error.
    [[nodiscard]] core::Result<void> add(const AssetInfo& info, std::string_view resourcePath,
                                         std::span<const std::byte> artifact);
    void setProject(const Project& project);
    void setIcon(PackageIcon icon);

    // Writes the index and moves the package to its destination.
    [[nodiscard]] core::Result<PackageStatistics> finish();

private:
    struct Blob
    {
        std::uint64_t offset = 0;
        std::uint64_t storedSize = 0;
        std::uint64_t size = 0;
        bool compressed = false;
    };

    struct Record
    {
        AssetInfo info;
        std::string path;
        Blob blob;
    };

    PackageWriter(std::filesystem::path destination, std::filesystem::path temporary, std::ofstream file);
    [[nodiscard]] core::Result<Blob> writeBlob(std::span<const std::byte> bytes);

    std::filesystem::path m_destination;
    std::filesystem::path m_temporary;
    std::ofstream m_file;
    std::uint64_t m_offset = 0;
    std::vector<Record> m_records;
    std::unordered_map<AssetId, std::size_t> m_indices;
    std::string m_projectText;
    std::optional<PackageIcon> m_icon;
    PackageStatistics m_statistics;
    bool m_finished = false;
};

// The assets and settings of an exported game, read from its package.
class PackageReader final : public AssetSource
{
public:
    // Checks the header and the index; an asset whose data is damaged fails when it is loaded.
    [[nodiscard]] static core::Result<std::unique_ptr<PackageReader>> open(const std::filesystem::path& path);

    [[nodiscard]] const Project& project() const noexcept override;
    [[nodiscard]] const AssetInfo* find(AssetId id) const override;
    [[nodiscard]] std::vector<AssetInfo> assets(std::optional<AssetType> type = std::nullopt) const override;
    [[nodiscard]] std::optional<AssetId> findByPath(std::string_view resourcePath) const override;
    [[nodiscard]] core::Result<std::vector<std::byte>> loadArtifact(AssetId id) const override;
    [[nodiscard]] core::Result<std::string> sceneText(AssetId id) const override;

    // The icon of the game, when it has one.
    [[nodiscard]] core::Result<std::optional<PackageIcon>> icon() const;
    [[nodiscard]] std::size_t assetCount() const noexcept;

private:
    struct Blob
    {
        std::uint64_t offset = 0;
        std::uint64_t storedSize = 0;
        std::uint64_t size = 0;
        bool compressed = false;
    };

    struct Record
    {
        AssetInfo info;
        std::string path;
        Blob blob;
    };

    PackageReader() = default;
    [[nodiscard]] core::Result<std::vector<std::byte>> readBlob(const Blob& blob) const;

    std::filesystem::path m_path;
    core::MappedFile m_file;
    Project m_project;
    std::vector<Record> m_records;
    std::unordered_map<AssetId, std::size_t> m_indices;
    std::unordered_map<std::string, AssetId> m_paths;
    std::optional<Blob> m_icon;
    std::uint32_t m_iconWidth = 0;
    std::uint32_t m_iconHeight = 0;
};

} // namespace devex::asset
