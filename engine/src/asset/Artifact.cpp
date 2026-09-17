#include <devex/asset/Artifact.hpp>
#include <devex/serialization/Binary.hpp>

#include <format>
#include <utility>

namespace devex::asset {
namespace {

using serialization::BinaryReader;
using serialization::BinaryWriter;

// "DVXA" in file order.
constexpr std::uint32_t artifactMagic = 0x41585644;

struct Header
{
    std::uint32_t magic = artifactMagic;
    AssetType type = AssetType::Mesh;
    std::uint8_t reserved[3]{};
    std::uint32_t version = 0;
};
static_assert(sizeof(Header) == 12);

[[nodiscard]] BinaryWriter beginArtifact(AssetType type)
{
    BinaryWriter writer;
    writer.write(Header{.type = type, .version = artifactVersion(type)});
    return writer;
}

[[nodiscard]] core::Result<void> readHeader(BinaryReader& reader, AssetType expected)
{
    const auto header = reader.read<Header>();
    if (reader.failed() || header.magic != artifactMagic)
    {
        return core::makeError(core::ErrorCode::Parse, "not a Devex asset file");
    }
    if (header.type != expected)
    {
        return core::makeError(core::ErrorCode::Parse, "the file holds a {} instead of a {}",
                               toString(header.type), toString(expected));
    }
    if (header.version != artifactVersion(expected))
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "{} layout version {} instead of {}", toString(expected),
                               header.version, artifactVersion(expected));
    }
    return {};
}

[[nodiscard]] core::Error truncated(AssetType type)
{
    return core::Error{core::ErrorCode::Parse,
                       std::format("the {} data is truncated or corrupt", toString(type))};
}

void writeQuat(BinaryWriter& writer, const math::Quat& rotation)
{
    for (const float component : {rotation.x, rotation.y, rotation.z, rotation.w})
    {
        writer.write(component);
    }
}

[[nodiscard]] math::Quat readQuat(BinaryReader& reader) noexcept
{
    const auto x = reader.read<float>();
    const auto y = reader.read<float>();
    const auto z = reader.read<float>();
    const auto w = reader.read<float>();
    return {w, x, y, z};
}

} // namespace

std::uint32_t artifactVersion(AssetType type) noexcept
{
    switch (type)
    {
    // 2: vertex tangents.
    case AssetType::Mesh:
        return 2;
    case AssetType::Texture:
    case AssetType::Material:
    case AssetType::Model:
    case AssetType::Scene:
        return 1;
    }
    return 0;
}

core::Result<AssetType> artifactType(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    const auto header = reader.read<Header>();
    if (reader.failed() || header.magic != artifactMagic)
    {
        return core::makeError(core::ErrorCode::Parse, "not a Devex asset file");
    }
    return header.type;
}

std::vector<std::byte> encodeMesh(const MeshData& mesh)
{
    BinaryWriter writer = beginArtifact(AssetType::Mesh);
    writer.writeArray(std::span<const Vertex>(mesh.vertices));
    writer.writeArray(std::span<const std::uint32_t>(mesh.indices));
    writer.write(static_cast<std::uint32_t>(mesh.submeshes.size()));
    for (const Submesh& submesh : mesh.submeshes)
    {
        writer.write(submesh.firstIndex);
        writer.write(submesh.indexCount);
        writer.write(submesh.material);
    }
    return writer.take();
}

core::Result<MeshData> decodeMesh(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::Mesh); !header)
    {
        return std::unexpected(header.error());
    }

    MeshData mesh;
    mesh.vertices = reader.readArray<Vertex>();
    mesh.indices = reader.readArray<std::uint32_t>();
    const auto submeshCount = reader.read<std::uint32_t>();
    if (submeshCount > reader.remaining() / sizeof(Submesh))
    {
        reader.fail();
    }
    for (std::uint32_t index = 0; index < submeshCount && !reader.failed(); ++index)
    {
        Submesh& submesh = mesh.submeshes.emplace_back();
        submesh.firstIndex = reader.read<std::uint32_t>();
        submesh.indexCount = reader.read<std::uint32_t>();
        submesh.material = reader.read<AssetId>();
    }
    if (reader.failed())
    {
        return std::unexpected(truncated(AssetType::Mesh));
    }
    if (core::Result<void> valid = validate(mesh); !valid)
    {
        return std::unexpected(valid.error());
    }
    return mesh;
}

std::vector<std::byte> encodeTexture(const TextureData& texture)
{
    BinaryWriter writer = beginArtifact(AssetType::Texture);
    writer.write(texture.format);
    writer.write(static_cast<std::uint32_t>(texture.mips.size()));
    for (const TextureMip& mip : texture.mips)
    {
        writer.write(mip.width);
        writer.write(mip.height);
        writer.writeArray(std::span<const std::byte>(mip.bytes));
    }
    return writer.take();
}

