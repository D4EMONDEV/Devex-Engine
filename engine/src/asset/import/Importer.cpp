#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/CurveFile.hpp>
#include <devex/asset/import/MaterialFile.hpp>
#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/asset/import/TilesetFile.hpp>
#include <devex/asset/import/ThemeFile.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <algorithm>
#include <cctype>
#include <format>

namespace devex::asset {
namespace {

// Gives the transparent pixels of a sprite the colors of their opaque neighbours, a few pixels
// deep: a smooth filter then blends its edges with their own colors rather than with black.
void spreadColorsUnderTransparency(Image& image)
{
    const std::uint32_t width = image.width;
    const std::uint32_t height = image.height;
    std::vector<std::uint8_t> filled(static_cast<std::size_t>(width) * height);
    for (std::size_t pixel = 0; pixel < filled.size(); ++pixel)
    {
        filled[pixel] = image.rgba[pixel * 4 + 3] > 0 ? 1 : 0;
    }
    std::vector<std::uint8_t> next = filled;
    for (int pass = 0; pass < 4; ++pass)
    {
        bool changed = false;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                if (filled[pixel] != 0)
                {
                    continue;
                }
                std::uint32_t sum[3]{};
                std::uint32_t count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const auto nx = static_cast<std::int64_t>(x) + dx;
                        const auto ny = static_cast<std::int64_t>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        {
                            continue;
                        }
                        const std::size_t neighbour = static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx);
                        if (filled[neighbour] != 0)
                        {
                            for (int channel = 0; channel < 3; ++channel)
                            {
                                sum[channel] += image.rgba[neighbour * 4 + static_cast<std::size_t>(channel)];
                            }
                            ++count;
                        }
                    }
                }
                if (count > 0)
                {
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        image.rgba[pixel * 4 + static_cast<std::size_t>(channel)] = static_cast<std::uint8_t>(sum[channel] / count);
                    }
                    next[pixel] = 1;
                    changed = true;
                }
            }
        }
        if (!changed)
        {
            break;
        }
        filled = next;
    }
}

// Whether a rectangle of the image has a pixel that is not fully transparent.
[[nodiscard]] bool hasVisiblePixel(const Image& image, std::uint32_t left, std::uint32_t top, std::uint32_t width,
                                   std::uint32_t height) noexcept
{
    for (std::uint32_t y = top; y < top + height; ++y)
    {
        for (std::uint32_t x = left; x < left + width; ++x)
        {
            if (image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4 + 3] != 0)
            {
                return true;
            }
        }
    }
    return false;
}

// The sprites the "sprite_mode" option cuts the image into: "single" makes the whole image one
// sprite, "grid" cuts it into "columns" by "rows" cells, row by row from the top-left one, leaving
// out the cells without a visible pixel.
[[nodiscard]] core::Result<std::vector<ImportedArtifact>> sliceSprites(ImportContext& context, const Image& image)
{
    const std::string mode = context.stringOption("sprite_mode", "none");
    if (mode == "none")
    {
        return std::vector<ImportedArtifact>{};
    }
    if (mode != "single" && mode != "grid")
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "sprite_mode is \"none\", \"single\" or \"grid\", not \"{}\"", mode);
    }
    SpriteData base{
        .texture = context.mainId,
        .textureWidth = image.width,
        .textureHeight = image.height,
        .pixelsPerUnit = static_cast<float>(context.numberOption("pixels_per_unit", 100.0)),
    };
    const math::Vec4 pivot = context.vectorOption("pivot", math::Vec4{0.5f, 0.5f, 0.0f, 0.0f});
    base.pivot = math::Vec2{pivot.x, pivot.y};
    base.border = context.vectorOption("border", math::Vec4{0.0f});

    std::vector<ImportedArtifact> sprites;
    const auto add = [&](std::string_view key, std::string name, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                         std::uint32_t height) -> core::Result<void> {
        SpriteData sprite = base;
        sprite.x = x;
        sprite.y = y;
        sprite.width = width;
        sprite.height = height;
        if (core::Result<void> valid = validate(sprite); !valid)
        {
            return valid;
        }
        sprites.push_back({context.subAssets.acquire(AssetType::Sprite, key), AssetType::Sprite, std::move(name),
                           encodeSprite(sprite)});
        return {};
    };
    if (mode == "single")
    {
        if (core::Result<void> added = add("sprite", context.name, 0, 0, image.width, image.height); !added)
        {
            return std::unexpected(added.error());
        }
        return sprites;
    }
    const auto columns = static_cast<std::uint32_t>(std::max(context.numberOption("columns", 1.0), 1.0));
    const auto rows = static_cast<std::uint32_t>(std::max(context.numberOption("rows", 1.0), 1.0));
    const std::uint32_t cellWidth = image.width / columns;
    const std::uint32_t cellHeight = image.height / rows;
    if (cellWidth == 0 || cellHeight == 0)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a {}x{} image has no room for {} columns and {} rows",
                               image.width, image.height, columns, rows);
    }
    for (std::uint32_t row = 0; row < rows; ++row)
    {
        for (std::uint32_t column = 0; column < columns; ++column)
        {
            const std::uint32_t x = column * cellWidth;
            const std::uint32_t y = row * cellHeight;
            if (!hasVisiblePixel(image, x, y, cellWidth, cellHeight))
            {
                continue;
            }
            const std::uint32_t index = row * columns + column;
            if (core::Result<void> added =
                    add(std::to_string(index), std::format("{}_{}", context.name, index), x, y, cellWidth, cellHeight);
                !added)
            {
                return std::unexpected(added.error());
            }
        }
    }
    return sprites;
}

