#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/MaterialFile.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <cctype>

namespace devex::asset {

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
        ImportResult result;
        result.artifacts.push_back({context.mainId, AssetType::Texture, context.name,
                                    encodeTexture(*texture)});
        return result;
    }

    const core::Result<Image> image = decodeImage(*encoded);
    if (!image)
    {
        return std::unexpected(image.error());
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

    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::Texture, context.name,
                                encodeTexture(*texture)});
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

std::span<const Importer> importers()
{
    using serialization::TextValue;
    static const std::vector<Importer> all{
        Importer{
            .name = "texture",
            // 2: high dynamic range images.
            .version = 2,
            .mainType = AssetType::Texture,
            .extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"},
            .defaultOptions =
                {
                    {"srgb", TextValue(true)},
                    {"normal_map", TextValue(false)},
                    {"mipmaps", TextValue(true)},
                    {"compress", TextValue(true)},
                    {"quality", TextValue(std::string("normal"))},
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
