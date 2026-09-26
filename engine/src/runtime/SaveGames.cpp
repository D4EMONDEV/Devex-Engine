#include <devex/runtime/SaveGames.hpp>

#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <span>
#include <system_error>
#include <utility>

namespace devex::runtime {
namespace {

constexpr std::string_view saveExtension = ".dvxsave";
constexpr std::string_view backupExtension = ".bak";
// The scene of a save follows this line, as its own file would hold it.
constexpr std::string_view sceneMarker = "[saved_scene]\n";
constexpr std::int64_t saveFormat = 1;

struct ParsedSave
{
    SaveSlot slot;
    serialization::TextDocument document;
    std::string sceneText;
};

[[nodiscard]] const std::string* stringAttribute(const serialization::TextSection& section, std::string_view key)
{
    const serialization::TextValue* const value = section.findAttribute(key);
    return value != nullptr ? serialization::asString(*value) : nullptr;
}

[[nodiscard]] core::Result<ParsedSave> parseSave(std::string_view text, std::string_view slot)
{
    // The header and the data, then the scene as its own file would hold it.
    std::string_view head = text;
    std::string_view sceneText;
    if (const std::size_t marker = text.find(std::string("\n").append(sceneMarker)); marker != std::string_view::npos)
    {
        head = text.substr(0, marker + 1);
        sceneText = text.substr(marker + 1 + sceneMarker.size());
    }
    core::Result<serialization::TextDocument> document = serialization::parseText(head);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "save")
    {
        return core::makeError(core::ErrorCode::Parse, "the file does not start with [save]");
    }
    const serialization::TextSection& header = document->sections.front();
    const serialization::TextValue* const format = header.findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format).value_or(saveFormat + 1) > saveFormat)
    {
        return core::makeError(core::ErrorCode::Unsupported, "the save needs a newer version of the game");
    }

    ParsedSave parsed{.sceneText = std::string(sceneText)};
    SaveSlot& info = parsed.slot;
    info.name = std::string(slot);
    info.hasScene = !parsed.sceneText.empty();
    if (const std::string* const label = stringAttribute(header, "label"))
    {
        info.label = *label;
    }
    if (const std::string* const type = stringAttribute(header, "type"))
    {
        info.type = *type;
    }
    if (const std::string* const sceneName = stringAttribute(header, "scene_name"))
    {
        info.sceneName = *sceneName;
    }
    if (const std::string* const scene = stringAttribute(header, "scene"))
    {
        info.scene = asset::AssetId{core::Uuid::parse(*scene).value_or(core::Uuid{})};
    }
    if (const serialization::TextValue* const time = header.findAttribute("time"))
    {
        info.time = serialization::asInteger(*time).value_or(0);
    }
    if (const serialization::TextValue* const played = header.findAttribute("play_time"))
    {
        info.playTime = std::max(0.0, serialization::asNumber(*played).value_or(0.0));
    }
    if (const serialization::TextValue* const version = header.findAttribute("version"))
    {
        info.version = static_cast<std::uint32_t>(std::max<std::int64_t>(0, serialization::asInteger(*version).value_or(0)));
    }
    parsed.document = std::move(*document);
    return parsed;
}

// The save of the slot, or the one it kept when the file is damaged.
[[nodiscard]] core::Result<ParsedSave> readSave(const std::filesystem::path& file, std::string_view slot)
{
    std::error_code error;
    if (!std::filesystem::exists(file, error))
    {
        return core::makeError(core::ErrorCode::NotFound, "there is no save '{}'", slot);
    }
    const core::Result<std::string> text = core::readTextFile(file);
    core::Result<ParsedSave> parsed = text ? parseSave(*text, slot) : core::Result<ParsedSave>(std::unexpected(text.error()));
    if (parsed)
    {
        return parsed;
    }
    std::filesystem::path backup = file;
    backup += backupExtension;
    const core::Result<std::string> previous = core::readTextFile(backup);
    core::Result<ParsedSave> fallback = previous ? parseSave(*previous, slot) : core::Result<ParsedSave>(std::unexpected(previous.error()));
    if (fallback)
    {
        DEVEX_LOG_WARNING("Save '{}' is damaged ({}): the previous one is loaded instead", slot, parsed.error());
        return fallback;
    }
    return parsed;
}

} // namespace

SaveGames::SaveGames(std::filesystem::path directory)
    : m_directory(std::move(directory))
{
}

const std::filesystem::path& SaveGames::directory() const noexcept
{
    return m_directory;
}

void SaveGames::setScene(const scene::Scene* scene, asset::AssetId asset, std::string name)
{
    m_scene = scene;
    m_sceneAsset = asset;
    m_sceneName = std::move(name);
}