core::Result<TextureData> decodeTexture(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::Texture); !header)
    {
        return std::unexpected(header.error());
    }

    TextureData texture;
    texture.format = reader.read<TextureFormat>();
    const auto mipCount = reader.read<std::uint32_t>();
    // A 64-bit texture cannot have more levels.
    if (mipCount > 64)
    {
        reader.fail();
    }
    for (std::uint32_t level = 0; level < mipCount && !reader.failed(); ++level)
    {
        TextureMip& mip = texture.mips.emplace_back();
        mip.width = reader.read<std::uint32_t>();
        mip.height = reader.read<std::uint32_t>();
        mip.bytes = reader.readArray<std::byte>();
    }
    if (reader.failed() || toString(texture.format) == "unknown")
    {
        return std::unexpected(truncated(AssetType::Texture));
    }
    if (core::Result<void> valid = validate(texture); !valid)
    {
        return std::unexpected(valid.error());
    }
    return texture;
}

std::vector<std::byte> encodeMaterial(const MaterialData& material)
{
    BinaryWriter writer = beginArtifact(AssetType::Material);
    writer.write(material.baseColorFactor);
    writer.write(material.baseColorTexture);
    writer.write(material.metallicFactor);
    writer.write(material.roughnessFactor);
    writer.write(material.metallicRoughnessTexture);
    writer.write(material.normalTexture);
    writer.write(material.normalScale);
    writer.write(material.occlusionTexture);
    writer.write(material.occlusionStrength);
    writer.write(material.emissiveFactor);
    writer.write(material.emissiveTexture);
    writer.write(material.alphaMode);
    writer.write(material.alphaCutoff);
    writer.write(static_cast<std::uint8_t>(material.doubleSided ? 1 : 0));
    return writer.take();
}

core::Result<MaterialData> decodeMaterial(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::Material); !header)
    {
        return std::unexpected(header.error());
    }

    MaterialData material;
    material.baseColorFactor = reader.read<math::Vec4>();
    material.baseColorTexture = reader.read<AssetId>();
    material.metallicFactor = reader.read<float>();
    material.roughnessFactor = reader.read<float>();
    material.metallicRoughnessTexture = reader.read<AssetId>();
    material.normalTexture = reader.read<AssetId>();
    material.normalScale = reader.read<float>();
    material.occlusionTexture = reader.read<AssetId>();
    material.occlusionStrength = reader.read<float>();
    material.emissiveFactor = reader.read<math::Vec3>();
    material.emissiveTexture = reader.read<AssetId>();
    material.alphaMode = reader.read<AlphaMode>();
    material.alphaCutoff = reader.read<float>();
    const auto doubleSided = reader.read<std::uint8_t>();
    material.doubleSided = doubleSided != 0;

    if (reader.failed() || doubleSided > 1 || toString(material.alphaMode) == "unknown")
    {
        return std::unexpected(truncated(AssetType::Material));
    }
    return material;
}

std::vector<std::byte> encodeModel(const ModelData& model)
{
    BinaryWriter writer = beginArtifact(AssetType::Model);
    writer.write(static_cast<std::uint32_t>(model.nodes.size()));
    for (const ModelNode& node : model.nodes)
    {
        writer.writeString(node.name);
        writer.write(node.parent);
        writer.write(node.translation);
        writeQuat(writer, node.rotation);
        writer.write(node.scale);
        writer.write(node.mesh);
    }
    return writer.take();
}

core::Result<ModelData> decodeModel(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::Model); !header)
    {
        return std::unexpected(header.error());
    }

    ModelData model;
    const auto nodeCount = reader.read<std::uint32_t>();
    // Each node takes at least its fixed-size fields.
    if (nodeCount > reader.remaining() / 64)
    {
        reader.fail();
    }
    for (std::uint32_t index = 0; index < nodeCount && !reader.failed(); ++index)
    {
        ModelNode& node = model.nodes.emplace_back();
        node.name = reader.readString();
        node.parent = reader.read<std::int32_t>();
        node.translation = reader.read<math::Vec3>();
        node.rotation = readQuat(reader);
        node.scale = reader.read<math::Vec3>();
        node.mesh = reader.read<AssetId>();
    }
    if (reader.failed())
    {
        return std::unexpected(truncated(AssetType::Model));
    }
    if (core::Result<void> valid = validate(model); !valid)
    {
        return std::unexpected(valid.error());
    }
    return model;
}

std::vector<std::byte> encodeScene(std::string_view text)
{
    BinaryWriter writer = beginArtifact(AssetType::Scene);
    writer.writeString(text);
    return writer.take();
}

core::Result<std::string> decodeScene(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::Scene); !header)
    {
        return std::unexpected(header.error());
    }
    std::string text = reader.readString();
    if (reader.failed())
    {
        return std::unexpected(truncated(AssetType::Scene));
    }
    return text;
}

} // namespace devex::asset
