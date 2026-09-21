#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

namespace devex::asset {
namespace {

constexpr fastgltf::Extensions supportedExtensions =
    fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform |
    fastgltf::Extensions::KHR_materials_emissive_strength;

enum class TextureRole : std::uint8_t
{
    // sRGB colors: base color and emission.
    Color,
    // Linear values: metallic-roughness and occlusion.
    Data,
    Normal,
};

[[nodiscard]] std::string_view keySuffix(TextureRole role) noexcept
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

// Names used as keys in the .dvxmeta: the glTF name when present, a fallback otherwise, and the
// index appended to names shared by several elements.
[[nodiscard]] std::vector<std::string> uniqueKeys(std::vector<std::string> names)
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

[[nodiscard]] std::optional<AnimationPath> toAnimationPath(fastgltf::AnimationPath path) noexcept
{
    switch (path)
    {
    case fastgltf::AnimationPath::Translation:
        return AnimationPath::Translation;
    case fastgltf::AnimationPath::Rotation:
        return AnimationPath::Rotation;
    case fastgltf::AnimationPath::Scale:
        return AnimationPath::Scale;
    case fastgltf::AnimationPath::Weights:
        // Morph targets are not supported yet.
        break;
    }
    return std::nullopt;
}

[[nodiscard]] AnimationInterpolation toInterpolation(fastgltf::AnimationInterpolation interpolation) noexcept
{
    switch (interpolation)
    {
    case fastgltf::AnimationInterpolation::Step:
        return AnimationInterpolation::Step;
    case fastgltf::AnimationInterpolation::CubicSpline:
        return AnimationInterpolation::CubicSpline;
    case fastgltf::AnimationInterpolation::Linear:
        break;
    }
    return AnimationInterpolation::Linear;
}

[[nodiscard]] math::Mat4 toMat4(const fastgltf::math::fmat4x4& matrix) noexcept
{
    math::Mat4 result{1.0f};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result[static_cast<int>(column)][static_cast<int>(row)] = matrix.col(column)[row];
        }
    }
    return result;
}

[[nodiscard]] const fastgltf::Accessor* findAccessor(const fastgltf::Asset& asset,
                                                     const fastgltf::Primitive& primitive,
                                                     std::string_view attribute)
{
    const auto found = primitive.findAttribute(attribute);
    return found == primitive.attributes.end() ? nullptr : &asset.accessors[found->accessorIndex];
}

