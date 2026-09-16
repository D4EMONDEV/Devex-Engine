#include <devex/asset/import/MetaFile.hpp>
#include <devex/core/Hash.hpp>

#include <format>
#include <optional>

namespace devex::asset {
namespace {

using serialization::TextSection;
using serialization::TextValue;

constexpr std::int64_t metaFormatVersion = 1;

[[nodiscard]] const std::string* stringAttribute(const TextSection& section, std::string_view key)
{
    const TextValue* const value = section.findAttribute(key);
    return value != nullptr ? serialization::asString(*value) : nullptr;
}

[[nodiscard]] std::optional<AssetId> idAttribute(const TextSection& section)
{
    const std::string* const text = stringAttribute(section, "uuid");
    std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    if (!uuid || uuid->isNil())
    {
        return std::nullopt;
    }
    return AssetId{*uuid};
}

} // namespace

core::Result<MetaFile> parseMetaFile(std::string_view text)
{
    core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "asset")
    {
        return core::makeError(core::ErrorCode::Parse, "the file does not start with [asset]");
    }

    TextSection& header = document->sections.front();
    const TextValue* const format = header.findAttribute("format");
    const std::optional<std::int64_t> version =
        format != nullptr ? serialization::asInteger(*format) : std::nullopt;
    if (!version || *version > metaFormatVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "line {}: unknown .dvxmeta format, a newer Devex may be needed",
                               header.line);
    }

    MetaFile meta;
    std::optional<AssetId> id = idAttribute(header);
    if (!id)
    {
        return core::makeError(core::ErrorCode::Parse, "line {}: [asset] needs a valid uuid",
                               header.line);
    }
    meta.id = *id;
    const std::string* const importer = stringAttribute(header, "importer");
    meta.importer = importer != nullptr ? *importer : std::string();
    meta.options = std::move(header.properties);

    for (std::size_t index = 1; index < document->sections.size(); ++index)
    {
        const TextSection& section = document->sections[index];
        if (section.type != "subasset")
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: unexpected [{}] section",
                                   section.line, section.type);
        }
        const std::string* const typeName = stringAttribute(section, "type");
        const std::optional<AssetType> type =
            typeName != nullptr ? parseAssetType(*typeName) : std::nullopt;
        const std::string* const key = stringAttribute(section, "key");
        const std::optional<AssetId> subId = idAttribute(section);
        if (!type || key == nullptr || !subId)
        {
            return core::makeError(core::ErrorCode::Parse,
                                   "line {}: [subasset] needs a type, a key and a valid uuid",
                                   section.line);
        }
        meta.subAssets.push_back({*type, *key, *subId});
    }
    return meta;
}

std::string writeMetaFile(const MetaFile& meta)
{
    serialization::TextDocument document;
    TextSection& header = document.sections.emplace_back();
    header.type = "asset";
    header.attributes.push_back({"format", TextValue(metaFormatVersion)});
    header.attributes.push_back({"uuid", TextValue(meta.id.uuid.toString())});
    header.attributes.push_back({"importer", TextValue(meta.importer)});
    header.properties = meta.options;

    for (const MetaSubAsset& subAsset : meta.subAssets)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "subasset";
        section.attributes.push_back({"type", TextValue(std::string(toString(subAsset.type)))});
        section.attributes.push_back({"key", TextValue(subAsset.key)});
        section.attributes.push_back({"uuid", TextValue(subAsset.id.uuid.toString())});
    }
    return "# Devex import settings: keep this file next to its asset in version control.\n" +
           serialization::writeText(document);
}

std::uint64_t importSettingsHash(const MetaFile& meta)
{
    std::string settings = meta.importer;
    for (const serialization::TextProperty& option : meta.options)
    {
        settings += std::format("\n{}={}", option.key, serialization::formatValue(option.value));
    }
    return core::hash64(settings);
}

} // namespace devex::asset
