#include <devex/asset/import/MaterialFile.hpp>
#include <devex/scene/FieldValue.hpp>

#include <array>
#include <optional>

namespace devex::asset {
namespace {

using reflection::ValueKind;
using serialization::TextSection;
using serialization::TextValue;

constexpr std::int64_t materialFormatVersion = 1;

struct MaterialField
{
    std::string_view key;
    ValueKind kind;
    void* (*address)(MaterialData& material);
};

// The properties with a reflected value kind. alpha_mode is a name and is handled separately.
const std::array<MaterialField, 13> materialFields{{
    {"base_color", ValueKind::Vec4, [](MaterialData& m) -> void* { return &m.baseColorFactor; }},
    {"base_color_texture", ValueKind::AssetId,
     [](MaterialData& m) -> void* { return &m.baseColorTexture; }},
    {"metallic", ValueKind::Float, [](MaterialData& m) -> void* { return &m.metallicFactor; }},
    {"roughness", ValueKind::Float, [](MaterialData& m) -> void* { return &m.roughnessFactor; }},
    {"metallic_roughness_texture", ValueKind::AssetId,
     [](MaterialData& m) -> void* { return &m.metallicRoughnessTexture; }},
    {"normal_texture", ValueKind::AssetId,
     [](MaterialData& m) -> void* { return &m.normalTexture; }},
    {"normal_scale", ValueKind::Float, [](MaterialData& m) -> void* { return &m.normalScale; }},
    {"occlusion_texture", ValueKind::AssetId,
     [](MaterialData& m) -> void* { return &m.occlusionTexture; }},
    {"occlusion_strength", ValueKind::Float,
     [](MaterialData& m) -> void* { return &m.occlusionStrength; }},
    {"emissive", ValueKind::Vec3, [](MaterialData& m) -> void* { return &m.emissiveFactor; }},
    {"emissive_texture", ValueKind::AssetId,
     [](MaterialData& m) -> void* { return &m.emissiveTexture; }},
    {"alpha_cutoff", ValueKind::Float, [](MaterialData& m) -> void* { return &m.alphaCutoff; }},
    {"double_sided", ValueKind::Bool, [](MaterialData& m) -> void* { return &m.doubleSided; }},
}};

} // namespace

core::Result<MaterialData> parseMaterialFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.size() != 1 || document->sections.front().type != "material")
    {
        return core::makeError(core::ErrorCode::Parse,
                               "a material file holds exactly one [material] section");
    }

    const TextSection& section = document->sections.front();
    const TextValue* const format = section.findAttribute("format");
    const std::optional<std::int64_t> version =
        format != nullptr ? serialization::asInteger(*format) : std::nullopt;
    if (!version || *version > materialFormatVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "unknown material format, a newer Devex may be needed");
    }

    MaterialData material;
    for (const serialization::TextProperty& property : section.properties)
    {
        if (property.key == "alpha_mode")
        {
            const std::string* const name = serialization::asString(property.value);
            const std::optional<AlphaMode> mode =
                name != nullptr ? parseAlphaMode(*name) : std::nullopt;
            if (!mode)
            {
                return core::makeError(core::ErrorCode::Parse,
                                       "line {}: alpha_mode is \"opaque\", \"mask\" or \"blend\"",
                                       property.line);
            }
            material.alphaMode = *mode;
            continue;
        }

        const MaterialField* field = nullptr;
        for (const MaterialField& candidate : materialFields)
        {
            field = candidate.key == property.key ? &candidate : field;
        }
        if (field == nullptr)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: unknown property '{}'",
                                   property.line, property.key);
        }
        if (core::Result<void> read =
                scene::readFieldValue(field->kind, property.value, field->address(material));
            !read)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: {}: {}", property.line,
                                   property.key, read.error().message);
        }
    }
    return material;
}

std::string writeMaterialFile(const MaterialData& material)
{
    serialization::TextDocument document;
    TextSection& section = document.sections.emplace_back();
    section.type = "material";
    section.attributes.push_back({"format", TextValue(materialFormatVersion)});

    MaterialData copy = material;
    for (const MaterialField& field : materialFields)
    {
        section.properties.push_back({std::string(field.key),
                                      scene::writeFieldValue(field.kind, field.address(copy))});
    }
    section.properties.push_back(
        {"alpha_mode", TextValue(std::string(toString(material.alphaMode)))});
    return serialization::writeText(document);
}

} // namespace devex::asset