// Imports one primitive. A skinned mesh passes the inverse bind matrices of its skin, which the
// joints of its vertices refer to.
[[nodiscard]] core::Result<MeshData> importPrimitive(const fastgltf::Asset& asset,
                                                     const fastgltf::Primitive& primitive,
                                                     const std::vector<math::Mat4>& inverseBind)
{
    if (primitive.type != fastgltf::PrimitiveType::Triangles)
    {
        return core::makeError(core::ErrorCode::Unsupported, "only triangle lists are supported");
    }

    const fastgltf::Accessor* positions = findAccessor(asset, primitive, "POSITION");
    if (positions == nullptr)
    {
        return core::makeError(core::ErrorCode::Parse, "the primitive has no POSITION attribute");
    }

    MeshData mesh;
    mesh.vertices.resize(positions->count);
    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
        asset, *positions, [&mesh](fastgltf::math::fvec3 position, std::size_t index) {
            mesh.vertices[index].position = {position[0], position[1], position[2]};
        });

    const fastgltf::Accessor* normals = findAccessor(asset, primitive, "NORMAL");
    const bool hasNormals = normals != nullptr && normals->count == positions->count;
    if (hasNormals)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, *normals, [&mesh](fastgltf::math::fvec3 normal, std::size_t index) {
                mesh.vertices[index].normal = {normal[0], normal[1], normal[2]};
            });
    }

    const fastgltf::Accessor* uvs = findAccessor(asset, primitive, "TEXCOORD_0");
    if (uvs != nullptr && uvs->count == positions->count)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, *uvs, [&mesh](fastgltf::math::fvec2 uv, std::size_t index) {
                mesh.vertices[index].uv = {uv[0], uv[1]};
            });
    }

    // Tangents from the file are only meaningful with the normals they were made for.
    const fastgltf::Accessor* tangents = findAccessor(asset, primitive, "TANGENT");
    const bool hasTangents = hasNormals && tangents != nullptr && tangents->count == positions->count;
    if (hasTangents)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset, *tangents, [&mesh](fastgltf::math::fvec4 tangent, std::size_t index) {
                mesh.vertices[index].tangent = {tangent[0], tangent[1], tangent[2], tangent[3]};
            });
    }

    const fastgltf::Accessor* joints = findAccessor(asset, primitive, "JOINTS_0");
    const fastgltf::Accessor* weights = findAccessor(asset, primitive, "WEIGHTS_0");
    if (!inverseBind.empty() && joints != nullptr && weights != nullptr &&
        joints->count == positions->count && weights->count == positions->count)
    {
        mesh.inverseBind = inverseBind;
        mesh.skin.resize(positions->count);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(
            asset, *joints, [&mesh](fastgltf::math::uvec4 joint, std::size_t index) {
                for (std::size_t slot = 0; slot < 4; ++slot)
                {
                    mesh.skin[index].joints[slot] = static_cast<std::uint16_t>(joint[slot]);
                }
            });
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset, *weights, [&mesh](fastgltf::math::fvec4 weight, std::size_t index) {
                mesh.skin[index].weights = {weight[0], weight[1], weight[2], weight[3]};
            });
        // Weights are normalized here rather than in every shader that skins the mesh.
        for (VertexSkin& skin : mesh.skin)
        {
            const float sum = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
            skin.weights = sum > 0.0f ? skin.weights / sum : math::Vec4{1.0f, 0.0f, 0.0f, 0.0f};
            // A joint past the bind pose would read another skeleton's bone.
            for (std::uint16_t& joint : skin.joints)
            {
                joint = joint < inverseBind.size() ? joint : 0;
            }
        }
    }

    if (primitive.indicesAccessor)
    {
        const fastgltf::Accessor& indices = asset.accessors[*primitive.indicesAccessor];
        mesh.indices.resize(indices.count);
        fastgltf::iterateAccessorWithIndex<std::uint32_t>(
            asset, indices,
            [&mesh](std::uint32_t vertex, std::size_t index) { mesh.indices[index] = vertex; });
    }
    else
    {
        mesh.indices.resize(mesh.vertices.size());
        std::iota(mesh.indices.begin(), mesh.indices.end(), 0u);
    }

    if (core::Result<void> valid = validate(mesh); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!hasNormals)
    {
        computeNormals(mesh);
    }
    if (!hasTangents)
    {
        computeTangents(mesh);
    }
    return mesh;
}

// The encoded bytes of an image, wherever the file keeps them.
[[nodiscard]] core::Result<std::span<const std::byte>> imageBytes(const fastgltf::Asset& asset,
                                                                  const fastgltf::Image& image)
{
    const auto bytesOf =
        [](const fastgltf::DataSource& source) -> std::optional<std::span<const std::byte>> {
        if (const auto* array = std::get_if<fastgltf::sources::Array>(&source))
        {
            return std::span<const std::byte>(array->bytes.data(), array->bytes.size());
        }
        if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&source))
        {
            return std::span<const std::byte>(vector->bytes);
        }
        if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&source))
        {
            return std::span<const std::byte>(view->bytes.data(), view->bytes.size());
        }
        return std::nullopt;
    };

    if (const std::optional<std::span<const std::byte>> direct = bytesOf(image.data))
    {
        return *direct;
    }
    if (const auto* view = std::get_if<fastgltf::sources::BufferView>(&image.data))
    {
        const fastgltf::BufferView& bufferView = asset.bufferViews[view->bufferViewIndex];
        const std::optional<std::span<const std::byte>> buffer =
            bytesOf(asset.buffers[bufferView.bufferIndex].data);
        if (buffer && bufferView.byteOffset + bufferView.byteLength <= buffer->size())
        {
            return buffer->subspan(bufferView.byteOffset, bufferView.byteLength);
        }
    }
    return core::makeError(core::ErrorCode::Unsupported, "the image data is not available");
}