[[nodiscard]] core::Result<TextureFilter> textureFilter(const ImportContext& context)
{
    const std::string filter = context.stringOption("filter", "linear");
    if (filter == "linear")
    {
        return TextureFilter::Linear;
    }
    if (filter == "nearest")
    {
        return TextureFilter::Nearest;
    }
    return core::makeError(core::ErrorCode::InvalidArgument, "filter is \"linear\" or \"nearest\", not \"{}\"", filter);
}

} // namespace

SubAssetIds::SubAssetIds(std::vector<MetaSubAsset> entries) noexcept
    : m_entries(std::move(entries))
{
}

AssetId SubAssetIds::acquire(AssetType type, std::string_view key)
{
    for (const MetaSubAsset& entry : m_entries)
    {
        if (entry.type == type && entry.key == key)
        {
            return entry.id;
        }
    }
    m_entries.push_back({type, std::string(key), AssetId::generate()});
    m_hasNewEntries = true;
    return m_entries.back().id;
}

const std::vector<MetaSubAsset>& SubAssetIds::entries() const noexcept
{
    return m_entries;
}

bool SubAssetIds::hasNewEntries() const noexcept
{
    return m_hasNewEntries;
}

bool ImportContext::isCancelled() const noexcept
{
    return cancelled != nullptr && cancelled->load(std::memory_order_relaxed);
}

bool ImportContext::boolOption(std::string_view key, bool fallback) const noexcept
{
    for (const serialization::TextProperty& property : options)
    {
        if (property.key == key)
        {
            return serialization::asBool(property.value).value_or(fallback);
        }
    }
    return fallback;
}

double ImportContext::numberOption(std::string_view key, double fallback) const noexcept
{
    for (const serialization::TextProperty& property : options)
    {
        if (property.key == key)
        {
            return serialization::asNumber(property.value).value_or(fallback);
        }
    }
    return fallback;
}

math::Vec4 ImportContext::vectorOption(std::string_view key, math::Vec4 fallback) const noexcept
{
    for (const serialization::TextProperty& property : options)
    {
        if (property.key != key)
        {
            continue;
        }
        const auto* const call = std::get_if<serialization::TextCall>(&property.value);
        if (call == nullptr || !call->name.starts_with("vec"))
        {
            return fallback;
        }
        for (std::size_t index = 0; index < std::min<std::size_t>(call->arguments.size(), 4); ++index)
        {
            fallback[static_cast<int>(index)] =
                static_cast<float>(serialization::asNumber(call->arguments[index]).value_or(fallback[static_cast<int>(index)]));
        }
        return fallback;
    }
    return fallback;
}

std::string ImportContext::stringOption(std::string_view key, std::string_view fallback) const
{
    for (const serialization::TextProperty& property : options)
    {
        if (const std::string* const text = serialization::asString(property.value);
            property.key == key && text != nullptr)
        {
            return *text;
        }
    }
    return std::string(fallback);
}

