#include <devex/asset/import/TilesetFile.hpp>

#include <devex/serialization/Text.hpp>

#include <format>
#include <optional>

namespace devex::asset {
namespace {

constexpr std::int64_t tilesetFormat = 1;

[[nodiscard]] std::optional<AssetId> readAsset(const serialization::TextValue& value)
{
    const serialization::TextCall* const call = serialization::asCall(value, "asset");
    if (call == nullptr || call->arguments.size() != 1)
    {
        return std::nullopt;
    }
    const std::string* const text = serialization::asString(call->arguments.front());
    const std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    return uuid ? std::optional(AssetId{*uuid}) : std::nullopt;
}

[[nodiscard]] serialization::TextValue assetValue(AssetId id)
{
    return serialization::makeCall("asset", {serialization::TextValue(id.uuid.toString())});
}

} // namespace

core::Result<TilesetData> parseTilesetFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "tileset")
    {
        return core::makeError(core::ErrorCode::Parse, "a tileset file starts with [tileset]");
    }
    const serialization::TextValue* const format = document->sections.front().findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format).value_or(tilesetFormat + 1) > tilesetFormat)
    {
        return core::makeError(core::ErrorCode::Unsupported, "the tileset needs a newer version of Devex");
    }
    TilesetData tileset;
    for (const serialization::TextSection& section : document->sections)
    {
        if (section.type != "tile")
        {
            continue;
        }
        TileData tile;
        const serialization::TextValue* const id = section.findAttribute("id");
        const std::optional<std::int64_t> number = id != nullptr ? serialization::asInteger(*id) : std::nullopt;
        if (!number || *number <= 0)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: a tile needs an id", section.line);
        }
        tile.id = static_cast<std::uint32_t>(*number);
        if (const serialization::TextValue* const sprite = section.findAttribute("sprite"))
        {
            const std::optional<AssetId> asset = readAsset(*sprite);
            if (!asset)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: the sprite is an asset(\"uuid\")", section.line);
            }
            tile.sprite = *asset;
        }
        if (const serialization::TextValue* const collision = section.findAttribute("collision"))
        {
            const std::string* const name = serialization::asString(*collision);
            const std::optional<TileCollision> parsed = name != nullptr ? parseTileCollision(*name) : std::nullopt;
            if (!parsed)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: collision is \"none\", \"full\" or \"top\"",
                                       section.line);
            }
            tile.collision = *parsed;
        }
        if (const serialization::TextValue* const frames = section.findProperty("frames"))
        {
            const serialization::TextCall* const list = serialization::asCall(*frames, "list");
            if (list == nullptr)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: the frames are a list(...)", section.line);
            }
            for (const serialization::TextValue& frame : list->arguments)
            {
                const std::optional<AssetId> asset = readAsset(frame);
                if (!asset)
                {
                    return core::makeError(core::ErrorCode::Parse, "line {}: a frame is an asset(\"uuid\")", section.line);
                }
                tile.frames.push_back(*asset);
            }
        }
        if (const serialization::TextValue* const fps = section.findProperty("fps"))
        {
            tile.fps = static_cast<float>(serialization::asNumber(*fps).value_or(0.0));
        }
        if (const serialization::TextValue* const data = section.findProperty("data"))
        {
            if (const std::string* const value = serialization::asString(*data))
            {
                tile.data = *value;
            }
        }
        tileset.tiles.push_back(std::move(tile));
    }
    if (core::Result<void> valid = validate(tileset); !valid)
    {
        return std::unexpected(valid.error());
    }
    return tileset;
}

std::string writeTilesetFile(const TilesetData& tileset)
{
    serialization::TextDocument document;
    serialization::TextSection& header = document.sections.emplace_back();
    header.type = "tileset";
    header.attributes.push_back({"format", serialization::TextValue(tilesetFormat)});
    for (const TileData& tile : tileset.tiles)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "tile";
        section.attributes.push_back({"id", serialization::TextValue(static_cast<std::int64_t>(tile.id))});
        section.attributes.push_back({"sprite", assetValue(tile.sprite)});
        section.attributes.push_back({"collision", serialization::TextValue(std::string(toString(tile.collision)))});
        if (!tile.frames.empty())
        {
            std::vector<serialization::TextValue> frames;
            for (const AssetId frame : tile.frames)
            {
                frames.push_back(assetValue(frame));
            }
            section.properties.push_back({"frames", serialization::makeCall("list", std::move(frames))});
            section.properties.push_back({"fps", serialization::TextValue(std::stod(std::format("{}", tile.fps)))});
        }
        if (!tile.data.empty())
        {
            section.properties.push_back({"data", serialization::TextValue(tile.data)});
        }
    }
    return serialization::writeText(document);
}

} // namespace devex::asset