// Local files referred to by URI, collected before external data is loaded.
[[nodiscard]] std::vector<std::filesystem::path> externalFiles(const fastgltf::Asset& asset,
                                                               const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    const auto add = [&](const fastgltf::DataSource& source) {
        if (const auto* uri = std::get_if<fastgltf::sources::URI>(&source);
            uri != nullptr && uri->uri.isLocalPath())
        {
            files.push_back((directory / uri->uri.fspath()).lexically_normal());
        }
    };
    for (const fastgltf::Buffer& buffer : asset.buffers)
    {
        add(buffer.data);
    }
    for (const fastgltf::Image& image : asset.images)
    {
        add(image.data);
    }
    return files;
}

[[nodiscard]] std::vector<std::string> imageKeys(const fastgltf::Asset& asset)
{
    std::vector<std::string> names;
    for (std::size_t index = 0; index < asset.images.size(); ++index)
    {
        const fastgltf::Image& image = asset.images[index];
        std::string name(image.name);
        if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data);
            name.empty() && uri != nullptr && uri->uri.isLocalPath())
        {
            name = core::toUtf8(uri->uri.fspath().filename());
        }
        names.push_back(name.empty() ? std::format("Image {}", index) : std::move(name));
    }
    return uniqueKeys(std::move(names));
}

struct TextureUse
{
    std::size_t image = 0;
    TextureRole role = TextureRole::Color;

    auto operator<=>(const TextureUse&) const = default;
};

class GltfImport
{
public:
    GltfImport(ImportContext& context, const fastgltf::Asset& asset,
               std::vector<std::string> keysOfImages)
        : m_context(context)
        , m_asset(asset)
        , m_imageKeys(std::move(keysOfImages))
    {
    }

    [[nodiscard]] core::Result<ImportResult> run()
    {
        collectTextureUses();
        importTextures();
        if (m_context.isCancelled())
        {
            return core::makeError(core::ErrorCode::InvalidState, "the import was cancelled");
        }
        importMaterials();
        importMeshes();
        importModel();

        ImportResult result;
        // The model is the main asset and comes first.
        std::ranges::rotate(m_artifacts, m_artifacts.end() - 1);
        result.artifacts = std::move(m_artifacts);
        return result;
    }

private:
    [[nodiscard]] std::optional<std::size_t> imageOf(std::size_t texture) const
    {
        if (texture >= m_asset.textures.size())
        {
            return std::nullopt;
        }
        const fastgltf::Optional<std::size_t>& image = m_asset.textures[texture].imageIndex;
        return image && *image < m_asset.images.size() ? std::optional(*image) : std::nullopt;
    }

    void useTexture(const fastgltf::TextureInfo* info, TextureRole role)
    {
        if (info == nullptr)
        {
            return;
        }
        if (std::optional<std::size_t> image = imageOf(info->textureIndex))
        {
            m_textureIds.try_emplace({*image, role}, AssetId{});
        }
    }

    void collectTextureUses()
    {
        for (const fastgltf::Material& material : m_asset.materials)
        {
            const auto pointer = [](const auto& optional) {
                return optional ? &*optional : nullptr;
            };
            useTexture(pointer(material.pbrData.baseColorTexture), TextureRole::Color);
            useTexture(pointer(material.emissiveTexture), TextureRole::Color);
            useTexture(pointer(material.pbrData.metallicRoughnessTexture), TextureRole::Data);
            useTexture(pointer(material.occlusionTexture), TextureRole::Data);
            useTexture(pointer(material.normalTexture), TextureRole::Normal);
        }
    }

