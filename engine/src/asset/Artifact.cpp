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
    // 2: vertex tangents. 3: skinning weights and bind pose.
    // 4: the box that holds the mesh.
    case AssetType::Mesh:
        return 4;
    // 2: skins and animations.
    case AssetType::Model:
        return 2;
    case AssetType::Texture:
    case AssetType::Material:
    case AssetType::Scene:
    case AssetType::AudioClip:
    case AssetType::AnimationClip:
    // 2: the kerning pairs.
    case AssetType::Font:
        return 2;
    case AssetType::Theme:
        return 1;
    }
    return 0;
}

std::uint32_t artifactLayouts() noexcept
{
    std::uint32_t combined = 0;
    for (std::uint8_t value = static_cast<std::uint8_t>(AssetType::Mesh);
         value <= static_cast<std::uint8_t>(AssetType::Theme); ++value)
    {
        combined = combined * 31 + artifactVersion(static_cast<AssetType>(value));
    }
    return combined;
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
    writer.writeArray(std::span<const VertexSkin>(mesh.skin));
    writer.writeArray(std::span<const math::Mat4>(mesh.inverseBind));
    // An empty box is written as it is: the renderer measures the mesh when it loads it.
    writer.write(mesh.bounds.min);
    writer.write(mesh.bounds.max);
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
    mesh.skin = reader.readArray<VertexSkin>();
    mesh.inverseBind = reader.readArray<math::Mat4>();
    mesh.bounds.min = reader.read<math::Vec3>();
    mesh.bounds.max = reader.read<math::Vec3>();
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
        writer.write(node.skin);
    }
    writer.write(static_cast<std::uint32_t>(model.skins.size()));
    for (const ModelSkin& skin : model.skins)
    {
        writer.writeArray(std::span<const std::int32_t>(skin.joints));
    }
    writer.writeArray(std::span<const AssetId>(model.animations));
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
        node.skin = reader.read<std::int32_t>();
    }
    const auto skinCount = reader.read<std::uint32_t>();
    if (skinCount > reader.remaining() / sizeof(std::uint32_t))
    {
        reader.fail();
    }
    for (std::uint32_t index = 0; index < skinCount && !reader.failed(); ++index)
    {
        model.skins.emplace_back().joints = reader.readArray<std::int32_t>();
    }
    model.animations = reader.readArray<AssetId>();
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

std::vector<std::byte> encodeAudioClip(const AudioClipData& clip)
{
    BinaryWriter writer = beginArtifact(AssetType::AudioClip);
    writer.write(clip.encoding);
    writer.write(clip.loading);
    writer.write(clip.channels);
    writer.write(clip.sampleRate);
    writer.write(clip.frames);
    writer.writeArray(std::span<const std::uint8_t>(clip.waveform));
    // The file last, so that the description reads without it.
    writer.writeArray(std::span<const std::byte>(clip.encoded));
    return writer.take();
}

namespace {

[[nodiscard]] core::Result<AudioClipData> decodeAudioClip(std::span<const std::byte> bytes, bool withFile)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::AudioClip); !header)
    {
        return std::unexpected(header.error());
    }
    AudioClipData clip;
    clip.encoding = reader.read<AudioEncoding>();
    clip.loading = reader.read<AudioLoading>();
    clip.channels = reader.read<std::uint32_t>();
    clip.sampleRate = reader.read<std::uint32_t>();
    clip.frames = reader.read<std::uint64_t>();
    clip.waveform = reader.readArray<std::uint8_t>();
    if (withFile)
    {
        clip.encoded = reader.readArray<std::byte>();
    }
    const bool known = clip.encoding >= AudioEncoding::Wav && clip.encoding <= AudioEncoding::Vorbis &&
                       (clip.loading == AudioLoading::Decoded || clip.loading == AudioLoading::Streamed);
    if (reader.failed() || !known || clip.channels == 0 || clip.sampleRate == 0 || (withFile && clip.encoded.empty()))
    {
        return std::unexpected(truncated(AssetType::AudioClip));
    }
    return clip;
}

} // namespace

core::Result<AudioClipData> decodeAudioClip(std::span<const std::byte> bytes)
{
    return decodeAudioClip(bytes, true);
}

core::Result<AudioClipData> decodeAudioClipInfo(std::span<const std::byte> bytes)
{
    return decodeAudioClip(bytes, false);
}

std::string_view toString(AudioEncoding encoding) noexcept
{
    switch (encoding)
    {
    case AudioEncoding::Wav:
        return "WAV";
    case AudioEncoding::Flac:
        return "FLAC";
    case AudioEncoding::Mp3:
        return "MP3";
    case AudioEncoding::Vorbis:
        return "Ogg Vorbis";
    }
    return "unknown";
}

