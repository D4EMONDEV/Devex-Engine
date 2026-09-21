#include <devex/asset/Artifact.hpp>
#include <devex/asset/Package.hpp>
#include <devex/core/Path.hpp>
#include <devex/serialization/Binary.hpp>

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <system_error>
#include <utility>

namespace devex::asset {
namespace {

constexpr std::array<char, 8> packageMagic{'D', 'V', 'X', 'P', 'A', 'K', 0, 0};
constexpr std::size_t headerSize = 32;
// Artifacts smaller than this are stored as they are.
constexpr std::size_t minimumCompressedSize = 256;
constexpr int compressionLevel = 9;

struct Header
{
    std::array<char, 8> magic = packageMagic;
    std::uint32_t version = packageFormatVersion;
    std::uint32_t reserved = 0;
    std::uint64_t indexOffset = 0;
    std::uint64_t indexSize = 0;
};

void writeUuid(serialization::BinaryWriter& writer, core::Uuid uuid)
{
    writer.writeBytes(std::as_bytes(std::span(uuid.bytes())));
}

[[nodiscard]] core::Uuid readUuid(serialization::BinaryReader& reader)
{
    std::array<std::byte, 16> bytes{};
    reader.readBytes(bytes);
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    for (std::size_t index = 0; index < 8; ++index)
    {
        high = high << 8 | std::to_integer<std::uint64_t>(bytes[index]);
        low = low << 8 | std::to_integer<std::uint64_t>(bytes[8 + index]);
    }
    return core::Uuid::fromParts(high, low);
}

[[nodiscard]] std::vector<AssetInfo> sortedAssets(std::vector<AssetInfo> assets)
{
    std::ranges::sort(assets, [](const AssetInfo& left, const AssetInfo& right) { return left.name < right.name; });
    return assets;
}

} // namespace

PackageWriter::PackageWriter(std::filesystem::path destination, std::filesystem::path temporary, std::ofstream file)
    : m_destination(std::move(destination))
    , m_temporary(std::move(temporary))
    , m_file(std::move(file))
    , m_offset(headerSize)
{
}

core::Result<PackageWriter> PackageWriter::create(const std::filesystem::path& path)
{
    std::error_code error;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    const std::array<char, headerSize> placeholder{};
    if (!file || !file.write(placeholder.data(), placeholder.size()))
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(temporary));
    }
    return PackageWriter(path, std::move(temporary), std::move(file));
}

PackageWriter::~PackageWriter()
{
    if (!m_finished && !m_temporary.empty())
    {
        m_file.close();
        std::error_code error;
        std::filesystem::remove(m_temporary, error);
    }
}

core::Result<PackageWriter::Blob> PackageWriter::writeBlob(std::span<const std::byte> bytes)
{
    std::vector<std::byte> compressed;
    if (bytes.size() >= minimumCompressedSize)
    {
        compressed.resize(ZSTD_compressBound(bytes.size()));
        const std::size_t size =
            ZSTD_compress(compressed.data(), compressed.size(), bytes.data(), bytes.size(), compressionLevel);
        // Compression is kept when it saves at least a tenth.
        if (ZSTD_isError(size) != 0 || size >= bytes.size() - bytes.size() / 10)
        {
            compressed.clear();
        }
        else
        {
            compressed.resize(size);
        }
    }
    const std::span<const std::byte> stored = compressed.empty() ? bytes : std::span<const std::byte>(compressed);
    if (!m_file.write(static_cast<const char*>(static_cast<const void*>(stored.data())),
                      static_cast<std::streamsize>(stored.size())))
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(m_temporary));
    }
    const Blob blob{.offset = m_offset, .storedSize = stored.size(), .size = bytes.size(), .compressed = !compressed.empty()};
    m_offset += stored.size();
    return blob;
}

core::Result<void> PackageWriter::add(const AssetInfo& info, std::string_view resourcePath,
                                      std::span<const std::byte> artifact)
{
    if (m_indices.contains(info.id))
    {
        return core::makeError(core::ErrorCode::AlreadyExists, "asset {} is already in the package", info.id.uuid);
    }
    core::Result<Blob> blob = writeBlob(artifact);
    if (!blob)
    {
        return std::unexpected(blob.error());
    }
    m_indices.emplace(info.id, m_records.size());
    m_records.push_back({.info = info, .path = std::string(resourcePath), .blob = *blob});
    ++m_statistics.assets;
    m_statistics.artifactBytes += artifact.size();
    m_statistics.storedBytes += blob->storedSize;
    return {};
}