    void importTextures()
    {
        const std::optional<TextureQuality> quality =
            parseTextureQuality(m_context.stringOption("texture_quality", "normal"));
        const bool compress = m_context.boolOption("compress_textures", true);

        std::vector<TextureUse> uses;
        for (auto& [use, id] : m_textureIds)
        {
            id = m_context.subAssets.acquire(
                AssetType::Texture, m_imageKeys[use.image] + std::string(keySuffix(use.role)));
            uses.push_back(use);
        }

        std::vector<std::optional<ImportedArtifact>> built(uses.size());
        const auto build = [&](std::size_t index) {
            const TextureUse use = uses[index];
            const std::string& key = m_imageKeys[use.image];
            const core::Result<std::span<const std::byte>> bytes =
                imageBytes(m_asset, m_asset.images[use.image]);
            core::Result<Image> image = bytes ? decodeImage(*bytes)
                                              : core::Result<Image>(std::unexpected(bytes.error()));
            if (!image)
            {
                DEVEX_LOG_WARNING("Skipping image '{}' of '{}': {}", key,
                                  core::toUtf8(m_context.source.filename()), image.error());
                return;
            }
            const TextureBuildOptions options{
                .srgb = use.role == TextureRole::Color,
                .normalMap = use.role == TextureRole::Normal,
                .compress = compress,
                .quality = quality.value_or(TextureQuality::Normal),
            };
            core::Result<TextureData> texture =
                buildTexture(*image, options, m_context.jobs, m_context.cancelled);
            if (!texture)
            {
                if (!m_context.isCancelled())
                {
                    DEVEX_LOG_WARNING("Skipping image '{}' of '{}': {}", key,
                                      core::toUtf8(m_context.source.filename()), texture.error());
                }
                return;
            }
            const AssetId id = m_textureIds.at(use);
            built[index] = ImportedArtifact{id, AssetType::Texture,
                                            key + std::string(keySuffix(use.role)),
                                            encodeTexture(*texture)};
        };
        if (m_context.jobs != nullptr)
        {
            m_context.jobs->parallelFor(uses.size(), build);
        }
        else
        {
            for (std::size_t index = 0; index < uses.size(); ++index)
            {
                build(index);
            }
        }

        for (std::size_t index = 0; index < uses.size(); ++index)
        {
            if (built[index])
            {
                m_artifacts.push_back(std::move(*built[index]));
            }
            else
            {
                // Materials fall back to their factors for textures that could not be built.
                m_textureIds.at(uses[index]) = AssetId{};
            }
        }
    }

    [[nodiscard]] AssetId textureId(const fastgltf::TextureInfo* info, TextureRole role) const
    {
        if (info == nullptr)
        {
            return {};
        }
        const std::optional<std::size_t> image = imageOf(info->textureIndex);
        const auto found = image ? m_textureIds.find({*image, role}) : m_textureIds.end();
        return found != m_textureIds.end() ? found->second : AssetId{};
    }

    void importMaterials()
    {
        std::vector<std::string> names;
        for (std::size_t index = 0; index < m_asset.materials.size(); ++index)
        {
            const std::string name(m_asset.materials[index].name);
            names.push_back(name.empty() ? std::format("Material {}", index) : name);
        }
        const std::vector<std::string> keys = uniqueKeys(std::move(names));

        for (std::size_t index = 0; index < m_asset.materials.size(); ++index)
        {
            const fastgltf::Material& source = m_asset.materials[index];
            const auto pointer = [](const auto& optional) {
                return optional ? &*optional : nullptr;
            };

            MaterialData material;
            const auto& baseColor = source.pbrData.baseColorFactor;
            material.baseColorFactor = {baseColor[0], baseColor[1], baseColor[2], baseColor[3]};
            material.baseColorTexture =
                textureId(pointer(source.pbrData.baseColorTexture), TextureRole::Color);
            material.metallicFactor = source.pbrData.metallicFactor;
            material.roughnessFactor = source.pbrData.roughnessFactor;
            material.metallicRoughnessTexture =
                textureId(pointer(source.pbrData.metallicRoughnessTexture), TextureRole::Data);
            if (source.normalTexture)
            {
                material.normalTexture = textureId(&*source.normalTexture, TextureRole::Normal);
                material.normalScale = source.normalTexture->scale;
            }
            if (source.occlusionTexture)
            {
                material.occlusionTexture = textureId(&*source.occlusionTexture, TextureRole::Data);
                material.occlusionStrength = source.occlusionTexture->strength;
            }
            const auto& emissive = source.emissiveFactor;
            material.emissiveFactor =
                math::Vec3{emissive[0], emissive[1], emissive[2]} * source.emissiveStrength;
            material.emissiveTexture = textureId(pointer(source.emissiveTexture), TextureRole::Color);
            material.alphaMode = source.alphaMode == fastgltf::AlphaMode::Mask    ? AlphaMode::Mask
                                 : source.alphaMode == fastgltf::AlphaMode::Blend ? AlphaMode::Blend
                                                                                  : AlphaMode::Opaque;
            material.alphaCutoff = source.alphaCutoff;
            material.doubleSided = source.doubleSided;

            const AssetId id = m_context.subAssets.acquire(AssetType::Material, keys[index]);
            m_materialIds.push_back(id);
            m_artifacts.push_back({id, AssetType::Material, keys[index], encodeMaterial(material)});
        }
    }

