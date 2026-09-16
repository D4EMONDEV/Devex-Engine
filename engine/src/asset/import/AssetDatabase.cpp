#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/core/Assert.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Hash.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/serialization/Text.hpp>

#include <efsw/efsw.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <ranges>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace devex::asset {
namespace {

using Clock = std::chrono::steady_clock;
using serialization::TextSection;
using serialization::TextValue;

constexpr std::int64_t recordFormatVersion = 1;
constexpr std::string_view recordExtension = ".dvxsource";

// Enough to tell whether a file changed since it was imported without reading it again.
struct FileStamp
{
    std::uint64_t size = 0;
    std::int64_t time = 0;
    std::uint64_t hash = 0;
    bool exists = false;
};

[[nodiscard]] std::optional<FileStamp> quickStamp(const std::filesystem::path& file)
{
    std::error_code error;
    const std::uint64_t size = std::filesystem::file_size(file, error);
    if (error)
    {
        return std::nullopt;
    }
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    if (error)
    {
        return std::nullopt;
    }
    return FileStamp{.size = size, .time = time.time_since_epoch().count(), .exists = true};
}

// A missing file stamps as absent, so that a dependency appearing later counts as a change.
[[nodiscard]] FileStamp fullStamp(const std::filesystem::path& file)
{
    std::optional<FileStamp> stamp = quickStamp(file);
    if (!stamp)
    {
        return {};
    }
    const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(file);
    if (!bytes)
    {
        return {};
    }
    stamp->hash = core::hash64(*bytes);
    return *stamp;
}

// Compares the file with its stamp, reading the contents only when its size or time changed. A
// file touched without changing is re-stamped in place.
[[nodiscard]] bool isUnchanged(const std::filesystem::path& file, FileStamp& stamp)
{
    const std::optional<FileStamp> current = quickStamp(file);
    if (!current || !stamp.exists)
    {
        return current.has_value() == stamp.exists;
    }
    if (current->size == stamp.size && current->time == stamp.time)
    {
        return true;
    }
    const FileStamp contents = fullStamp(file);
    if (contents.exists && contents.size == stamp.size && contents.hash == stamp.hash)
    {
        stamp.time = contents.time;
        return true;
    }
    return false;
}

struct Dependency
{
    std::string path;
    FileStamp stamp;
};

// What the cache knows about the last import of a source file.
struct ImportRecord
{
    AssetId id;
    std::string path;
    std::string importer;
    std::uint32_t importerVersion = 0;
    std::uint64_t settingsHash = 0;
    FileStamp source;
    std::vector<Dependency> dependencies;
    std::vector<AssetInfo> artifacts;
    std::string error;
};

void addStamp(std::vector<serialization::TextProperty>& properties, const FileStamp& stamp)
{
    properties.push_back({"exists", TextValue(stamp.exists)});
    properties.push_back({"size", TextValue(static_cast<std::int64_t>(stamp.size))});
    properties.push_back({"time", TextValue(stamp.time)});
    properties.push_back({"hash", TextValue(core::toHex(stamp.hash))});
}

[[nodiscard]] std::string writeRecord(const ImportRecord& record)
{
    serialization::TextDocument document;
    TextSection& header = document.sections.emplace_back();
    header.type = "source";
    header.attributes.push_back({"format", TextValue(recordFormatVersion)});
    header.attributes.push_back({"uuid", TextValue(record.id.uuid.toString())});
    header.attributes.push_back({"path", TextValue(record.path)});
    header.attributes.push_back({"importer", TextValue(record.importer)});
    header.attributes.push_back(
        {"version", TextValue(static_cast<std::int64_t>(record.importerVersion))});
    header.attributes.push_back({"settings", TextValue(core::toHex(record.settingsHash))});
    addStamp(header.properties, record.source);
    if (!record.error.empty())
    {
        header.properties.push_back({"error", TextValue(record.error)});
    }

    for (const AssetInfo& artifact : record.artifacts)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "artifact";
        section.attributes.push_back({"uuid", TextValue(artifact.id.uuid.toString())});
        section.attributes.push_back({"type", TextValue(std::string(toString(artifact.type)))});
        section.attributes.push_back({"name", TextValue(artifact.name)});
    }
    for (const Dependency& dependency : record.dependencies)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "dependency";
        section.attributes.push_back({"path", TextValue(dependency.path)});
        addStamp(section.properties, dependency.stamp);
    }
    return serialization::writeText(document);
}

[[nodiscard]] std::optional<std::uint64_t> parseHex(const TextValue* value)
{
    const std::string* const text = value != nullptr ? serialization::asString(*value) : nullptr;
    if (text == nullptr || text->size() != 16)
    {
        return std::nullopt;
    }
    std::uint64_t result = 0;
    const auto [end, status] = std::from_chars(text->data(), text->data() + text->size(), result, 16);
    return status == std::errc() && end == text->data() + text->size() ? std::optional(result)
                                                                      : std::nullopt;
}