void SaveGames::setAssets(AssetManager* assets) noexcept
{
    m_assets = assets;
}

void SaveGames::addPlayTime(double seconds) noexcept
{
    m_playTime += std::max(seconds, 0.0);
}

double SaveGames::playTime() const noexcept
{
    return m_playTime;
}

bool SaveGames::isValidSlot(std::string_view slot) noexcept
{
    if (slot.empty() || slot.size() > 64 || slot.front() == ' ' || slot.back() == ' ' || slot.back() == '.')
    {
        return false;
    }
    return std::ranges::all_of(slot, [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return std::isalnum(code) != 0 || character == ' ' || character == '-' || character == '_' || character == '.';
    });
}

std::filesystem::path SaveGames::fileOf(std::string_view slot) const
{
    return m_directory / core::pathFromUtf8(std::string(slot) + std::string(saveExtension));
}

core::Result<void> SaveGames::save(std::string_view slot, const reflection::TypeInfo& type, const void* data,
                                   const SaveOptions& options)
{
    if (!isValidSlot(slot))
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "'{}' is no save slot: letters, digits, spaces, dashes, dots and underscores", slot);
    }
    if (m_directory.empty())
    {
        return core::makeError(core::ErrorCode::Unsupported, "this game has no folder to keep saves in");
    }

    serialization::TextDocument document;
    serialization::TextSection& header = document.sections.emplace_back();
    header.type = "save";
    header.attributes.push_back({"format", serialization::TextValue(saveFormat)});
    header.attributes.push_back({"type", serialization::TextValue(type.name)});
    header.attributes.push_back({"version", serialization::TextValue(static_cast<std::int64_t>(options.version))});
    if (!options.label.empty())
    {
        header.attributes.push_back({"label", serialization::TextValue(options.label)});
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    header.attributes.push_back({"time", serialization::TextValue(static_cast<std::int64_t>(now.count()))});
    header.attributes.push_back({"play_time", serialization::TextValue(m_playTime)});
    if (m_sceneAsset.isValid())
    {
        header.attributes.push_back({"scene", serialization::TextValue(m_sceneAsset.uuid.toString())});
    }
    if (!m_sceneName.empty())
    {
        header.attributes.push_back({"scene_name", serialization::TextValue(m_sceneName)});
    }
    serialization::TextSection& fields = document.sections.emplace_back();
    fields.type = "data";
    for (const reflection::FieldInfo& field : type.fields)
    {
        fields.properties.push_back({field.name, scene::writeFieldValue(field, field.address(data))});
    }
    std::string text = serialization::writeText(document);
    if (options.scene && m_scene != nullptr)
    {
        if (!text.ends_with('\n'))
        {
            text.push_back('\n');
        }
        text.append(sceneMarker);
        text.append(scene::saveScene(*m_scene));
    }

    // The previous save stays beside the new one, which a damaged file falls back on.
    const std::filesystem::path file = fileOf(slot);
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    if (std::filesystem::exists(file, error))
    {
        std::filesystem::path backup = file;
        backup += backupExtension;
        std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);
    }
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
    if (core::Result<void> written = core::writeFileAtomically(file, bytes); !written)
    {
        return written;
    }

    std::filesystem::path picture = file;
    picture.replace_extension(".png");
    if (options.thumbnail)
    {
        m_thumbnailRequests.push_back(std::move(picture));
    }
    else
    {
        std::filesystem::remove(picture, error);
    }
    DEVEX_LOG_INFO("Saved '{}'", slot);
    return {};
}

core::Result<SaveSlot> SaveGames::load(std::string_view slot, const reflection::TypeInfo& type, void* data, bool restoreScene)
{
    if (!isValidSlot(slot))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "'{}' is no save slot", slot);
    }
    core::Result<ParsedSave> parsed = readSave(fileOf(slot), slot);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }
    if (!parsed->slot.type.empty() && parsed->slot.type != type.name)
    {
        DEVEX_LOG_WARNING("Save '{}' holds a {}, read as a {}", slot, parsed->slot.type, type.name);
    }
    for (const serialization::TextSection& section : parsed->document.sections)
    {
        if (section.type != "data")
        {
            continue;
        }
        for (const serialization::TextProperty& property : section.properties)
        {
            // What the data no longer has is skipped; what it has more keeps its default.
            const reflection::FieldInfo* const field = type.findField(property.key);
            if (field == nullptr)
            {
                continue;
            }
            if (core::Result<void> read = scene::readFieldValue(*field, property.value, field->address(data)); !read)
            {
                DEVEX_LOG_WARNING("Save '{}': {} is not read: {}", slot, property.key, read.error());
            }
        }
    }

    SaveSlot info = std::move(parsed->slot);
    if (restoreScene && info.hasScene)
    {
        m_restore = Restore{.slot = std::string(slot), .sceneText = std::move(parsed->sceneText), .scene = info.scene};
        m_playTime = info.playTime;
    }
    std::filesystem::path picture = fileOf(slot);
    picture.replace_extension(".png");
    std::error_code error;
    if (std::filesystem::exists(picture, error))
    {
        info.thumbnail = picture;
    }
    return info;
}