    // The skin of each glTF mesh, taken from the first node that draws it with one.
    void findMeshSkins()
    {
        m_meshSkins.assign(m_asset.meshes.size(), -1);
        for (const fastgltf::Node& node : m_asset.nodes)
        {
            if (node.meshIndex && node.skinIndex && *node.meshIndex < m_meshSkins.size() &&
                m_meshSkins[*node.meshIndex] < 0)
            {
                m_meshSkins[*node.meshIndex] = static_cast<std::int32_t>(*node.skinIndex);
            }
        }

        m_inverseBinds.resize(m_asset.skins.size());
        for (std::size_t index = 0; index < m_asset.skins.size(); ++index)
        {
            const fastgltf::Skin& skin = m_asset.skins[index];
            std::vector<math::Mat4>& matrices = m_inverseBinds[index];
            matrices.assign(skin.joints.size(), math::Mat4{1.0f});
            if (!skin.inverseBindMatrices)
            {
                continue;
            }
            const fastgltf::Accessor& accessor = m_asset.accessors[*skin.inverseBindMatrices];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(
                m_asset, accessor, [&matrices](fastgltf::math::fmat4x4 matrix, std::size_t joint) {
                    if (joint < matrices.size())
                    {
                        matrices[joint] = toMat4(matrix);
                    }
                });
        }
    }

    void importMeshes()
    {
        findMeshSkins();
        std::vector<std::string> names;
        for (std::size_t index = 0; index < m_asset.meshes.size(); ++index)
        {
            const std::string name(m_asset.meshes[index].name);
            names.push_back(name.empty() ? std::format("Mesh {}", index) : name);
        }
        const std::vector<std::string> keys = uniqueKeys(std::move(names));
        const std::string fileName = core::toUtf8(m_context.source.filename());

        m_meshIds.resize(m_asset.meshes.size());
        for (std::size_t index = 0; index < m_asset.meshes.size(); ++index)
        {
            MeshData mesh;
            const fastgltf::Mesh& source = m_asset.meshes[index];
            const std::int32_t skin = m_meshSkins[index];
            const std::vector<math::Mat4>& inverseBind =
                skin >= 0 ? m_inverseBinds[static_cast<std::size_t>(skin)] : m_noBindPose;
            for (std::size_t primitive = 0; primitive < source.primitives.size(); ++primitive)
            {
                const fastgltf::Primitive& part = source.primitives[primitive];
                core::Result<MeshData> imported = importPrimitive(m_asset, part, inverseBind);
                if (!imported)
                {
                    DEVEX_LOG_WARNING("Skipping primitive {} of mesh '{}' in '{}': {}", primitive,
                                      keys[index], fileName, imported.error());
                    continue;
                }

                const auto baseVertex = static_cast<std::uint32_t>(mesh.vertices.size());
                const Submesh submesh{
                    .firstIndex = static_cast<std::uint32_t>(mesh.indices.size()),
                    .indexCount = static_cast<std::uint32_t>(imported->indices.size()),
                    .material = part.materialIndex && *part.materialIndex < m_materialIds.size()
                                    ? m_materialIds[*part.materialIndex]
                                    : AssetId{},
                };
                mesh.vertices.insert(mesh.vertices.end(), imported->vertices.begin(),
                                     imported->vertices.end());
                if (!inverseBind.empty())
                {
                    // Parts of a skinned mesh without joints follow its first one.
                    mesh.inverseBind = inverseBind;
                    mesh.skin.resize(mesh.vertices.size(), VertexSkin{.weights = {1.0f, 0.0f, 0.0f, 0.0f}});
                    for (std::size_t vertex = 0; vertex < imported->skin.size(); ++vertex)
                    {
                        mesh.skin[baseVertex + vertex] = imported->skin[vertex];
                    }
                }
                for (const std::uint32_t vertex : imported->indices)
                {
                    mesh.indices.push_back(baseVertex + vertex);
                }
                mesh.submeshes.push_back(submesh);
            }
            if (mesh.submeshes.empty())
            {
                continue;
            }

            const AssetId id = m_context.subAssets.acquire(AssetType::Mesh, keys[index]);
            m_meshIds[index] = id;
            m_artifacts.push_back({id, AssetType::Mesh, keys[index], encodeMesh(mesh)});
        }
    }

