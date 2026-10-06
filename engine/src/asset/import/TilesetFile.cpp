#include <devex/asset/import/TilesetFile.hpp>

#include <devex/serialization/Text.hpp>

#include <format>
#include <optional>

namespace devex::asset {
namespace {

// 2: terrain sets, the terrains of the tiles and their probability.
constexpr std::int64_t tilesetFormat = 2;

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

[[nodiscard]] std::optional<math::Vec4> readColor(const serialization::TextValue& value)
{
    const serialization::TextCall* const call = serialization::asCall(value, "vec4");
    if (call == nullptr || call->arguments.size() != 4)
    {
        return std::nullopt;
    }
    math::Vec4 color{0.0f};
    for (int index = 0; index < 4; ++index)
    {
        const std::optional<double> number = serialization::asNumber(call->arguments[static_cast<std::size_t>(index)]);
        if (!number)
        {
            return std::nullopt;
        }
        color[index] = static_cast<float>(*number);
    }
    return color;
}

[[nodiscard]] serialization::TextValue number(float value)
{
    return serialization::TextValue(std::stod(std::format("{}", value)));
}

[[nodiscard]] std::optional<std::int32_t> readTerrain(const serialization::TextValue& value)
{
    const std::optional<std::int64_t> number = serialization::asInteger(value);
    if (!number || *number < noTerrain || *number >= static_cast<std::int64_t>(maxTerrains))
    {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*number);
}

// The terrains of a tile: its set, its middle and its sides and corners.
[[nodiscard]] core::Result<void> readTileTerrains(const serialization::TextSection& section, TileData& tile)
{
    if (const serialization::TextValue* const set = section.findAttribute("terrain_set"))
    {
        const std::optional<std::int64_t> number = serialization::asInteger(*set);
        if (!number || *number < noTerrain || *number >= static_cast<std::int64_t>(maxTerrainSets))
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: terrain_set is the number of a terrain set", section.line);
        }
        tile.terrainSet = static_cast<std::int32_t>(*number);
    }
    if (const serialization::TextValue* const terrain = section.findAttribute("terrain"))
    {
        const std::optional<std::int32_t> read = readTerrain(*terrain);
        if (!read)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: terrain is the number of a terrain", section.line);
        }
        tile.terrain = *read;
    }
    if (const serialization::TextValue* const bits = section.findProperty("bits"))
    {
        const serialization::TextCall* const list = serialization::asCall(*bits, "list");
        if (list == nullptr || list->arguments.size() != tileNeighborCount)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: the bits are a list of {} terrains", section.line,
                                   tileNeighborCount);
        }
        for (std::size_t index = 0; index < tileNeighborCount; ++index)
        {
            const std::optional<std::int32_t> read = readTerrain(list->arguments[index]);
            if (!read)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: a bit is the number of a terrain, or -1", section.line);
            }
            tile.terrainBits[index] = *read;
        }
    }
    if (const serialization::TextValue* const probability = section.findProperty("probability"))
    {
        tile.probability = static_cast<float>(serialization::asNumber(*probability).value_or(1.0));
    }
    return {};
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
        if (section.type == "terrain_set")
        {
            TerrainSetData& set = tileset.terrainSets.emplace_back();
            if (const serialization::TextValue* const mode = section.findAttribute("mode"))
            {
                const std::string* const name = serialization::asString(*mode);
                const std::optional<TerrainMode> parsed = name != nullptr ? parseTerrainMode(*name) : std::nullopt;
                if (!parsed)
                {
                    return core::makeError(core::ErrorCode::Parse,
                                           "line {}: mode is \"corners_and_sides\", \"corners\" or \"sides\"", section.line);
                }
                set.mode = *parsed;
            }
            const serialization::TextValue* const mirrorX = section.findAttribute("mirror_x");
            const serialization::TextValue* const mirrorY = section.findAttribute("mirror_y");
            set.mirrorX = mirrorX != nullptr && serialization::asBool(*mirrorX).value_or(false);
            set.mirrorY = mirrorY != nullptr && serialization::asBool(*mirrorY).value_or(false);
            continue;
        }
        if (section.type == "terrain")
        {
            if (tileset.terrainSets.empty())
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: a terrain follows its [terrain_set]", section.line);
            }
            TerrainData& terrain = tileset.terrainSets.back().terrains.emplace_back();
            if (const serialization::TextValue* const name = section.findAttribute("name"))
            {
                if (const std::string* const named = serialization::asString(*name))
                {
                    terrain.name = *named;
                }
            }
            if (const serialization::TextValue* const color = section.findAttribute("color"))
            {
                const std::optional<math::Vec4> read = readColor(*color);
                if (!read)
                {
                    return core::makeError(core::ErrorCode::Parse, "line {}: the colour of a terrain is a vec4(...)", section.line);
                }
                terrain.color = *read;
            }
            continue;
        }
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
        if (core::Result<void> terrains = readTileTerrains(section, tile); !terrains)
        {
            return std::unexpected(terrains.error());
        }
        tileset.tiles.push_back(std::move(tile));
    }
    for (TileData& tile : tileset.tiles)
    {
        if (tileset.terrainSet(tile.terrainSet) != nullptr)
        {
            normalizeTerrains(tile, tileset);
        }
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
    for (const TerrainSetData& set : tileset.terrainSets)
    {
        serialization::TextSection& setSection = document.sections.emplace_back();
        setSection.type = "terrain_set";
        setSection.attributes.push_back({"mode", serialization::TextValue(std::string(toString(set.mode)))});
        setSection.attributes.push_back({"mirror_x", serialization::TextValue(set.mirrorX)});
        setSection.attributes.push_back({"mirror_y", serialization::TextValue(set.mirrorY)});
        for (const TerrainData& terrain : set.terrains)
        {
            serialization::TextSection& terrainSection = document.sections.emplace_back();
            terrainSection.type = "terrain";
            terrainSection.attributes.push_back({"name", serialization::TextValue(terrain.name)});
            terrainSection.attributes.push_back(
                {"color", serialization::makeCall("vec4", {number(terrain.color.x), number(terrain.color.y), number(terrain.color.z),
                                                           number(terrain.color.w)})});
        }
    }
    for (const TileData& tile : tileset.tiles)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "tile";
        section.attributes.push_back({"id", serialization::TextValue(static_cast<std::int64_t>(tile.id))});
        section.attributes.push_back({"sprite", assetValue(tile.sprite)});
        section.attributes.push_back({"collision", serialization::TextValue(std::string(toString(tile.collision)))});
        if (tile.terrainSet != noTerrain)
        {
            section.attributes.push_back({"terrain_set", serialization::TextValue(static_cast<std::int64_t>(tile.terrainSet))});
            section.attributes.push_back({"terrain", serialization::TextValue(static_cast<std::int64_t>(tile.terrain))});
        }
        if (!tile.frames.empty())
        {
            std::vector<serialization::TextValue> frames;
            for (const AssetId frame : tile.frames)
            {
                frames.push_back(assetValue(frame));
            }
            section.properties.push_back({"frames", serialization::makeCall("list", std::move(frames))});
            section.properties.push_back({"fps", number(tile.fps)});
        }
        if (!tile.data.empty())
        {
            section.properties.push_back({"data", serialization::TextValue(tile.data)});
        }
        if (tile.terrainSet != noTerrain && tile.terrainBits != noTerrainBits)
        {
            std::vector<serialization::TextValue> bits;
            for (const std::int32_t bit : tile.terrainBits)
            {
                bits.emplace_back(static_cast<std::int64_t>(bit));
            }
            section.properties.push_back({"bits", serialization::makeCall("list", std::move(bits))});
        }
        if (tile.probability != 1.0f)
        {
            section.properties.push_back({"probability", number(tile.probability)});
        }
    }
    return serialization::writeText(document);
}

} // namespace devex::asset