void PackageWriter::setProject(const Project& project)
{
    m_projectText = writeProjectText(project);
}

void PackageWriter::setIcon(PackageIcon icon)
{
    m_icon = std::move(icon);
}

core::Result<PackageStatistics> PackageWriter::finish()
{
    serialization::BinaryWriter index;
    const auto writeBlobRecord = [&index](const Blob& blob) {
        index.write(blob.offset);
        index.write(blob.storedSize);
        index.write(blob.size);
        index.write(static_cast<std::uint8_t>(blob.compressed ? 1 : 0));
    };

    index.writeString(m_projectText);
    index.write(static_cast<std::uint8_t>(m_icon ? 1 : 0));
    if (m_icon)
    {
        core::Result<Blob> blob = writeBlob(std::as_bytes(std::span(m_icon->rgba)));
        if (!blob)
        {
            return std::unexpected(blob.error());
        }
        index.write(m_icon->width);
        index.write(m_icon->height);
        writeBlobRecord(*blob);
    }
    index.write(static_cast<std::uint64_t>(m_records.size()));
    for (const Record& record : m_records)
    {
        writeUuid(index, record.info.id.uuid);
        index.write(static_cast<std::uint8_t>(record.info.type));
        index.writeString(record.info.name);
        index.writeString(record.path);
        writeUuid(index, record.info.source.uuid);
        writeBlobRecord(record.blob);
    }

    const std::vector<std::byte> indexBytes = index.take();
    const Header header{.indexOffset = m_offset, .indexSize = indexBytes.size()};
    if (!m_file.write(static_cast<const char*>(static_cast<const void*>(indexBytes.data())),
                      static_cast<std::streamsize>(indexBytes.size())) ||
        !m_file.seekp(0) ||
        !m_file.write(static_cast<const char*>(static_cast<const void*>(&header)), sizeof(header)))
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(m_temporary));
    }
    m_file.close();
    if (!m_file)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(m_temporary));
    }

    std::error_code error;
    std::filesystem::rename(m_temporary, m_destination, error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "cannot replace '{}': {}", core::toUtf8(m_destination),
                               error.message());
    }
    m_finished = true;
    m_statistics.fileBytes = m_offset + indexBytes.size();
    return m_statistics;
}

core::Result<std::unique_ptr<PackageReader>> PackageReader::open(const std::filesystem::path& path)
{
    core::Result<core::MappedFile> file = core::MappedFile::open(path);
    if (!file)
    {
        return std::unexpected(file.error());
    }
    const std::span<const std::byte> bytes = file->bytes();
    const auto damaged = [&path](std::string_view what) {
        return core::makeError(core::ErrorCode::Parse, "'{}' is not a valid game package: {}", core::toUtf8(path), what);
    };
    Header header;
    if (bytes.size() < headerSize)
    {
        return damaged("the file is too short");
    }
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != packageMagic)
    {
        return damaged("the header is missing");
    }
    if (header.version != packageFormatVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported, "'{}' has package format {}, this engine reads format {}",
                               core::toUtf8(path), header.version, packageFormatVersion);
    }
    if (header.indexOffset < headerSize || header.indexOffset > bytes.size() ||
        header.indexSize > bytes.size() - header.indexOffset)
    {
        return damaged("the index is out of the file");
    }

    std::unique_ptr<PackageReader> reader(new PackageReader());
    reader->m_path = path;
    serialization::BinaryReader index(bytes.subspan(header.indexOffset, header.indexSize));
    const auto readBlob = [&index, &header]() -> std::optional<Blob> {
        Blob blob{.offset = index.read<std::uint64_t>(), .storedSize = index.read<std::uint64_t>(),
                  .size = index.read<std::uint64_t>(), .compressed = index.read<std::uint8_t>() != 0};
        if (blob.offset < headerSize || blob.offset > header.indexOffset || blob.storedSize > header.indexOffset - blob.offset)
        {
            return std::nullopt;
        }
        return blob;
    };

    const std::string projectText = index.readString();
    core::Result<Project> project = parseProject(projectText, path);
    if (!project)
    {
        return std::unexpected(project.error());
    }
    reader->m_project = std::move(*project);

    if (index.read<std::uint8_t>() != 0)
    {
        reader->m_iconWidth = index.read<std::uint32_t>();
        reader->m_iconHeight = index.read<std::uint32_t>();
        reader->m_icon = readBlob();
        if (!reader->m_icon)
        {
            return damaged("the icon is out of the file");
        }
    }

    const auto count = index.read<std::uint64_t>();
    if (count > index.remaining())
    {
        return damaged("the index is truncated");
    }
    reader->m_records.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t entry = 0; entry < count && !index.failed(); ++entry)
    {
        Record record;
        record.info.id = AssetId{readUuid(index)};
        const auto type = index.read<std::uint8_t>();
        record.info.name = index.readString();
        record.path = index.readString();
        record.info.source = AssetId{readUuid(index)};
        const std::optional<Blob> blob = readBlob();
        if (!isAssetType(type) || !blob)
        {
            return damaged("an asset record is invalid");
        }
        record.info.type = static_cast<AssetType>(type);
        record.blob = *blob;
        if (!reader->m_indices.emplace(record.info.id, reader->m_records.size()).second)
        {
            return damaged("an asset appears twice");
        }
        if (!record.path.empty())
        {
            reader->m_paths.emplace(record.path, record.info.id);
        }
        reader->m_records.push_back(std::move(record));
    }
    if (index.failed())
    {
        return damaged("the index is truncated");
    }
    reader->m_file = std::move(*file);
    return reader;
}