[[nodiscard]] std::optional<FileStamp> readStamp(const TextSection& section)
{
    const TextValue* const exists = section.findProperty("exists");
    const TextValue* const size = section.findProperty("size");
    const TextValue* const time = section.findProperty("time");
    const std::optional<bool> existsValue =
        exists != nullptr ? serialization::asBool(*exists) : std::nullopt;
    const std::optional<std::int64_t> sizeValue =
        size != nullptr ? serialization::asInteger(*size) : std::nullopt;
    const std::optional<std::int64_t> timeValue =
        time != nullptr ? serialization::asInteger(*time) : std::nullopt;
    const std::optional<std::uint64_t> hash = parseHex(section.findProperty("hash"));
    if (!existsValue || !sizeValue || !timeValue || !hash)
    {
        return std::nullopt;
    }
    return FileStamp{static_cast<std::uint64_t>(*sizeValue), *timeValue, *hash, *existsValue};
}

[[nodiscard]] const std::string* stringAttribute(const TextSection& section, std::string_view key)
{
    const TextValue* const value = section.findAttribute(key);
    return value != nullptr ? serialization::asString(*value) : nullptr;
}

[[nodiscard]] std::optional<AssetId> idAttribute(const TextSection& section)
{
    const std::string* const text = stringAttribute(section, "uuid");
    const std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    return uuid && !uuid->isNil() ? std::optional(AssetId{*uuid}) : std::nullopt;
}

// Records that cannot be read are ignored: their source is simply imported again.
[[nodiscard]] std::optional<ImportRecord> parseRecord(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document || document->sections.empty() || document->sections.front().type != "source")
    {
        return std::nullopt;
    }

    const TextSection& header = document->sections.front();
    const TextValue* const format = header.findAttribute("format");
    const TextValue* const version = header.findAttribute("version");
    const std::optional<AssetId> id = idAttribute(header);
    const std::string* const path = stringAttribute(header, "path");
    const std::string* const importer = stringAttribute(header, "importer");
    const std::optional<std::uint64_t> settings = parseHex(header.findAttribute("settings"));
    const std::optional<FileStamp> source = readStamp(header);
    if (format == nullptr || serialization::asInteger(*format) != recordFormatVersion ||
        version == nullptr || !serialization::asInteger(*version) || !id || path == nullptr ||
        importer == nullptr || !settings || !source)
    {
        return std::nullopt;
    }

    ImportRecord record;
    record.id = *id;
    record.path = *path;
    record.importer = *importer;
    record.importerVersion = static_cast<std::uint32_t>(*serialization::asInteger(*version));
    record.settingsHash = *settings;
    record.source = *source;
    if (const TextValue* const error = header.findProperty("error"))
    {
        const std::string* const message = serialization::asString(*error);
        record.error = message != nullptr ? *message : "unknown error";
    }

    for (const TextSection& section : document->sections | std::views::drop(1))
    {
        if (section.type == "artifact")
        {
            const std::optional<AssetId> artifactId = idAttribute(section);
            const std::string* const typeName = stringAttribute(section, "type");
            const std::optional<AssetType> type =
                typeName != nullptr ? parseAssetType(*typeName) : std::nullopt;
            const std::string* const name = stringAttribute(section, "name");
            if (!artifactId || !type || name == nullptr)
            {
                return std::nullopt;
            }
            record.artifacts.push_back({*artifactId, *type, *name, record.id});
        }
        else if (section.type == "dependency")
        {
            const std::string* const dependencyPath = stringAttribute(section, "path");
            const std::optional<FileStamp> stamp = readStamp(section);
            if (dependencyPath == nullptr || !stamp)
            {
                return std::nullopt;
            }
            record.dependencies.push_back({*dependencyPath, *stamp});
        }
    }
    return record;
}