core::Result<ImportResult> importTextureFile(ImportContext& context)
{
    const core::Result<std::vector<std::byte>> encoded = core::readBinaryFile(context.source);
    if (!encoded)
    {
        return std::unexpected(encoded.error());
    }
    const core::Result<TextureFilter> filter = textureFilter(context);
    if (!filter)
    {
        return std::unexpected(filter.error());
    }
    if (isHighDynamicRange(*encoded))
    {
        const core::Result<FloatImage> floatImage = decodeFloatImage(*encoded);
        if (!floatImage)
        {
            return std::unexpected(floatImage.error());
        }
        core::Result<TextureData> texture =
            buildFloatTexture(*floatImage, context.boolOption("mipmaps", true), context.jobs);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        texture->filter = *filter;
        ImportResult result;
        result.artifacts.push_back({context.mainId, AssetType::Texture, context.name,
                                    encodeTexture(*texture)});
        return result;
    }

    core::Result<Image> image = decodeImage(*encoded);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    core::Result<std::vector<ImportedArtifact>> sprites = sliceSprites(context, *image);
    if (!sprites)
    {
        return std::unexpected(sprites.error());
    }
    if (!sprites->empty())
    {
        spreadColorsUnderTransparency(*image);
    }

    const std::string qualityName = context.stringOption("quality", "normal");
    const std::optional<TextureQuality> quality = parseTextureQuality(qualityName);
    if (!quality)
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "quality is \"fast\", \"normal\" or \"high\", not \"{}\"",
                               qualityName);
    }

    const TextureBuildOptions options{
        .srgb = context.boolOption("srgb", true),
        .normalMap = context.boolOption("normal_map", false),
        .mipmaps = context.boolOption("mipmaps", true),
        .compress = context.boolOption("compress", true),
        .quality = *quality,
    };
    core::Result<TextureData> texture = buildTexture(*image, options, context.jobs, context.cancelled);
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    texture->filter = *filter;

    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::Texture, context.name,
                                encodeTexture(*texture)});
    std::ranges::move(*sprites, std::back_inserter(result.artifacts));
    return result;
}

core::Result<ImportResult> importMaterialFile(ImportContext& context)
{
    const core::Result<std::string> text = core::readTextFile(context.source);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    const core::Result<MaterialData> material = parseMaterialFile(*text);
    if (!material)
    {
        return std::unexpected(material.error());
    }

    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::Material, context.name,
                                encodeMaterial(*material)});
    return result;
}

core::Result<ImportResult> importThemeFile(ImportContext& context)
{
    const core::Result<std::string> text = core::readTextFile(context.source);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    const core::Result<ThemeData> theme = parseThemeFile(*text);
    if (!theme)
    {
        return std::unexpected(theme.error());
    }

    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::Theme, context.name,
                                encodeTheme(*theme)});
    return result;
}

core::Result<ImportResult> importCurveFile(ImportContext& context)
{
    const core::Result<std::string> text = core::readTextFile(context.source);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    const core::Result<CurveData> curve = parseCurveFile(*text);
    if (!curve)
    {
        return std::unexpected(curve.error());
    }
    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::Curve, context.name, encodeCurve(*curve)});
    return result;
}

core::Result<ImportResult> importSpriteFramesFile(ImportContext& context)
{
    const core::Result<std::string> text = core::readTextFile(context.source);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    const core::Result<SpriteFramesData> frames = parseSpriteFramesFile(*text);
    if (!frames)
    {
        return std::unexpected(frames.error());
    }
    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::SpriteFrames, context.name, encodeSpriteFrames(*frames)});
    return result;
}

core::Result<ImportResult> importTilesetFile(ImportContext& context)
{
    const core::Result<std::string> text = core::readTextFile(context.source);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    const core::Result<TilesetData> tileset = parseTilesetFile(*text);
    if (!tileset)
    {
        return std::unexpected(tileset.error());
    }
    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::Tileset, context.name, encodeTileset(*tileset)});
    return result;
}

core::Result<ImportResult> importSceneFile(ImportContext& context)
{
    const core::Result<std::string> text = core::readTextFile(context.source);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    // A scene that does not load is reported now rather than when it is opened. Its prefabs are other
    // assets, which are checked when they are imported themselves.
    if (core::Result<scene::Scene> loaded = scene::loadScene(*text, scene::PrefabLoading::KeepUnresolved); !loaded)
    {
        return std::unexpected(loaded.error());
    }

    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::Scene, context.name, encodeScene(*text)});
    return result;
}