std::vector<SaveSlot> SaveGames::slots() const
{
    std::vector<SaveSlot> found;
    std::error_code error;
    if (m_directory.empty() || !std::filesystem::is_directory(m_directory, error))
    {
        return found;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_directory, error))
    {
        if (entry.path().extension() == saveExtension)
        {
            if (std::optional<SaveSlot> slot = find(core::toUtf8(entry.path().stem())))
            {
                found.push_back(std::move(*slot));
            }
        }
    }
    std::ranges::sort(found, [](const SaveSlot& first, const SaveSlot& second) {
        return first.time != second.time ? first.time > second.time : first.name < second.name;
    });
    return found;
}

std::optional<SaveSlot> SaveGames::find(std::string_view slot) const
{
    if (!isValidSlot(slot) || m_directory.empty())
    {
        return std::nullopt;
    }
    core::Result<ParsedSave> parsed = readSave(fileOf(slot), slot);
    if (!parsed)
    {
        return std::nullopt;
    }
    std::filesystem::path picture = fileOf(slot);
    picture.replace_extension(".png");
    std::error_code error;
    if (std::filesystem::exists(picture, error))
    {
        parsed->slot.thumbnail = picture;
    }
    return std::move(parsed->slot);
}

bool SaveGames::exists(std::string_view slot) const
{
    std::error_code error;
    return isValidSlot(slot) && !m_directory.empty() && std::filesystem::exists(fileOf(slot), error);
}

core::Result<void> SaveGames::remove(std::string_view slot)
{
    if (!isValidSlot(slot))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "'{}' is no save slot", slot);
    }
    const std::filesystem::path file = fileOf(slot);
    std::filesystem::path backup = file;
    backup += backupExtension;
    std::filesystem::path picture = file;
    picture.replace_extension(".png");
    std::error_code error;
    for (const std::filesystem::path& path : {file, backup, picture})
    {
        std::filesystem::remove(path, error);
        if (error)
        {
            return core::makeError(core::ErrorCode::Io, "cannot remove '{}': {}", core::toUtf8(path), error.message());
        }
    }
    m_thumbnails.erase(std::string(slot));
    return {};
}

asset::AssetId SaveGames::thumbnail(std::string_view slot)
{
    if (m_assets == nullptr || !isValidSlot(slot))
    {
        return {};
    }
    std::filesystem::path picture = fileOf(slot);
    picture.replace_extension(".png");
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(picture, error);
    if (error)
    {
        return {};
    }
    const auto known = m_thumbnails.find(slot);
    if (known != m_thumbnails.end() && known->second.written == written)
    {
        return known->second.texture;
    }

    const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(picture);
    const core::Result<asset::Image> image =
        bytes ? asset::decodeImage(*bytes) : core::Result<asset::Image>(std::unexpected(bytes.error()));
    core::Result<asset::TextureData> texture =
        image ? asset::buildTexture(*image, {.srgb = true, .mipmaps = false, .compress = false})
              : core::Result<asset::TextureData>(std::unexpected(image.error()));
    if (!texture)
    {
        DEVEX_LOG_WARNING("The picture of save '{}' cannot be read: {}", slot, texture.error());
        return {};
    }
    // A texture of its own for each version of the picture, so that interfaces showing the
    // previous one see the change.
    const asset::AssetId id{core::Uuid::generate()};
    if (core::Result<void> made = m_assets->setTexture(id, *texture); !made)
    {
        DEVEX_LOG_WARNING("The picture of save '{}' cannot be shown: {}", slot, made.error());
        return {};
    }
    m_thumbnails.insert_or_assign(std::string(slot), Thumbnail{id, written});
    return id;
}

const std::string& SaveGames::restoredSlot() const noexcept
{
    return m_restoredSlot;
}

std::optional<SaveGames::Restore> SaveGames::takeRestore()
{
    return std::exchange(m_restore, std::nullopt);
}

void SaveGames::setRestoredSlot(std::string slot)
{
    m_restoredSlot = std::move(slot);
}

std::vector<std::filesystem::path> SaveGames::takeThumbnailRequests()
{
    return std::exchange(m_thumbnailRequests, {});
}

} // namespace devex::runtime