std::string_view toString(AudioLoading loading) noexcept
{
    return loading == AudioLoading::Streamed ? "streamed" : "decoded";
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

std::vector<std::byte> encodeAnimation(const AnimationClipData& clip)
{
    BinaryWriter writer = beginArtifact(AssetType::AnimationClip);
    writer.writeString(clip.name);
    writer.write(clip.duration);
    writer.write(static_cast<std::uint32_t>(clip.joints.size()));
    for (const std::string& joint : clip.joints)
    {
        writer.writeString(joint);
    }
    writer.write(static_cast<std::uint32_t>(clip.channels.size()));
    for (const AnimationChannel& channel : clip.channels)
    {
        writer.write(channel.joint);
        writer.write(static_cast<std::uint8_t>(channel.path));
        writer.write(static_cast<std::uint8_t>(channel.interpolation));
        writer.writeArray(std::span<const float>(channel.times));
        writer.writeArray(std::span<const float>(channel.values));
    }
    return writer.take();
}

core::Result<AnimationClipData> decodeAnimation(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::AnimationClip); !header)
    {
        return std::unexpected(header.error());
    }

    AnimationClipData clip;
    clip.name = reader.readString();
    clip.duration = reader.read<float>();
    const auto jointCount = reader.read<std::uint32_t>();
    if (jointCount > reader.remaining() / sizeof(std::uint32_t))
    {
        reader.fail();
    }
    for (std::uint32_t index = 0; index < jointCount && !reader.failed(); ++index)
    {
        clip.joints.push_back(reader.readString());
    }
    const auto channelCount = reader.read<std::uint32_t>();
    // Each channel takes at least its fixed-size fields and two empty arrays.
    if (channelCount > reader.remaining() / 14)
    {
        reader.fail();
    }
    for (std::uint32_t index = 0; index < channelCount && !reader.failed(); ++index)
    {
        AnimationChannel& channel = clip.channels.emplace_back();
        channel.joint = reader.read<std::uint32_t>();
        channel.path = static_cast<AnimationPath>(reader.read<std::uint8_t>());
        channel.interpolation = static_cast<AnimationInterpolation>(reader.read<std::uint8_t>());
        channel.times = reader.readArray<float>();
        channel.values = reader.readArray<float>();
    }
    if (reader.failed())
    {
        return std::unexpected(truncated(AssetType::AnimationClip));
    }
    if (core::Result<void> valid = validate(clip); !valid)
    {
        return std::unexpected(valid.error());
    }
    return clip;
}

std::vector<std::byte> encodeFont(const FontData& font)
{
    BinaryWriter writer = beginArtifact(AssetType::Font);
    writer.writeString(font.family);
    writer.write(font.bakedSize);
    writer.write(font.spread);
    writer.write(font.ascent);
    writer.write(font.descent);
    writer.write(font.lineGap);
    writer.write(font.atlasWidth);
    writer.write(font.atlasHeight);
    writer.writeArray(std::span<const std::uint8_t>(font.atlas));
    writer.writeArray(std::span<const FontGlyph>(font.glyphs));
    writer.writeArray(std::span<const FontKerning>(font.kerning));
    return writer.take();
}

core::Result<FontData> decodeFont(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::Font); !header)
    {
        return std::unexpected(header.error());
    }

    FontData font;
    font.family = reader.readString();
    font.bakedSize = reader.read<float>();
    font.spread = reader.read<float>();
    font.ascent = reader.read<float>();
    font.descent = reader.read<float>();
    font.lineGap = reader.read<float>();
    font.atlasWidth = reader.read<std::uint32_t>();
    font.atlasHeight = reader.read<std::uint32_t>();
    font.atlas = reader.readArray<std::uint8_t>();
    font.glyphs = reader.readArray<FontGlyph>();
    font.kerning = reader.readArray<FontKerning>();
    if (reader.failed())
    {
        return std::unexpected(truncated(AssetType::Font));
    }
    if (core::Result<void> valid = validate(font); !valid)
    {
        return std::unexpected(valid.error());
    }
    return font;
}

std::vector<std::byte> encodeTheme(const ThemeData& theme)
{
    BinaryWriter writer = beginArtifact(AssetType::Theme);
    writer.write(static_cast<std::uint32_t>(theme.styles.size()));
    for (const ThemeStyle& style : theme.styles)
    {
        writer.writeString(style.name);
        writer.write(static_cast<std::uint32_t>(style.values.size()));
        for (const ThemeOverride& value : style.values)
        {
            writer.writeString(value.component);
            writer.writeString(value.field);
            writer.writeString(value.value);
        }
    }
    return writer.take();
}

core::Result<ThemeData> decodeTheme(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    if (core::Result<void> header = readHeader(reader, AssetType::Theme); !header)
    {
        return std::unexpected(header.error());
    }

    ThemeData theme;
    const auto styles = reader.read<std::uint32_t>();
    for (std::uint32_t index = 0; index < styles && !reader.failed(); ++index)
    {
        ThemeStyle style;
        style.name = reader.readString();
        const auto values = reader.read<std::uint32_t>();
        for (std::uint32_t value = 0; value < values && !reader.failed(); ++value)
        {
            ThemeOverride written;
            written.component = reader.readString();
            written.field = reader.readString();
            written.value = reader.readString();
            style.values.push_back(std::move(written));
        }
        theme.styles.push_back(std::move(style));
    }
    if (reader.failed())
    {
        return std::unexpected(truncated(AssetType::Theme));
    }
    if (core::Result<void> valid = validate(theme); !valid)
    {
        return std::unexpected(valid.error());
    }
    return theme;
}

} // namespace devex::asset
