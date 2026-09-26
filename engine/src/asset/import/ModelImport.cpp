#include "ModelImport.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <format>
#include <unordered_map>

namespace devex::asset::detail {

std::string_view keySuffix(TextureRole role) noexcept
{
    switch (role)
    {
    case TextureRole::Color:
        return "";
    case TextureRole::Data:
        return " (linear)";
    case TextureRole::Normal:
        return " (normal)";
    }
    return "";
}

std::vector<std::string> uniqueKeys(std::vector<std::string> names)
{
    std::unordered_map<std::string, std::size_t> occurrences;
    for (const std::string& name : names)
    {
        ++occurrences[name];
    }
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        if (occurrences[names[index]] > 1)
        {
            names[index] = std::format("{} #{}", names[index], index);
        }
    }
    return names;
}

std::vector<std::optional<ImportedArtifact>> buildTextures(const ImportContext& context,
                                                           std::span<const TextureRequest> requests)
{
    const std::optional<TextureQuality> quality =
        parseTextureQuality(context.stringOption("texture_quality", "normal"));
    const bool compress = context.boolOption("compress_textures", true);
    const std::string fileName = core::toUtf8(context.source.filename());

    std::vector<std::optional<ImportedArtifact>> built(requests.size());
    const auto build = [&](std::size_t index) {
        const TextureRequest& request = requests[index];
        const core::Result<Image> image = request.decode();
        if (!image)
        {
            DEVEX_LOG_WARNING("Skipping image '{}' of '{}': {}", request.key, fileName, image.error());
            return;
        }
        const TextureBuildOptions options{
            .srgb = request.role == TextureRole::Color,
            .normalMap = request.role == TextureRole::Normal,
            .compress = compress,
            .quality = quality.value_or(TextureQuality::Normal),
        };
        core::Result<TextureData> texture = buildTexture(*image, options, context.jobs, context.cancelled);
        if (!texture)
        {
            if (!context.isCancelled())
            {
                DEVEX_LOG_WARNING("Skipping image '{}' of '{}': {}", request.key, fileName,
                                  texture.error());
            }
            return;
        }
        built[index] = ImportedArtifact{request.id, AssetType::Texture, request.key, encodeTexture(*texture)};
    };
    if (context.jobs != nullptr)
    {
        context.jobs->parallelFor(requests.size(), build);
    }
    else
    {
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            build(index);
        }
    }
    return built;
}

} // namespace devex::asset::detail