const Project& PackageReader::project() const noexcept
{
    return m_project;
}

const AssetInfo* PackageReader::find(AssetId id) const
{
    const auto found = m_indices.find(id);
    return found != m_indices.end() ? &m_records[found->second].info : nullptr;
}

std::vector<AssetInfo> PackageReader::assets(std::optional<AssetType> type) const
{
    std::vector<AssetInfo> result;
    for (const Record& record : m_records)
    {
        if (!type || record.info.type == *type)
        {
            result.push_back(record.info);
        }
    }
    return sortedAssets(std::move(result));
}

std::optional<AssetId> PackageReader::findByPath(std::string_view resourcePath) const
{
    const auto found = m_paths.find(std::string(resourcePath));
    return found != m_paths.end() ? std::optional(found->second) : std::nullopt;
}

core::Result<std::vector<std::byte>> PackageReader::readBlob(const Blob& blob) const
{
    const std::span<const std::byte> stored = m_file.bytes().subspan(blob.offset, blob.storedSize);
    if (!blob.compressed)
    {
        if (blob.size != blob.storedSize)
        {
            return core::makeError(core::ErrorCode::Parse, "damaged data in '{}'", core::toUtf8(m_path));
        }
        return std::vector<std::byte>(stored.begin(), stored.end());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(blob.size));
    const std::size_t size = ZSTD_decompress(bytes.data(), bytes.size(), stored.data(), stored.size());
    if (ZSTD_isError(size) != 0 || size != bytes.size())
    {
        return core::makeError(core::ErrorCode::Parse, "damaged data in '{}'", core::toUtf8(m_path));
    }
    return bytes;
}

core::Result<std::vector<std::byte>> PackageReader::loadArtifact(AssetId id) const
{
    const auto found = m_indices.find(id);
    if (found == m_indices.end())
    {
        return core::makeError(core::ErrorCode::NotFound, "asset {} is not in the game package", id.uuid);
    }
    return readBlob(m_records[found->second].blob);
}

core::Result<std::string> PackageReader::sceneText(AssetId id) const
{
    const AssetInfo* const info = find(id);
    if (info == nullptr || info->type != AssetType::Scene)
    {
        return core::makeError(core::ErrorCode::NotFound, "scene {} is not in the game package", id.uuid);
    }
    const core::Result<std::vector<std::byte>> bytes = loadArtifact(id);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    return decodeScene(*bytes);
}

core::Result<std::optional<PackageIcon>> PackageReader::icon() const
{
    if (!m_icon)
    {
        return std::optional<PackageIcon>();
    }
    core::Result<std::vector<std::byte>> bytes = readBlob(*m_icon);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    if (bytes->size() != static_cast<std::size_t>(m_iconWidth) * m_iconHeight * 4)
    {
        return core::makeError(core::ErrorCode::Parse, "the icon of '{}' is damaged", core::toUtf8(m_path));
    }
    PackageIcon icon{.width = m_iconWidth, .height = m_iconHeight};
    icon.rgba.resize(bytes->size());
    std::memcpy(icon.rgba.data(), bytes->data(), bytes->size());
    return std::optional<PackageIcon>(std::move(icon));
}

std::size_t PackageReader::assetCount() const noexcept
{
    return m_records.size();
}

} // namespace devex::asset