[[nodiscard]] std::string lowercaseExtension(const std::filesystem::path& file)
{
    std::string extension = core::toUtf8(file.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

[[nodiscard]] core::Result<void> writeText(const std::filesystem::path& file, std::string_view text)
{
    return core::writeFileAtomically(file, std::as_bytes(std::span(text.data(), text.size())));
}

// Written by the import jobs and read by the database on its thread. The jobs hold it by shared
// pointer, so a destroyed database never leaves them dangling.
struct SharedState
{
    struct Outcome
    {
        AssetId id;
        ImportRecord record;
        std::vector<MetaSubAsset> subAssets;
        bool hasNewSubAssets = false;
        std::chrono::milliseconds duration{0};
    };

    std::atomic<bool> cancelled{false};
    std::atomic<bool> filesChanged{false};
    std::atomic<Clock::rep> lastFileChange{0};

    std::mutex mutex;
    std::condition_variable importsDone;
    std::size_t pending = 0;
    std::deque<Outcome> outcomes;
};

class FolderListener final : public efsw::FileWatchListener
{
public:
    explicit FolderListener(std::shared_ptr<SharedState> shared) noexcept
        : m_shared(std::move(shared))
    {
    }

    void handleFileAction(efsw::WatchID /*watch*/, const std::string& /*directory*/,
                          const std::string& /*filename*/, efsw::Action /*action*/,
                          const std::string& /*oldFilename*/) override
    {
        m_shared->lastFileChange.store(Clock::now().time_since_epoch().count());
        m_shared->filesChanged.store(true);
    }

private:
    std::shared_ptr<SharedState> m_shared;
};

struct ImportJob
{
    AssetId id;
    std::filesystem::path file;
    std::string path;
    const Importer* importer = nullptr;
    MetaFile meta;
    std::filesystem::path importedDirectory;
    std::filesystem::path recordFile;
    std::vector<AssetInfo> previousArtifacts;
    core::JobSystem* jobs = nullptr;
};

[[nodiscard]] core::Result<void> writeArtifacts(const ImportJob& job, const ImportResult& result,
                                                std::vector<AssetInfo>& written)
{
    std::unordered_set<AssetId> seen;
    for (const ImportedArtifact& artifact : result.artifacts)
    {
        if (!artifact.id.isValid() || !seen.insert(artifact.id).second)
        {
            return core::makeError(core::ErrorCode::InvalidState,
                                   "the importer produced a missing or duplicate identifier");
        }
    }
    if (result.artifacts.empty() || result.artifacts.front().id != job.id ||
        result.artifacts.front().type != job.importer->mainType)
    {
        return core::makeError(core::ErrorCode::InvalidState,
                               "the importer did not produce its main asset first");
    }

    for (const ImportedArtifact& artifact : result.artifacts)
    {
        const std::filesystem::path file =
            job.importedDirectory /
            core::pathFromUtf8(artifact.id.uuid.toString() + std::string(artifactExtension));
        if (core::Result<void> saved = core::writeFileAtomically(file, artifact.bytes); !saved)
        {
            return saved;
        }
        written.push_back({artifact.id, artifact.type, artifact.name, job.id});
    }
    return {};
}

void runImport(const ImportJob& job, const std::shared_ptr<SharedState>& shared)
{
    const Clock::time_point start = Clock::now();
    SharedState::Outcome outcome;
    outcome.id = job.id;

    ImportRecord& record = outcome.record;
    record.id = job.id;
    record.path = job.path;
    record.importer = std::string(job.importer->name);
    record.importerVersion = job.importer->version;
    record.settingsHash = importSettingsHash(job.meta);
    // Stamped before reading, so that a change during the import is seen by the next scan.
    record.source = fullStamp(job.file);

    ImportContext context{
        .source = job.file,
        .mainId = job.id,
        .name = core::toUtf8(job.file.stem()),
        .options = job.meta.options,
        .subAssets = SubAssetIds(job.meta.subAssets),
        .jobs = job.jobs,
        .cancelled = &shared->cancelled,
    };

    core::Result<ImportResult> result = job.importer->run(context);
    if (!shared->cancelled.load())
    {
        if (result)
        {
            core::Result<void> written = writeArtifacts(job, *result, record.artifacts);
            if (written)
            {
                for (const std::filesystem::path& dependency : result->dependencies)
                {
                    record.dependencies.push_back(
                        {core::toUtf8(dependency.lexically_normal()), fullStamp(dependency)});
                }
            }
            else
            {
                record.artifacts.clear();
                result = std::unexpected(written.error());
            }
        }
        if (!result)
        {
            record.error = result.error().message.empty() ? std::string(toString(result.error().code))
                                                          : result.error().message;
            record.artifacts = job.previousArtifacts;
        }

        outcome.subAssets = context.subAssets.entries();
        outcome.hasNewSubAssets = result && context.subAssets.hasNewEntries();
        if (core::Result<void> saved = writeText(job.recordFile, writeRecord(record)); !saved)
        {
            DEVEX_LOG_WARNING("Cannot write the import record of {}: {}", job.path, saved.error());
        }
    }
    outcome.duration = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);

    {
        const std::scoped_lock lock(shared->mutex);
        if (!shared->cancelled.load())
        {
            shared->outcomes.push_back(std::move(outcome));
        }
        --shared->pending;
    }
    shared->importsDone.notify_all();
}

} // namespace

