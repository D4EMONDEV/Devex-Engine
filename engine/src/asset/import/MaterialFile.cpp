#include <devex/asset/import/MaterialFile.hpp>
#include <devex/scene/FieldValue.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <vector>

namespace devex::asset {
namespace {

using reflection::ValueKind;
using serialization::TextSection;
using serialization::TextValue;

// 2: the shader, and the values of its uniforms.
constexpr std::int64_t materialFormatVersion = 2;

struct MaterialField
{
    std::string_view key;
    ValueKind kind;
    void* (*address)(MaterialData& material);
};

// The properties with a reflected value kind. alpha_mode is a name and is handled separately.
const std::array<MaterialField, 14> materialFields{{
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
    {"shader", ValueKind::AssetId, [](MaterialData& m) -> void* { return &m.shader; }},
}};

// A value of the [parameters] section: a number, a vector, a boolean or a texture.
[[nodiscard]] core::Result<MaterialParameter> readParameter(const serialization::TextProperty& property)
{
    MaterialParameter parameter{.name = property.key};
    if (const std::optional<bool> flag = serialization::asBool(property.value))
    {
        parameter.value.x = *flag ? 1.0f : 0.0f;
        return parameter;
    }
    if (const std::optional<double> number = serialization::asNumber(property.value))
    {
        parameter.value.x = static_cast<float>(*number);
        return parameter;
    }
    if (serialization::asCall(property.value, "asset") != nullptr)
    {
        if (core::Result<void> read = scene::readFieldValue(ValueKind::AssetId, property.value, &parameter.texture); !read)
        {
            return std::unexpected(read.error());
        }
        return parameter;
    }
    constexpr std::array<std::string_view, 3> vectors{"vec2", "vec3", "vec4"};
    for (std::size_t index = 0; index < vectors.size(); ++index)
    {
        const serialization::TextCall* const call = serialization::asCall(property.value, vectors[index]);
        if (call == nullptr)
        {
            continue;
        }
        if (call->arguments.size() != index + 2)
        {
            break;
        }
        for (std::size_t component = 0; component < call->arguments.size(); ++component)
        {
            const std::optional<double> number = serialization::asNumber(call->arguments[component]);
            if (!number)
            {
                return core::makeError(core::ErrorCode::Parse, "{} takes numbers", vectors[index]);
            }
            parameter.value[static_cast<int>(component)] = static_cast<float>(*number);
        }
        parameter.components = static_cast<std::uint8_t>(index + 2);
        return parameter;
    }
    return core::makeError(core::ErrorCode::Parse, "a number, vec2(...) to vec4(...), true, false or asset(\"...\")");
}

[[nodiscard]] serialization::TextValue writeParameter(const MaterialParameter& parameter)
{
    if (parameter.texture.isValid())
    {
        AssetId texture = parameter.texture;
        return scene::writeFieldValue(ValueKind::AssetId, &texture);
    }
    // Numbers as the fields of components write them: the shortest text of the float.
    switch (parameter.components)
    {
    case 2: {
        math::Vec2 value(parameter.value);
        return scene::writeFieldValue(ValueKind::Vec2, &value);
    }
    case 3: {
        math::Vec3 value(parameter.value);
        return scene::writeFieldValue(ValueKind::Vec3, &value);
    }
    case 4: {
        math::Vec4 value = parameter.value;
        return scene::writeFieldValue(ValueKind::Vec4, &value);
    }
    default: {
        float value = parameter.value.x;
        return scene::writeFieldValue(ValueKind::Float, &value);
    }
    }
}

} // namespace

core::Result<MaterialData> parseMaterialFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    const std::size_t sections = document->sections.size();
    if (sections < 1 || sections > 2 || document->sections.front().type != "material" ||
        (sections == 2 && document->sections[1].type != "parameters"))
    {
        return core::makeError(core::ErrorCode::Parse,
                               "a material file holds one [material] section, then the [parameters] of its shader");
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
    if (sections == 2)
    {
        for (const serialization::TextProperty& property : document->sections[1].properties)
        {
            core::Result<MaterialParameter> parameter = readParameter(property);
            if (!parameter)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: {}: {}", property.line, property.key,
                                       parameter.error().message);
            }
            if (material.findParameter(property.key) != nullptr)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: '{}' is given twice", property.line, property.key);
            }
            material.parameters.push_back(std::move(*parameter));
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
        // Materials drawn by the standard shader name none.
        if (field.key == "shader" && !material.shader.isValid())
        {
            continue;
        }
        section.properties.push_back({std::string(field.key),
                                      scene::writeFieldValue(field.kind, field.address(copy))});
    }
    section.properties.push_back(
        {"alpha_mode", TextValue(std::string(toString(material.alphaMode)))});
    if (!material.parameters.empty())
    {
        TextSection& parameters = document.sections.emplace_back();
        parameters.type = "parameters";
        for (const MaterialParameter& parameter : material.parameters)
        {
            parameters.properties.push_back({parameter.name, writeParameter(parameter)});
        }
    }
    return serialization::writeText(document);
}

} // namespace devex::asset