    void addNode(ModelData& model, std::size_t nodeIndex, std::int32_t parent,
                 std::vector<bool>& visited)
    {
        // glTF nodes form a forest; a malformed file must not loop forever.
        if (nodeIndex >= m_asset.nodes.size() || visited[nodeIndex])
        {
            return;
        }
        visited[nodeIndex] = true;
        const fastgltf::Node& source = m_asset.nodes[nodeIndex];

        ModelNode node;
        node.parent = parent;
        node.name = std::string(source.name);
        if (source.skinIndex)
        {
            node.skin = static_cast<std::int32_t>(*source.skinIndex);
        }
        if (source.meshIndex && *source.meshIndex < m_meshIds.size())
        {
            node.mesh = m_meshIds[*source.meshIndex];
            if (node.name.empty())
            {
                node.name = std::string(m_asset.meshes[*source.meshIndex].name);
            }
        }
        if (node.name.empty())
        {
            node.name = std::format("Node {}", nodeIndex);
        }

        if (const auto* trs = std::get_if<fastgltf::TRS>(&source.transform))
        {
            node.translation = {trs->translation[0], trs->translation[1], trs->translation[2]};
            // fastgltf stores quaternions as x, y, z, w; GLM takes w first.
            node.rotation =
                math::Quat{trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
            node.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
        }
        else if (const auto* matrix = std::get_if<fastgltf::math::fmat4x4>(&source.transform))
        {
            const math::Trs decomposed = math::decomposeTrs(toMat4(*matrix));
            node.translation = decomposed.translation;
            node.rotation = decomposed.rotation;
            node.scale = decomposed.scale;
        }

        const auto index = static_cast<std::int32_t>(model.nodes.size());
        m_nodeToModel[nodeIndex] = index;
        model.nodes.push_back(std::move(node));
        for (const std::size_t child : source.children)
        {
            addNode(model, child, index, visited);
        }
    }

    // Turns the skin of every node into a skin of the model, whose joints are model nodes.
    void resolveSkins(ModelData& model)
    {
        std::vector<std::int32_t> modelSkins(m_asset.skins.size(), -1);
        for (ModelNode& node : model.nodes)
        {
            if (node.skin < 0)
            {
                continue;
            }
            const auto gltfSkin = static_cast<std::size_t>(node.skin);
            node.skin = -1;
            if (gltfSkin >= m_asset.skins.size() || !node.mesh.isValid())
            {
                continue;
            }
            if (modelSkins[gltfSkin] < 0)
            {
                ModelSkin skin;
                for (const std::size_t joint : m_asset.skins[gltfSkin].joints)
                {
                    // A joint outside the imported scene leaves the bone at its bind pose.
                    skin.joints.push_back(joint < m_nodeToModel.size() ? m_nodeToModel[joint] : -1);
                }
                modelSkins[gltfSkin] = static_cast<std::int32_t>(model.skins.size());
                model.skins.push_back(std::move(skin));
            }
            node.skin = modelSkins[gltfSkin];
        }
    }

    // One AnimationClip asset per animation of the file. Channels target the nodes by name, so a
    // clip plays on any skeleton whose bones carry the same names.
    void importAnimations(ModelData& model)
    {
        std::vector<std::string> names;
        for (std::size_t index = 0; index < m_asset.animations.size(); ++index)
        {
            const std::string name(m_asset.animations[index].name);
            names.push_back(name.empty() ? std::format("Animation {}", index) : name);
        }
        const std::vector<std::string> keys = uniqueKeys(std::move(names));
        const std::string fileName = core::toUtf8(m_context.source.filename());

        for (std::size_t index = 0; index < m_asset.animations.size(); ++index)
        {
            const fastgltf::Animation& source = m_asset.animations[index];
            AnimationClipData clip;
            clip.name = keys[index];
            // The joints of the clip, by name, and where each node lands among them.
            std::unordered_map<std::size_t, std::uint32_t> clipJoints;
            for (const fastgltf::AnimationChannel& channel : source.channels)
            {
                const std::optional<AnimationPath> path = toAnimationPath(channel.path);
                const std::int32_t node = channel.nodeIndex && *channel.nodeIndex < m_nodeToModel.size()
                                              ? m_nodeToModel[*channel.nodeIndex]
                                              : -1;
                if (!path || node < 0 || channel.samplerIndex >= source.samplers.size())
                {
                    continue;
                }
                const fastgltf::AnimationSampler& sampler = source.samplers[channel.samplerIndex];
                if (sampler.inputAccessor >= m_asset.accessors.size() ||
                    sampler.outputAccessor >= m_asset.accessors.size())
                {
                    continue;
                }

                AnimationChannel imported;
                imported.path = *path;
                imported.interpolation = toInterpolation(sampler.interpolation);
                const fastgltf::Accessor& times = m_asset.accessors[sampler.inputAccessor];
                imported.times.reserve(times.count);
                fastgltf::iterateAccessor<float>(m_asset, times, [&imported](float time) {
                    imported.times.push_back(time);
                });
                const fastgltf::Accessor& values = m_asset.accessors[sampler.outputAccessor];
                if (*path == AnimationPath::Rotation)
                {
                    fastgltf::iterateAccessor<fastgltf::math::fvec4>(
                        m_asset, values, [&imported](fastgltf::math::fvec4 value) {
                            for (const float component : {value[0], value[1], value[2], value[3]})
                            {
                                imported.values.push_back(component);
                            }
                        });
                }
                else
                {
                    fastgltf::iterateAccessor<fastgltf::math::fvec3>(
                        m_asset, values, [&imported](fastgltf::math::fvec3 value) {
                            for (const float component : {value[0], value[1], value[2]})
                            {
                                imported.values.push_back(component);
                            }
                        });
                }

                const auto [joint, inserted] = clipJoints.try_emplace(
                    static_cast<std::size_t>(node), static_cast<std::uint32_t>(clip.joints.size()));
                if (inserted)
                {
                    clip.joints.push_back(model.nodes[static_cast<std::size_t>(node)].name);
                }
                imported.joint = joint->second;
                clip.duration = std::max(clip.duration, imported.times.empty() ? 0.0f : imported.times.back());
                clip.channels.push_back(std::move(imported));
            }

            if (core::Result<void> valid = validate(clip); !valid)
            {
                DEVEX_LOG_WARNING("Skipping animation '{}' of '{}': {}", keys[index], fileName,
                                  valid.error());
                continue;
            }
            const AssetId id = m_context.subAssets.acquire(AssetType::AnimationClip, keys[index]);
            model.animations.push_back(id);
            m_artifacts.push_back({id, AssetType::AnimationClip, keys[index], encodeAnimation(clip)});
        }
    }

    void importModel()
    {
        ModelData model;
        m_nodeToModel.assign(m_asset.nodes.size(), -1);
        std::vector<bool> visited(m_asset.nodes.size(), false);
        const std::size_t sceneIndex = m_asset.defaultScene ? *m_asset.defaultScene : 0;
        if (sceneIndex < m_asset.scenes.size())
        {
            for (const std::size_t root : m_asset.scenes[sceneIndex].nodeIndices)
            {
                addNode(model, root, -1, visited);
            }
        }
        else
        {
            // Without scenes, every node that is nobody's child is a root.
            std::vector<bool> isChild(m_asset.nodes.size(), false);
            for (const fastgltf::Node& node : m_asset.nodes)
            {
                for (const std::size_t child : node.children)
                {
                    if (child < isChild.size())
                    {
                        isChild[child] = true;
                    }
                }
            }
            for (std::size_t node = 0; node < m_asset.nodes.size(); ++node)
            {
                if (!isChild[node])
                {
                    addNode(model, node, -1, visited);
                }
            }
        }
        resolveSkins(model);
        importAnimations(model);
        m_artifacts.push_back({m_context.mainId, AssetType::Model, m_context.name, encodeModel(model)});
    }

    ImportContext& m_context;
    const fastgltf::Asset& m_asset;
    std::vector<std::string> m_imageKeys;
    std::map<TextureUse, AssetId> m_textureIds;
    std::vector<AssetId> m_materialIds;
    std::vector<AssetId> m_meshIds;
    // The skin of each glTF mesh, and the bind pose of each glTF skin.
    std::vector<std::int32_t> m_meshSkins;
    std::vector<std::vector<math::Mat4>> m_inverseBinds;
    const std::vector<math::Mat4> m_noBindPose;
    // Where each glTF node landed among the nodes of the model, or -1.
    std::vector<std::int32_t> m_nodeToModel;
    std::vector<ImportedArtifact> m_artifacts;
};

[[nodiscard]] core::Result<fastgltf::Asset> parse(std::span<const std::byte> file,
                                                  const std::filesystem::path& path,
                                                  fastgltf::Options options)
{
    fastgltf::Expected<fastgltf::GltfDataBuffer> data =
        fastgltf::GltfDataBuffer::FromBytes(file.data(), file.size());
    if (data.error() != fastgltf::Error::None)
    {
        return core::makeError(core::ErrorCode::OutOfMemory, "cannot buffer '{}': {}",
                               core::toUtf8(path), fastgltf::getErrorMessage(data.error()));
    }
    fastgltf::Parser parser(supportedExtensions);
    fastgltf::Expected<fastgltf::Asset> asset =
        parser.loadGltf(data.get(), path.parent_path(), options);
    if (asset.error() != fastgltf::Error::None)
    {
        return core::makeError(core::ErrorCode::Parse, "cannot parse '{}': {}", core::toUtf8(path),
                               fastgltf::getErrorMessage(asset.error()));
    }
    return std::move(asset.get());
}

} // namespace