std::string_view toString(ImportStatus status) noexcept
{
    switch (status)
    {
    case ImportStatus::Importing:
        return "importing";
    case ImportStatus::Ready:
        return "ready";
    case ImportStatus::Failed:
        return "failed";
    }
    return "unknown";
}

class AssetDatabase::Impl
{
public:
    Impl(const Project& project, core::JobSystem& jobs, const AssetDatabaseConfig& config)
        : m_project(project)
        , m_jobs(jobs)
        , m_config(config)
        , m_shared(std::make_shared<SharedState>())
    {
    }

    ~Impl()
    {
        m_shared->cancelled.store(true);
        m_watcher.reset();
    }

    [[nodiscard]] core::Result<void> initialize()
    {
        for (const std::filesystem::path& directory :
             {m_project.assetsDirectory(), importedDirectory(), recordDirectory()})
        {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
            {
                return core::makeError(core::ErrorCode::Io, "cannot create '{}': {}",
                                       core::toUtf8(directory), error.message());
            }
        }
        loadRecords();
        removeOrphanArtifacts();
        refresh();

        if (m_config.watchFiles)
        {
            m_listener = std::make_unique<FolderListener>(m_shared);
            m_watcher = std::make_unique<efsw::FileWatcher>();
            const efsw::WatchID watch =
                m_watcher->addWatch(core::toUtf8(m_project.assetsDirectory()), m_listener.get(), true);
            if (watch < 0)
            {
                DEVEX_LOG_WARNING("Cannot watch '{}': {}; changes are seen on restart only",
                                  core::toUtf8(m_project.assetsDirectory()),
                                  efsw::Errors::Log::getLastErrorLog());
                m_watcher.reset();
            }
            else
            {
                m_watcher->watch();
            }
        }
        return {};
    }

    [[nodiscard]] const Project& project() const noexcept
    {
        return m_project;
    }