std::span<const Importer> importers()
{
    using serialization::TextValue;
    static const std::vector<Importer> all{
        Importer{
            .name = "texture",
            // 2: high dynamic range images. 3: the filter and the sprites.
            .version = 3,
            .mainType = AssetType::Texture,
            .extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"},
            .defaultOptions =
                {
                    {"srgb", TextValue(true)},
                    {"normal_map", TextValue(false)},
                    {"mipmaps", TextValue(true)},
                    {"compress", TextValue(true)},
                    {"quality", TextValue(std::string("normal"))},
                    {"filter", TextValue(std::string("linear"))},
                    {"sprite_mode", TextValue(std::string("none"))},
                },
            .run = &importTextureFile,
        },
        Importer{
            .name = "material",
            .version = 1,
            .mainType = AssetType::Material,
            .extensions = {materialExtension},
            .run = &importMaterialFile,
        },
        Importer{
            .name = "gltf",
            // 2: tangents.
            .version = 2,
            .mainType = AssetType::Model,
            .extensions = {".gltf", ".glb"},
            .defaultOptions =
                {
                    {"compress_textures", TextValue(true)},
                    {"texture_quality", TextValue(std::string("normal"))},
                },
            .run = &importGltfFile,
            .findDependencies = &findGltfDependencies,
        },
        Importer{
            .name = "fbx",
            .version = 1,
            .mainType = AssetType::Model,
            .extensions = {".fbx"},
            .defaultOptions =
                {
                    {"scale", TextValue(1.0)},
                    {"compress_textures", TextValue(true)},
                    {"texture_quality", TextValue(std::string("normal"))},
                },
            .run = &importFbxFile,
            .findDependencies = &findFbxDependencies,
        },
        Importer{
            .name = "obj",
            .version = 1,
            .mainType = AssetType::Model,
            .extensions = {".obj"},
            .defaultOptions =
                {
                    {"scale", TextValue(1.0)},
                    {"compress_textures", TextValue(true)},
                    {"texture_quality", TextValue(std::string("normal"))},
                },
            // ufbx reads OBJ files too.
            .run = &importFbxFile,
            .findDependencies = &findFbxDependencies,
        },
        Importer{
            .name = "audio",
            .version = 1,
            .mainType = AssetType::AudioClip,
            .extensions = {".wav", ".ogg", ".mp3", ".flac"},
            // "auto" decodes short clips once and streams the long ones.
            .defaultOptions = {{"loading", TextValue(std::string("auto"))}},
            .run = &importAudioFile,
        },
        Importer{
            .name = "font",
            // 2: central European letters, the punctuation of running text and the euro sign.
            .version = 2,
            .mainType = AssetType::Font,
            .extensions = {".ttf", ".otf"},
            .defaultOptions =
                {
                    {"size", TextValue(48.0)},
                    {"spread", TextValue(6.0)},
                },
            .run = &importFontFile,
        },
        Importer{
            .name = "theme",
            .version = 1,
            .mainType = AssetType::Theme,
            .extensions = {themeExtension},
            .run = &importThemeFile,
        },
        Importer{
            .name = "curve",
            .version = 1,
            .mainType = AssetType::Curve,
            .extensions = {curveExtension},
            .run = &importCurveFile,
        },
        Importer{
            .name = "frames",
            .version = 1,
            .mainType = AssetType::SpriteFrames,
            .extensions = {spriteFramesExtension},
            .run = &importSpriteFramesFile,
        },
        Importer{
            .name = "tileset",
            .version = 1,
            .mainType = AssetType::Tileset,
            .extensions = {tilesetExtension},
            .run = &importTilesetFile,
        },
        Importer{
            .name = "scene",
            .version = 1,
            .mainType = AssetType::Scene,
            .extensions = {scene::sceneExtension},
            .run = &importSceneFile,
        },
    };
    return all;
}

const Importer* findImporterForExtension(std::string_view extension)
{
    std::string lowercase(extension);
    std::ranges::transform(lowercase, lowercase.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    for (const Importer& importer : importers())
    {
        if (std::ranges::find(importer.extensions, lowercase) != importer.extensions.end())
        {
            return &importer;
        }
    }
    return nullptr;
}

const Importer* findImporter(std::string_view name)
{
    for (const Importer& importer : importers())
    {
        if (importer.name == name)
        {
            return &importer;
        }
    }
    return nullptr;
}

} // namespace devex::asset