core::Result<std::vector<std::filesystem::path>> findGltfDependencies(
    const std::filesystem::path& file)
{
    const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(file);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    const core::Result<fastgltf::Asset> description = parse(*bytes, file, fastgltf::Options::None);
    if (!description)
    {
        return std::unexpected(description.error());
    }
    return externalFiles(*description, file.parent_path());
}

core::Result<ImportResult> importGltfFile(ImportContext& context)
{
    const core::Result<std::vector<std::byte>> file = core::readBinaryFile(context.source);
    if (!file)
    {
        return std::unexpected(file.error());
    }

    // A first pass without external data finds the files the model depends on and the names of
    // its images.
    core::Result<fastgltf::Asset> description =
        parse(*file, context.source, fastgltf::Options::None);
    if (!description)
    {
        return std::unexpected(description.error());
    }
    std::vector<std::filesystem::path> dependencies =
        externalFiles(*description, context.source.parent_path());
    std::vector<std::string> keysOfImages = imageKeys(*description);

    core::Result<fastgltf::Asset> asset =
        parse(*file, context.source,
              fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);
    if (!asset)
    {
        return std::unexpected(asset.error());
    }

    GltfImport import(context, *asset, std::move(keysOfImages));
    core::Result<ImportResult> result = import.run();
    if (result)
    {
        result->dependencies = std::move(dependencies);
    }
    return result;
}

} // namespace devex::asset