    void refresh()
    {
        for (auto& [id, source] : m_sources)
        {
            source.seen = false;
        }

        std::error_code error;
        auto iterator = std::filesystem::recursive_directory_iterator(
            m_project.assetsDirectory(), std::filesystem::directory_options::skip_permission_denied,
            error);
        if (error)
        {
            DEVEX_LOG_ERROR("Cannot scan '{}': {}", core::toUtf8(m_project.assetsDirectory()),
                            error.message());
            return;
        }

        std::vector<std::filesystem::path> files;
        for (auto end = std::filesystem::recursive_directory_iterator(); iterator != end;
             iterator.increment(error))
        {
            if (error)
            {
                break;
            }
            const std::filesystem::path& file = iterator->path();
            // Hidden files and folders, such as editor backups, are not assets.
            if (core::toUtf8(file.filename()).starts_with('.'))
            {
                if (iterator->is_directory(error))
                {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (iterator->is_regular_file(error) && findImporterForExtension(lowercaseExtension(file)))
            {
                files.push_back(file.lexically_normal());
            }
        }
        // A stable order decides which of two files sharing an identifier keeps it.
        std::ranges::sort(files);

        for (const std::filesystem::path& file : files)
        {
            scanFile(file);
        }

        std::vector<AssetId> deleted;
        for (const auto& [id, source] : m_sources)
        {
            if (!source.seen)
            {
                deleted.push_back(id);
            }
        }
        for (const AssetId id : deleted)
        {
            removeSource(id);
        }
    }

    [[nodiscard]] std::vector<AssetEvent> update()
    {
        const auto settleTime = std::chrono::duration_cast<Clock::duration>(m_config.settleTime);
        if (m_shared->filesChanged.load() &&
            Clock::now().time_since_epoch().count() - m_shared->lastFileChange.load() >=
                settleTime.count())
        {
            // Cleared before scanning: a change during the scan triggers another one.
            m_shared->filesChanged.store(false);
            refresh();
        }

        std::deque<SharedState::Outcome> outcomes;
        {
            const std::scoped_lock lock(m_shared->mutex);
            outcomes.swap(m_shared->outcomes);
        }
        for (SharedState::Outcome& outcome : outcomes)
        {
            applyOutcome(std::move(outcome));
        }
        return std::exchange(m_events, {});
    }

    void waitForImports()
    {
        std::unique_lock lock(m_shared->mutex);
        m_shared->importsDone.wait(lock, [this] { return m_shared->pending == 0; });
    }

    [[nodiscard]] std::size_t pendingImports() const
    {
        const std::scoped_lock lock(m_shared->mutex);
        return m_shared->pending;
    }

    [[nodiscard]] core::Result<void> reimport(AssetId id)
    {
        const AssetInfo* const info = find(id);
        const auto found = m_sources.find(info != nullptr ? info->source : id);
        if (found == m_sources.end())
        {
            return core::makeError(core::ErrorCode::NotFound, "no source file produces asset {}",
                                   id.uuid);
        }
        Source& source = found->second;
        source.forced = true;
        if (source.importing)
        {
            source.changedWhileImporting = true;
        }
        else
        {
            queueImport(source);
        }
        return {};
    }

    [[nodiscard]] const AssetInfo* find(AssetId id) const
    {
        const auto found = m_assets.find(id);
        return found != m_assets.end() ? &found->second : nullptr;
    }

    [[nodiscard]] std::vector<AssetInfo> assets(std::optional<AssetType> type) const
    {
        std::vector<AssetInfo> result;
        for (const auto& [id, info] : m_assets)
        {
            if (!type || info.type == *type)
            {
                result.push_back(info);
            }
        }
        std::ranges::sort(result, [](const AssetInfo& left, const AssetInfo& right) {
            return std::tie(left.name, left.id) < std::tie(right.name, right.id);
        });
        return result;
    }

    [[nodiscard]] std::vector<SourceFile> sources() const
    {
        std::vector<SourceFile> result;
        for (const auto& [id, source] : m_sources)
        {
            result.push_back(describe(source));
        }
        std::ranges::sort(result, [](const SourceFile& left, const SourceFile& right) {
            return left.path < right.path;
        });
        return result;
    }

    [[nodiscard]] std::optional<SourceFile> sourceOf(AssetId id) const
    {
        const AssetInfo* const info = find(id);
        const auto found = m_sources.find(info != nullptr ? info->source : id);
        return found != m_sources.end() ? std::optional(describe(found->second)) : std::nullopt;
    }

    [[nodiscard]] std::optional<AssetId> findByPath(std::string_view resourcePath) const
    {
        for (const auto& [id, source] : m_sources)
        {
            if (source.path == resourcePath)
            {
                return id;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::filesystem::path artifactPath(AssetId id) const
    {
        return importedDirectory() /
               core::pathFromUtf8(id.uuid.toString() + std::string(artifactExtension));
    }

    [[nodiscard]] core::Result<std::vector<std::byte>> loadArtifact(AssetId id) const
    {
        if (find(id) == nullptr)
        {
            return core::makeError(core::ErrorCode::NotFound, "asset {} is not imported", id.uuid);
        }
        return core::readBinaryFile(artifactPath(id));
    }

    [[nodiscard]] core::Result<AssetId> addFile(const std::filesystem::path& file,
                                                std::string_view folder)
    {
        const std::optional<std::filesystem::path> directory = m_project.absolutePath(folder);
        if (!directory || m_project.resourcePath(*directory).find("res://assets") != 0)
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "'{}' is not a folder of the project assets", folder);
        }
        const Importer* const importer = findImporterForExtension(lowercaseExtension(file));
        if (importer == nullptr)
        {
            return core::makeError(core::ErrorCode::Unsupported, "no importer handles '{}'",
                                   core::toUtf8(file.filename()));
        }

        std::vector<std::filesystem::path> dependencies;
        if (importer->name == "gltf")
        {
            core::Result<std::vector<std::filesystem::path>> found = findGltfDependencies(file);
            if (!found)
            {
                return std::unexpected(found.error());
            }
            dependencies = std::move(*found);
        }

        std::error_code error;
        std::filesystem::create_directories(*directory, error);
        std::filesystem::path destination = *directory / file.filename();
        for (int copy = 2; std::filesystem::exists(destination, error); ++copy)
        {
            destination = *directory / core::pathFromUtf8(std::format(
                                           "{} {}{}", core::toUtf8(file.stem()), copy,
                                           core::toUtf8(file.extension())));
        }
        std::filesystem::copy_file(file, destination, error);
        if (error)
        {
            return core::makeError(core::ErrorCode::Io, "cannot copy '{}': {}",
                                   core::toUtf8(file), error.message());
        }

        for (const std::filesystem::path& dependency : dependencies)
        {
            const std::filesystem::path relative =
                dependency.lexically_relative(file.parent_path());
            const std::filesystem::path target = (*directory / relative).lexically_normal();
            if (relative.empty() || *relative.begin() == ".." ||
                m_project.resourcePath(target).find("res://assets") != 0)
            {
                DEVEX_LOG_WARNING("'{}' refers to '{}' outside its folder, which is not copied",
                                  core::toUtf8(file.filename()), core::toUtf8(dependency));
                continue;
            }
            std::filesystem::create_directories(target.parent_path(), error);
            if (!std::filesystem::exists(target, error))
            {
                std::filesystem::copy_file(dependency, target, error);
                if (error)
                {
                    DEVEX_LOG_WARNING("Cannot copy '{}': {}", core::toUtf8(dependency),
                                      error.message());
                }
            }
        }

        core::Result<MetaFile> meta = readOrCreateMeta(destination, *importer);
        if (!meta)
        {
            return std::unexpected(meta.error());
        }
        refresh();
        return meta->id;
    }

private:
    struct Source
    {
        AssetId id;
        std::filesystem::path file;
        std::string path;
        const Importer* importer = nullptr;
        MetaFile meta;
        std::optional<ImportRecord> record;
        bool seen = false;
        bool importing = false;
        bool changedWhileImporting = false;
        bool forced = false;
    };

    [[nodiscard]] std::filesystem::path importedDirectory() const
    {
        return m_project.cacheDirectory() / "imported";
    }

    [[nodiscard]] std::filesystem::path recordDirectory() const
    {
        return m_project.cacheDirectory() / "sources";
    }

    [[nodiscard]] std::filesystem::path recordFile(AssetId id) const
    {
        return recordDirectory() / core::pathFromUtf8(id.uuid.toString() + std::string(recordExtension));
    }

    [[nodiscard]] static std::filesystem::path metaFileOf(const std::filesystem::path& file)
    {
        std::filesystem::path meta = file;
        meta += std::filesystem::path(metaExtension);
        return meta;
    }

    void loadRecords()
    {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(recordDirectory(), error))
        {
            if (lowercaseExtension(entry.path()) != recordExtension)
            {
                continue;
            }
            const core::Result<std::string> text = core::readTextFile(entry.path());
            std::optional<ImportRecord> record = text ? parseRecord(*text) : std::nullopt;
            if (record)
            {
                m_records.insert_or_assign(record->id, std::move(*record));
            }
        }
    }

    // Deletes cooked files that no import record refers to, left by removed sources or by an
    // interrupted session.
    void removeOrphanArtifacts()
    {
        std::unordered_set<std::string> referenced;
        for (const auto& [id, record] : m_records)
        {
            for (const AssetInfo& artifact : record.artifacts)
            {
                referenced.insert(core::toUtf8(artifactPath(artifact.id).filename()));
            }
        }
        std::error_code error;
        std::vector<std::filesystem::path> orphans;
        for (const auto& entry : std::filesystem::directory_iterator(importedDirectory(), error))
        {
            if (!referenced.contains(core::toUtf8(entry.path().filename())))
            {
                orphans.push_back(entry.path());
            }
        }
        for (const std::filesystem::path& orphan : orphans)
        {
            std::filesystem::remove(orphan, error);
        }
    }

    [[nodiscard]] core::Result<MetaFile> readOrCreateMeta(const std::filesystem::path& file,
                                                          const Importer& importer)
    {
        const std::filesystem::path metaFile = metaFileOf(file);
        std::error_code error;
        if (std::filesystem::exists(metaFile, error))
        {
            const core::Result<std::string> text = core::readTextFile(metaFile);
            if (!text)
            {
                return std::unexpected(text.error());
            }
            return parseMetaFile(*text);
        }

        MetaFile meta{
            .id = AssetId::generate(),
            .importer = std::string(importer.name),
            .options = importer.defaultOptions,
        };
        if (core::Result<void> written = writeText(metaFile, writeMetaFile(meta)); !written)
        {
            return std::unexpected(written.error());
        }
        return meta;
    }

    void scanFile(const std::filesystem::path& file)
    {
        const Importer* const importer = findImporterForExtension(lowercaseExtension(file));
        const std::string path = m_project.resourcePath(file);
        core::Result<MetaFile> meta = readOrCreateMeta(file, *importer);
        if (!meta)
        {
            // A broken .dvxmeta is left for the user to fix rather than replaced, which would lose
            // the identifiers that references rely on.
            if (m_reportedMetaErrors.insert(path).second)
            {
                DEVEX_LOG_ERROR("Cannot read the .dvxmeta of {}: {}", path, meta.error());
            }
            return;
        }
        m_reportedMetaErrors.erase(path);

        if (const auto existing = m_sources.find(meta->id);
            existing != m_sources.end() && existing->second.seen && existing->second.file != file)
        {
            // A file copied with its .dvxmeta: the copy receives new identifiers.
            DEVEX_LOG_WARNING("{} has the identifiers of {}; it receives new ones", path,
                              existing->second.path);
            meta->id = AssetId::generate();
            meta->subAssets.clear();
            if (core::Result<void> written = writeText(metaFileOf(file), writeMetaFile(*meta));
                !written)
            {
                DEVEX_LOG_ERROR("Cannot update the .dvxmeta of {}: {}", path, written.error());
                return;
            }
        }

        auto [iterator, inserted] = m_sources.try_emplace(meta->id);
        Source& source = iterator->second;
        if (inserted)
        {
            source.id = meta->id;
            if (auto record = m_records.extract(meta->id))
            {
                source.record = std::move(record.mapped());
                publishArtifacts(source, {});
            }
        }
        source.seen = true;
        source.file = file;
        source.importer = importer;
        source.meta = std::move(*meta);
        if (source.path != path)
        {
            source.path = path;
            // A source moved with its .dvxmeta keeps its assets without being imported again.
            if (source.record && source.record->path != path && !source.importing)
            {
                source.record->path = path;
                static_cast<void>(writeText(recordFile(source.id), writeRecord(*source.record)));
            }
        }

        if (source.importing)
        {
            source.changedWhileImporting = true;
        }
        else if (needsImport(source))
        {
            queueImport(source);
        }
    }

    [[nodiscard]] bool needsImport(Source& source)
    {
        if (source.forced || !source.record)
        {
            return true;
        }
        ImportRecord& record = *source.record;
        if (record.importer != source.importer->name ||
            record.importerVersion != source.importer->version ||
            record.settingsHash != importSettingsHash(source.meta))
        {
            return true;
        }

        const std::uint64_t previousTime = static_cast<std::uint64_t>(record.source.time);
        bool changed = !isUnchanged(source.file, record.source);
        for (Dependency& dependency : record.dependencies)
        {
            changed = changed || !isUnchanged(core::pathFromUtf8(dependency.path), dependency.stamp);
        }
        if (!changed && static_cast<std::uint64_t>(record.source.time) != previousTime)
        {
            static_cast<void>(writeText(recordFile(source.id), writeRecord(record)));
        }
        if (changed)
        {
            return true;
        }

        // Failed imports are retried only once something changes.
        if (!record.error.empty())
        {
            return false;
        }
        std::error_code error;
        return std::ranges::any_of(record.artifacts, [&](const AssetInfo& artifact) {
            return !std::filesystem::exists(artifactPath(artifact.id), error);
        });
    }

    void queueImport(Source& source)
    {
        source.importing = true;
        source.forced = false;
        source.changedWhileImporting = false;

        ImportJob job{
            .id = source.id,
            .file = source.file,
            .path = source.path,
            .importer = source.importer,
            .meta = source.meta,
            .importedDirectory = importedDirectory(),
            .recordFile = recordFile(source.id),
            .previousArtifacts = source.record ? source.record->artifacts : std::vector<AssetInfo>{},
            .jobs = &m_jobs,
        };
        {
            const std::scoped_lock lock(m_shared->mutex);
            ++m_shared->pending;
        }
        m_jobs.schedule([job = std::move(job), shared = m_shared] { runImport(job, shared); });
    }

    // Makes the artifacts of the source's record findable, reporting what changed compared with
    // the previous artifacts.
    void publishArtifacts(const Source& source, const std::vector<AssetInfo>& previous)
    {
        const std::vector<AssetInfo>& current = source.record->artifacts;
        for (const AssetInfo& old : previous)
        {
            const bool kept = std::ranges::any_of(
                current, [&](const AssetInfo& artifact) { return artifact.id == old.id; });
            if (!kept)
            {
                m_assets.erase(old.id);
                std::error_code error;
                std::filesystem::remove(artifactPath(old.id), error);
                m_events.push_back({old.id, old.type, AssetChange::Removed});
            }
        }
        for (const AssetInfo& artifact : current)
        {
            m_assets.insert_or_assign(artifact.id, artifact);
        }
    }

    void applyOutcome(SharedState::Outcome outcome)
    {
        const auto found = m_sources.find(outcome.id);
        if (found == m_sources.end())
        {
            // The source was deleted during its import.
            removeFiles(outcome.record);
            return;
        }

        Source& source = found->second;
        source.importing = false;
        const std::vector<AssetInfo> previous =
            source.record ? source.record->artifacts : std::vector<AssetInfo>{};
        const bool failed = !outcome.record.error.empty();
        source.record = std::move(outcome.record);

        if (failed)
        {
            DEVEX_LOG_ERROR("Cannot import {}: {}", source.path, source.record->error);
        }
        else
        {
            publishArtifacts(source, previous);
            for (const AssetInfo& artifact : source.record->artifacts)
            {
                m_events.push_back({artifact.id, artifact.type, AssetChange::Imported});
            }
            DEVEX_LOG_INFO("Imported {} ({} asset{}, {} ms)", source.path,
                           source.record->artifacts.size(),
                           source.record->artifacts.size() == 1 ? "" : "s",
                           outcome.duration.count());
        }

        if (outcome.hasNewSubAssets)
        {
            source.meta.subAssets = std::move(outcome.subAssets);
            if (core::Result<void> written =
                    writeText(metaFileOf(source.file), writeMetaFile(source.meta));
                !written)
            {
                DEVEX_LOG_ERROR("Cannot update the .dvxmeta of {}: {}", source.path,
                                written.error());
            }
        }

        if (source.changedWhileImporting && (source.forced || needsImport(source)))
        {
            queueImport(source);
        }
        source.changedWhileImporting = false;
    }

    void removeFiles(const ImportRecord& record)
    {
        std::error_code error;
        for (const AssetInfo& artifact : record.artifacts)
        {
            std::filesystem::remove(artifactPath(artifact.id), error);
        }
        std::filesystem::remove(recordFile(record.id), error);
    }

    void removeSource(AssetId id)
    {
        const auto found = m_sources.find(id);
        Source& source = found->second;
        DEVEX_LOG_INFO("Removed {}", source.path);
        if (source.record)
        {
            for (const AssetInfo& artifact : source.record->artifacts)
            {
                m_assets.erase(artifact.id);
                m_events.push_back({artifact.id, artifact.type, AssetChange::Removed});
            }
            removeFiles(*source.record);
        }
        // An import still running finds no source when it completes and removes its own files.
        m_sources.erase(found);
    }

    [[nodiscard]] SourceFile describe(const Source& source) const
    {
        SourceFile file{
            .id = source.id,
            .path = source.path,
            .importer = std::string(source.importer->name),
        };
        if (source.importing || !source.record)
        {
            file.status = ImportStatus::Importing;
        }
        else if (!source.record->error.empty())
        {
            file.status = ImportStatus::Failed;
            file.error = source.record->error;
        }
        else
        {
            file.status = ImportStatus::Ready;
        }
        if (source.record)
        {
            for (const AssetInfo& artifact : source.record->artifacts)
            {
                file.assets.push_back(artifact.id);
            }
        }
        return file;
    }

    Project m_project;
    core::JobSystem& m_jobs;
    AssetDatabaseConfig m_config;
    std::shared_ptr<SharedState> m_shared;
    std::unordered_map<AssetId, Source> m_sources;
    // Records read at startup whose source has not been scanned yet.
    std::unordered_map<AssetId, ImportRecord> m_records;
    std::unordered_map<AssetId, AssetInfo> m_assets;
    std::vector<AssetEvent> m_events;
    std::unordered_set<std::string> m_reportedMetaErrors;
    std::unique_ptr<FolderListener> m_listener;
    // Declared last: destroyed first, so no event arrives during destruction.
    std::unique_ptr<efsw::FileWatcher> m_watcher;
};

core::Result<std::unique_ptr<AssetDatabase>> AssetDatabase::open(const Project& project,
                                                                 core::JobSystem& jobs,
                                                                 const AssetDatabaseConfig& config)
{
    auto impl = std::make_unique<Impl>(project, jobs, config);
    if (core::Result<void> initialized = impl->initialize(); !initialized)
    {
        return std::unexpected(initialized.error());
    }
    return std::unique_ptr<AssetDatabase>(new AssetDatabase(std::move(impl)));
}

AssetDatabase::AssetDatabase(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

AssetDatabase::~AssetDatabase() = default;

const Project& AssetDatabase::project() const noexcept
{
    return m_impl->project();
}

void AssetDatabase::refresh()
{
    m_impl->refresh();
}

std::vector<AssetEvent> AssetDatabase::update()
{
    return m_impl->update();
}

void AssetDatabase::waitForImports()
{
    m_impl->waitForImports();
}

std::size_t AssetDatabase::pendingImports() const noexcept
{
    return m_impl->pendingImports();
}

core::Result<void> AssetDatabase::reimport(AssetId id)
{
    return m_impl->reimport(id);
}

const AssetInfo* AssetDatabase::find(AssetId id) const
{
    return m_impl->find(id);
}

std::vector<AssetInfo> AssetDatabase::assets(std::optional<AssetType> type) const
{
    return m_impl->assets(type);
}

std::vector<SourceFile> AssetDatabase::sources() const
{
    return m_impl->sources();
}

std::optional<SourceFile> AssetDatabase::sourceOf(AssetId id) const
{
    return m_impl->sourceOf(id);
}

std::optional<AssetId> AssetDatabase::findByPath(std::string_view resourcePath) const
{
    return m_impl->findByPath(resourcePath);
}

std::filesystem::path AssetDatabase::artifactPath(AssetId id) const
{
    return m_impl->artifactPath(id);
}

core::Result<std::vector<std::byte>> AssetDatabase::loadArtifact(AssetId id) const
{
    return m_impl->loadArtifact(id);
}

core::Result<AssetId> AssetDatabase::addFile(const std::filesystem::path& file,
                                             std::string_view folder)
{
    return m_impl->addFile(file, folder);
}

} // namespace devex::asset
