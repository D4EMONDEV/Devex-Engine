#include "ModelImport.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace devex::asset {
namespace {

using detail::keySuffix;
using detail::TextureRole;
using detail::uniqueKeys;

struct SceneDeleter
{
    void operator()(ufbx_scene* scene) const noexcept { ufbx_free_scene(scene); }
};

struct BakedAnimationDeleter
{
    void operator()(ufbx_baked_anim* animation) const noexcept { ufbx_free_baked_anim(animation); }
};

[[nodiscard]] std::string toString(ufbx_string text)
{
    return std::string(text.data, text.length);
}

[[nodiscard]] math::Vec3 toVec3(const ufbx_vec3& vector) noexcept
{
    return {static_cast<float>(vector.x), static_cast<float>(vector.y), static_cast<float>(vector.z)};
}

[[nodiscard]] math::Quat toQuat(const ufbx_quat& quaternion) noexcept
{
    return math::Quat{static_cast<float>(quaternion.w), static_cast<float>(quaternion.x),
                      static_cast<float>(quaternion.y), static_cast<float>(quaternion.z)};
}

// ufbx matrices are affine: four columns of three rows.
[[nodiscard]] math::Mat4 toMat4(const ufbx_matrix& matrix) noexcept
{
    math::Mat4 result{1.0f};
    for (int column = 0; column < 4; ++column)
    {
        result[column] = math::Vec4{toVec3(matrix.cols[column]), column == 3 ? 1.0f : 0.0f};
    }
    return result;
}

// A path as the file writes it, with the separators of the system it was saved on.
[[nodiscard]] std::filesystem::path writtenPath(ufbx_string text)
{
    std::string path = toString(text);
    std::ranges::replace(path, '\\', '/');
    return core::pathFromUtf8(path);
}

[[nodiscard]] bool isObjFile(const std::filesystem::path& path)
{
    std::string extension = core::toUtf8(path.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".obj";
}

struct LoadedScene
{
    std::unique_ptr<ufbx_scene, SceneDeleter> scene;
    // Files ufbx opened besides the model, such as the .mtl of an .obj file.
    std::vector<std::filesystem::path> openedFiles;
};

bool openFile(void* user, ufbx_stream* stream, const char* path, std::size_t length,
              const ufbx_open_file_info* info)
{
    if (!ufbx_default_open_file(nullptr, stream, path, length, info))
    {
        return false;
    }
    if (info->type != UFBX_OPEN_FILE_MAIN_MODEL)
    {
        static_cast<std::vector<std::filesystem::path>*>(user)->push_back(
            core::pathFromUtf8(std::string_view(path, length)).lexically_normal());
    }
    return true;
}

// Loads the file in the engine conventions: Y up, right-handed, one unit per meter times the
// scale of the import. Without content, only what the file refers to is read.
[[nodiscard]] core::Result<LoadedScene> loadScene(const std::filesystem::path& path, double scale,
                                                  bool content)
{
    std::vector<std::filesystem::path> openedFiles;
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = static_cast<ufbx_real>(1.0 / scale);
    // The conversion goes into the geometry and the transforms, so that nodes keep a scale of one.
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    // Nodes of the engine have no transform applying to their mesh alone, and inherit the whole
    // transform of their parent.
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    options.generate_missing_normals = true;
    options.normalize_normals = true;
    options.clean_skin_weights = true;
    // Blender writes its materials as Phong ones in a way that can be read back.
    options.use_blender_pbr_material = true;
    // .obj files do not say their units and axes: they are taken as the engine's, and their .mtl
    // is opened, even when named after the file rather than by it.
    options.obj_unit_meters = 1.0;
    options.obj_axes = ufbx_axes_right_handed_y_up;
    options.obj_search_mtl_by_filename = true;
    options.load_external_files = isObjFile(path);
    options.ignore_missing_external_files = true;
    // The hierarchy is walked recursively.
    options.node_depth_limit = 1024;
    options.ignore_geometry = !content;
    options.ignore_animation = !content;
    options.open_file_cb.fn = &openFile;
    options.open_file_cb.user = &openedFiles;

    ufbx_error error{};
    const std::string file = core::toUtf8(path);
    LoadedScene loaded;
    loaded.scene.reset(ufbx_load_file_len(file.data(), file.size(), &options, &error));
    if (!loaded.scene)
    {
        std::string reason = toString(error.description);
        if (error.info_length > 0)
        {
            reason += std::format(" ({})", std::string_view(error.info, error.info_length));
        }
        return core::makeError(error.type == UFBX_ERROR_FILE_NOT_FOUND ? core::ErrorCode::NotFound
                                                                       : core::ErrorCode::Parse,
                               "cannot load '{}': {}", core::toUtf8(path.filename()), reason);
    }
    loaded.openedFiles = std::move(openedFiles);
    return loaded;
}

// An image the materials of the file use: embedded in it, or a file of its own.
struct SceneImage
{
    std::string key;
    std::span<const std::byte> content;
    // The file of an image that is not embedded, when it is found.
    std::optional<std::filesystem::path> path;
};

[[nodiscard]] std::vector<SceneImage> findImages(const ufbx_scene& scene,
                                                 const std::filesystem::path& directory)
{
    std::vector<SceneImage> images;
    std::vector<std::string> names;
    for (const ufbx_texture_file& file : scene.texture_files)
    {
        const std::filesystem::path relative = writtenPath(file.relative_filename);
        const std::filesystem::path written = relative.empty() ? writtenPath(file.absolute_filename) : relative;
        std::string name = core::toUtf8(written.filename());
        names.push_back(name.empty() ? std::format("Image {}", file.index) : std::move(name));

        SceneImage image;
        if (file.content.size > 0)
        {
            image.content = {static_cast<const std::byte*>(file.content.data), file.content.size};
        }
        else
        {
            // The path relative to the model, then the name alone beside it: a file saved on
            // another machine often keeps only the name of its images. Absolute paths are not
            // followed, since they seldom exist anywhere else.
            std::vector<std::filesystem::path> candidates;
            if (!relative.empty() && relative.is_relative())
            {
                candidates.push_back((directory / relative).lexically_normal());
            }
            if (!written.filename().empty())
            {
                candidates.push_back(directory / written.filename());
            }
            for (const std::filesystem::path& candidate : candidates)
            {
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate, error))
                {
                    image.path = candidate;
                    break;
                }
            }
        }
        images.push_back(std::move(image));
    }
    std::vector<std::string> keys = uniqueKeys(std::move(names));
    for (std::size_t index = 0; index < images.size(); ++index)
    {
        images[index].key = std::move(keys[index]);
    }
    return images;
}

// The files of the images and the .mtl the import reads, which import the model again when they
// change.
[[nodiscard]] std::vector<std::filesystem::path> dependenciesOf(const std::vector<SceneImage>& images,
                                                                std::vector<std::filesystem::path> files)
{
    for (const SceneImage& image : images)
    {
        if (image.path)
        {
            files.push_back(*image.path);
        }
    }
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());
    return files;
}

[[nodiscard]] core::Result<Image> decodeSceneImage(const SceneImage& image)
{
    if (!image.content.empty())
    {
        return decodeImage(image.content);
    }
    if (!image.path)
    {
        return core::makeError(core::ErrorCode::NotFound, "the file of the image is not found");
    }
    const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(*image.path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    return decodeImage(*bytes);
}

struct TextureUse
{
    // Index into the images of the scene.
    std::uint32_t image = 0;
    TextureRole role = TextureRole::Color;

    auto operator<=>(const TextureUse&) const = default;
};

// Roughness and metalness, which FBX files keep in maps of their own, packed the way materials read
// them: roughness in green, metalness in blue.
struct PackedUse
{
    std::optional<std::uint32_t> roughness;
    std::optional<std::uint32_t> metalness;
    // The roughness map holds glossiness, its inverse.
    bool glossiness = false;

    auto operator<=>(const PackedUse&) const = default;
};

// The images a material reads.
struct MaterialImages
{
    std::optional<std::uint32_t> baseColor;
    std::optional<std::uint32_t> opacity;
    std::optional<std::uint32_t> normal;
    std::optional<std::uint32_t> occlusion;
    std::optional<std::uint32_t> emission;
    PackedUse metallicRoughness;
};

// Where a vertex attribute is found in a corner, whose floats are compared byte for byte to merge
// identical corners into one vertex: position, normal and uv, then four joints and four weights.
constexpr std::size_t cornerFloats = 16;
constexpr std::size_t cornerJoints = 8;
constexpr std::size_t cornerWeights = 12;

// One mesh of the file, a submesh per material. Faces are triangulated, and skinned vertices keep
// the four joints that weigh the most. The scale baked into the nodes that show the mesh multiplies
// its positions, and the scales baked into its bones go into their bind pose.
[[nodiscard]] core::Result<MeshData> importMesh(const ufbx_mesh& mesh, std::span<const AssetId> materials,
                                                float scale, std::span<const float> nodeScales)
{
    const ufbx_skin_deformer* const skin = mesh.skin_deformers.count > 0 ? mesh.skin_deformers.data[0] : nullptr;
    std::vector<float> corners;
    corners.reserve(mesh.num_triangles * 3 * cornerFloats);
    std::vector<std::uint32_t> triangles(mesh.max_face_triangles * 3);
    std::vector<Submesh> submeshes;
    std::uint32_t cornerCount = 0;

    for (const ufbx_mesh_part& part : mesh.material_parts)
    {
        if (part.num_triangles == 0)
        {
            continue;
        }
        // A node may give its instance of the mesh other materials, which the model does not keep.
        const ufbx_material* const material = part.index < mesh.materials.count ? mesh.materials.data[part.index] : nullptr;
        Submesh submesh{
            .firstIndex = cornerCount,
            .material = material != nullptr && material->typed_id < materials.size() ? materials[material->typed_id] : AssetId{},
        };
        for (const std::uint32_t face : part.face_indices)
        {
            const std::uint32_t count =
                ufbx_triangulate_face(triangles.data(), triangles.size(), &mesh, mesh.faces.data[face]);
            for (std::size_t corner = 0; corner < std::size_t{count} * 3; ++corner)
            {
                const std::uint32_t index = triangles[corner];
                const math::Vec3 position = toVec3(ufbx_get_vertex_vec3(&mesh.vertex_position, index)) * scale;
                const math::Vec3 normal = mesh.vertex_normal.exists
                                              ? toVec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, index))
                                              : math::Vec3{0.0f, 1.0f, 0.0f};
                const ufbx_vec2 uv = mesh.vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh.vertex_uv, index) : ufbx_vec2{};
                std::array<float, cornerFloats> values{
                    position.x, position.y, position.z, normal.x, normal.y, normal.z,
                    // FBX puts the origin of texture coordinates at the bottom-left corner.
                    static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y),
                };
                const std::uint32_t vertex = mesh.vertex_indices.data[index];
                if (skin != nullptr && vertex < skin->vertices.count)
                {
                    // Weights are sorted from the heaviest.
                    const ufbx_skin_vertex& influences = skin->vertices.data[vertex];
                    for (std::uint32_t slot = 0; slot < std::min(influences.num_weights, 4u); ++slot)
                    {
                        const ufbx_skin_weight& weight = skin->weights.data[influences.weight_begin + slot];
                        values[cornerJoints + slot] = static_cast<float>(weight.cluster_index);
                        values[cornerWeights + slot] = static_cast<float>(weight.weight);
                    }
                }
                corners.insert(corners.end(), values.begin(), values.end());
            }
            cornerCount += count * 3;
        }
        submesh.indexCount = cornerCount - submesh.firstIndex;
        submeshes.push_back(submesh);
    }
    if (cornerCount == 0)
    {
        return core::makeError(core::ErrorCode::Unsupported, "the mesh has no triangles");
    }

    MeshData result;
    result.indices.resize(cornerCount);
    ufbx_vertex_stream stream{corners.data(), cornerCount, cornerFloats * sizeof(float)};
    ufbx_error error{};
    const std::size_t vertexCount =
        ufbx_generate_indices(&stream, 1, result.indices.data(), result.indices.size(), nullptr, &error);
    if (error.type != UFBX_ERROR_NONE)
    {
        return core::makeError(core::ErrorCode::Parse, "cannot index the mesh: {}", toString(error.description));
    }
    result.submeshes = std::move(submeshes);
    result.vertices.resize(vertexCount);
    for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
    {
        const float* const values = corners.data() + vertex * cornerFloats;
        result.vertices[vertex].position = {values[0], values[1], values[2]};
        result.vertices[vertex].normal = {values[3], values[4], values[5]};
        result.vertices[vertex].uv = {values[6], values[7]};
    }

    if (skin != nullptr && skin->clusters.count > 0)
    {
        for (const ufbx_skin_cluster* cluster : skin->clusters)
        {
            // From the scaled mesh back to the original one, through the bind pose, to the bone
            // without its baked scale.
            const float boneScale = cluster->bone_node != nullptr ? nodeScales[cluster->bone_node->typed_id] : 1.0f;
            result.inverseBind.push_back(math::scale(math::Mat4{1.0f}, math::Vec3{boneScale}) *
                                         toMat4(cluster->geometry_to_bone) *
                                         math::scale(math::Mat4{1.0f}, math::Vec3{1.0f / scale}));
        }
        result.skin.resize(vertexCount);
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
        {
            const float* const values = corners.data() + vertex * cornerFloats;
            VertexSkin& skinned = result.skin[vertex];
            for (std::size_t slot = 0; slot < 4; ++slot)
            {
                const auto joint = static_cast<std::size_t>(values[cornerJoints + slot]);
                // A joint past the bind pose would read another skeleton's bone.
                skinned.joints[slot] = static_cast<std::uint16_t>(joint < result.inverseBind.size() ? joint : 0);
                skinned.weights[static_cast<int>(slot)] = values[cornerWeights + slot];
            }
            // Weights are normalized here rather than in every shader that skins the mesh.
            const float sum = skinned.weights.x + skinned.weights.y + skinned.weights.z + skinned.weights.w;
            skinned.weights = sum > 0.0f ? skinned.weights / sum : math::Vec4{1.0f, 0.0f, 0.0f, 0.0f};
        }
    }

    if (core::Result<void> valid = validate(result); !valid)
    {
        return std::unexpected(valid.error());
    }
    computeTangents(result);
    result.bounds = computeBounds(result);
    return result;
}

// The keys of a baked translation or scale, times the factor of a baked scale, or nothing when they
// hold the pose the node rests in.
[[nodiscard]] std::optional<AnimationChannel> vectorChannel(const ufbx_baked_vec3_list& keys, bool constant,
                                                            math::Vec3 rest, AnimationPath path, float factor)
{
    if (keys.count == 0 ||
        (constant && math::length(toVec3(keys.data[0].value) * factor - rest) < 1e-5f * std::max(1.0f, math::length(rest))))
    {
        return std::nullopt;
    }
    AnimationChannel channel;
    channel.path = path;
    for (const ufbx_baked_vec3& key : keys)
    {
        const auto time = static_cast<float>(key.time);
        const math::Vec3 value = toVec3(key.value) * factor;
        // Keys closer than a float tells apart become one.
        if (!channel.times.empty() && time <= channel.times.back())
        {
            channel.values.resize(channel.values.size() - 3);
        }
        else
        {
            channel.times.push_back(time);
        }
        channel.values.insert(channel.values.end(), {value.x, value.y, value.z});
    }
    return channel;
}

[[nodiscard]] std::optional<AnimationChannel> rotationChannel(const ufbx_baked_quat_list& keys, bool constant,
                                                              math::Quat rest)
{
    if (keys.count == 0 || (constant && std::abs(math::dot(toQuat(keys.data[0].value), rest)) > 1.0f - 1e-6f))
    {
        return std::nullopt;
    }
    AnimationChannel channel;
    channel.path = AnimationPath::Rotation;
    math::Quat previous = rest;
    for (const ufbx_baked_quat& key : keys)
    {
        const auto time = static_cast<float>(key.time);
        math::Quat value = toQuat(key.value);
        // Consecutive keys on the same side of the sphere turn the short way.
        if (!channel.times.empty() && math::dot(previous, value) < 0.0f)
        {
            value = -value;
        }
        previous = value;
        if (!channel.times.empty() && time <= channel.times.back())
        {
            channel.values.resize(channel.values.size() - 4);
        }
        else
        {
            channel.times.push_back(time);
        }
        channel.values.insert(channel.values.end(), {value.x, value.y, value.z, value.w});
    }
    return channel;
}

class FbxImport
{
public:
    FbxImport(ImportContext& context, const ufbx_scene& scene, const std::vector<SceneImage>& images)
        : m_context(context)
        , m_scene(scene)
        , m_images(images)
    {
    }

    [[nodiscard]] core::Result<ImportResult> run()
    {
        findBakedScales();
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
    [[nodiscard]] static bool isRootNode(const ufbx_node& node) noexcept
    {
        return node.parent != nullptr && node.parent->is_root;
    }

    // Whether an animation changes the scale of the node. Exporters often key every property of an
    // object, a still scale included.
    [[nodiscard]] bool isScaleAnimated(const ufbx_node& node) const
    {
        const math::Vec3 rest = toVec3(node.local_transform.scale);
        return std::ranges::any_of(m_scene.anim_layers, [&](const ufbx_anim_layer* layer) {
            const ufbx_anim_prop* const prop = ufbx_find_anim_prop(layer, &node.element, UFBX_Lcl_Scaling);
            if (prop == nullptr || prop->anim_value == nullptr)
            {
                return false;
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                const ufbx_anim_curve* const curve = prop->anim_value->curves[axis];
                if (curve != nullptr && curve->keyframes.count > 0 &&
                    (curve->max_value - curve->min_value > 1e-4 * std::abs(rest[axis]) ||
                     std::abs(static_cast<float>(curve->max_value) - rest[axis]) > 1e-4f * std::abs(rest[axis])))
                {
                    return true;
                }
            }
            return false;
        });
    }

    void setSubtreeScale(const ufbx_node& node, float scale)
    {
        m_nodeScales[node.typed_id] = scale;
        for (const ufbx_node* child : node.children)
        {
            setSubtreeScale(*child, scale);
        }
    }

    // Blender writes its objects with a scale of 100 that makes up for the unit of its files, the
    // centimeter. The still, uniform scale of every root node is baked instead: into the positions of
    // the meshes below it and the translations of its descendants, which commutes with their
    // rotations. The nodes then keep a scale of one, as those of glTF files do.
    void findBakedScales()
    {
        m_nodeScales.assign(m_scene.nodes.count, 1.0f);
        for (const ufbx_node* root : m_scene.root_node->children)
        {
            const math::Vec3 scale = toVec3(root->local_transform.scale);
            const bool uniform = std::abs(scale.x - scale.y) <= 1e-4f * scale.x &&
                                 std::abs(scale.x - scale.z) <= 1e-4f * scale.x;
            if (uniform && scale.x > 0.0f && std::abs(scale.x - 1.0f) > 1e-5f && !isScaleAnimated(*root))
            {
                setSubtreeScale(*root, scale.x);
            }
        }

        // A mesh shown below roots of different scales cannot take one of them: those roots keep
        // their scale, which may in turn part the instances of other meshes.
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const ufbx_mesh* mesh : m_scene.meshes)
            {
                const bool parted = std::ranges::any_of(mesh->instances, [&](const ufbx_node* instance) {
                    return m_nodeScales[instance->typed_id] != m_nodeScales[mesh->instances.data[0]->typed_id];
                });
                if (!parted)
                {
                    continue;
                }
                for (const ufbx_node* instance : mesh->instances)
                {
                    const ufbx_node* root = instance;
                    while (!isRootNode(*root) && root->parent != nullptr)
                    {
                        root = root->parent;
                    }
                    if (m_nodeScales[root->typed_id] != 1.0f)
                    {
                        setSubtreeScale(*root, 1.0f);
                        changed = true;
                    }
                }
            }
        }
        m_meshScales.clear();
        for (const ufbx_mesh* mesh : m_scene.meshes)
        {
            m_meshScales.push_back(mesh->instances.count > 0 ? m_nodeScales[mesh->instances.data[0]->typed_id] : 1.0f);
        }
    }

    [[nodiscard]] std::optional<std::uint32_t> imageOf(const ufbx_material_map& map) const
    {
        const ufbx_texture* texture = map.texture_enabled ? map.texture : nullptr;
        // Layered and shader textures are read as their first image.
        if (texture != nullptr && texture->type != UFBX_TEXTURE_FILE)
        {
            texture = texture->file_textures.count > 0 ? texture->file_textures.data[0] : nullptr;
        }
        if (texture == nullptr || !texture->has_file || texture->file_index >= m_images.size())
        {
            return std::nullopt;
        }
        return texture->file_index;
    }

    [[nodiscard]] MaterialImages imagesOf(const ufbx_material& material) const
    {
        const ufbx_material_pbr_maps& pbr = material.pbr;
        const ufbx_material_fbx_maps& fbx = material.fbx;
        MaterialImages images;
        images.baseColor = imageOf(pbr.base_color);
        // Phong materials keep what hides their surface as a transparency.
        images.opacity = imageOf(pbr.opacity);
        images.opacity = images.opacity ? images.opacity : imageOf(fbx.transparency_color);
        images.opacity = images.opacity ? images.opacity : imageOf(fbx.transparency_factor);
        images.normal = imageOf(pbr.normal_map);
        images.normal = images.normal ? images.normal : imageOf(fbx.normal_map);
        images.occlusion = imageOf(pbr.ambient_occlusion);
        images.emission = imageOf(pbr.emission_color);
        images.metallicRoughness.roughness = imageOf(pbr.roughness);
        if (!images.metallicRoughness.roughness)
        {
            images.metallicRoughness.roughness = imageOf(pbr.glossiness);
            images.metallicRoughness.glossiness = images.metallicRoughness.roughness.has_value();
        }
        images.metallicRoughness.metalness = imageOf(pbr.metalness);
        return images;
    }

    // One image for both is taken as already packed, as glTF packs them.
    [[nodiscard]] static bool isPacked(const PackedUse& use) noexcept
    {
        return use.roughness && use.roughness == use.metalness && !use.glossiness;
    }

    void useTexture(std::optional<std::uint32_t> image, TextureRole role)
    {
        if (image)
        {
            m_textureIds.try_emplace({*image, role}, AssetId{});
        }
    }

    void collectTextureUses()
    {
        for (const ufbx_material* material : m_scene.materials)
        {
            const MaterialImages images = imagesOf(*material);
            useTexture(images.baseColor, TextureRole::Color);
            useTexture(images.emission, TextureRole::Color);
            useTexture(images.occlusion, TextureRole::Data);
            useTexture(images.normal, TextureRole::Normal);
            if (isPacked(images.metallicRoughness))
            {
                useTexture(images.metallicRoughness.roughness, TextureRole::Data);
            }
            else if (images.metallicRoughness.roughness || images.metallicRoughness.metalness)
            {
                m_packedIds.try_emplace(images.metallicRoughness, AssetId{});
            }
        }
    }

    [[nodiscard]] std::string packedKey(const PackedUse& use) const
    {
        std::string key;
        if (use.roughness)
        {
            key = m_images[*use.roughness].key + (use.glossiness ? " (inverted)" : "");
        }
        if (use.metalness)
        {
            key += (key.empty() ? "" : " + ") + m_images[*use.metalness].key;
        }
        return key + " (metallic-roughness)";
    }

    [[nodiscard]] core::Result<Image> packMetallicRoughness(const PackedUse& use) const
    {
        std::optional<Image> roughness;
        std::optional<Image> metalness;
        for (auto [image, decoded] : {std::pair{use.roughness, &roughness}, std::pair{use.metalness, &metalness}})
        {
            if (image)
            {
                core::Result<Image> result = decodeSceneImage(m_images[*image]);
                if (!result)
                {
                    return std::unexpected(result.error());
                }
                *decoded = std::move(*result);
            }
        }
        const std::uint32_t width = std::max(roughness ? roughness->width : 0u, metalness ? metalness->width : 0u);
        const std::uint32_t height = std::max(roughness ? roughness->height : 0u, metalness ? metalness->height : 0u);
        for (std::optional<Image>* image : {&roughness, &metalness})
        {
            if (*image && ((*image)->width != width || (*image)->height != height))
            {
                *image = resizeImage(**image, width, height);
            }
        }

        // A missing map reads as one, times the factor of the material.
        Image packed{width, height, std::vector<std::uint8_t>(std::size_t{width} * height * 4, 255)};
        for (std::size_t pixel = 0; pixel < std::size_t{width} * height; ++pixel)
        {
            if (roughness)
            {
                const std::uint8_t value = roughness->rgba[pixel * 4];
                packed.rgba[pixel * 4 + 1] = use.glossiness ? static_cast<std::uint8_t>(255 - value) : value;
            }
            if (metalness)
            {
                packed.rgba[pixel * 4 + 2] = metalness->rgba[pixel * 4];
            }
        }
        return packed;
    }

    void importTextures()
    {
        std::vector<detail::TextureRequest> requests;
        std::vector<AssetId*> ids;
        for (auto& [use, id] : m_textureIds)
        {
            std::string key = m_images[use.image].key + std::string(keySuffix(use.role));
            id = m_context.subAssets.acquire(AssetType::Texture, key);
            ids.push_back(&id);
            requests.push_back({
                .id = id,
                .key = std::move(key),
                .role = use.role,
                .decode = [this, image = use.image] { return decodeSceneImage(m_images[image]); },
            });
        }
        for (auto& [use, id] : m_packedIds)
        {
            std::string key = packedKey(use);
            id = m_context.subAssets.acquire(AssetType::Texture, key);
            ids.push_back(&id);
            requests.push_back({
                .id = id,
                .key = std::move(key),
                .role = TextureRole::Data,
                .decode = [this, use] { return packMetallicRoughness(use); },
            });
        }

        std::vector<std::optional<ImportedArtifact>> built = detail::buildTextures(m_context, requests);
        for (std::size_t index = 0; index < built.size(); ++index)
        {
            if (built[index])
            {
                m_artifacts.push_back(std::move(*built[index]));
            }
            else
            {
                // Materials fall back to their factors for textures that could not be built.
                *ids[index] = AssetId{};
            }
        }
    }

    [[nodiscard]] AssetId textureId(std::optional<std::uint32_t> image, TextureRole role) const
    {
        const auto found = image ? m_textureIds.find({*image, role}) : m_textureIds.end();
        return found != m_textureIds.end() ? found->second : AssetId{};
    }

    [[nodiscard]] MaterialData convertMaterial(const ufbx_material& source) const
    {
        const ufbx_material_pbr_maps& pbr = source.pbr;
        const ufbx_material_fbx_maps& fbx = source.fbx;
        const MaterialImages images = imagesOf(source);
        const auto value = [](const ufbx_material_map& map, float fallback) {
            return map.has_value ? static_cast<float>(map.value_real) : fallback;
        };
        const auto color = [](const ufbx_material_map& map, math::Vec3 fallback) {
            return map.has_value ? toVec3(map.value_vec3) : fallback;
        };

        MaterialData material;
        // A texture replaces the color it is connected to; the factor still scales it.
        const math::Vec3 baseColor = images.baseColor ? math::Vec3{1.0f} : color(pbr.base_color, math::Vec3{1.0f});
        float opacity = 1.0f;
        if (pbr.opacity.has_value)
        {
            opacity = static_cast<float>(pbr.opacity.value_real);
        }
        else if (fbx.transparency_factor.has_value || fbx.transparency_color.has_value)
        {
            // What shows through, as Phong materials say it: a color times a factor.
            const math::Vec3 transparency = color(fbx.transparency_color, math::Vec3{1.0f});
            opacity = 1.0f - value(fbx.transparency_factor, 1.0f) * (transparency.x + transparency.y + transparency.z) / 3.0f;
        }
        opacity = std::clamp(opacity, 0.0f, 1.0f);
        material.baseColorFactor = math::Vec4{baseColor * value(pbr.base_factor, 1.0f), opacity};
        material.baseColorTexture = textureId(images.baseColor, TextureRole::Color);
        if (images.opacity && images.opacity == images.baseColor)
        {
            // The alpha of the color image cuts the surface, as for leaves and fences.
            material.alphaMode = AlphaMode::Mask;
        }
        else if (opacity < 1.0f)
        {
            material.alphaMode = AlphaMode::Blend;
        }

        const PackedUse& packed = images.metallicRoughness;
        material.metallicFactor = packed.metalness ? 1.0f : std::clamp(value(pbr.metalness, 0.0f), 0.0f, 1.0f);
        material.roughnessFactor = packed.roughness ? 1.0f : std::clamp(value(pbr.roughness, 1.0f), 0.0f, 1.0f);
        if (isPacked(packed))
        {
            material.metallicRoughnessTexture = textureId(packed.roughness, TextureRole::Data);
        }
        else if (const auto found = m_packedIds.find(packed); found != m_packedIds.end())
        {
            material.metallicRoughnessTexture = found->second;
        }

        material.normalTexture = textureId(images.normal, TextureRole::Normal);
        material.occlusionTexture = textureId(images.occlusion, TextureRole::Data);
        const math::Vec3 emission = images.emission ? math::Vec3{1.0f} : color(pbr.emission_color, math::Vec3{0.0f});
        material.emissiveFactor = emission * value(pbr.emission_factor, 1.0f);
        material.emissiveTexture = textureId(images.emission, TextureRole::Color);
        material.doubleSided = source.features.double_sided.enabled;
        return material;
    }

    void importMaterials()
    {
        std::vector<std::string> names;
        for (const ufbx_material* material : m_scene.materials)
        {
            std::string name = toString(material->name);
            names.push_back(name.empty() ? std::format("Material {}", material->typed_id) : std::move(name));
        }
        const std::vector<std::string> keys = uniqueKeys(std::move(names));

        for (const ufbx_material* source : m_scene.materials)
        {
            const std::string& key = keys[source->typed_id];
            const AssetId id = m_context.subAssets.acquire(AssetType::Material, key);
            m_materialIds.push_back(id);
            m_artifacts.push_back({id, AssetType::Material, key, encodeMaterial(convertMaterial(*source))});
        }
    }

    void importMeshes()
    {
        std::vector<std::string> names;
        for (const ufbx_mesh* mesh : m_scene.meshes)
        {
            // Meshes are often unnamed: the node that shows one names it.
            std::string name = toString(mesh->name);
            if (name.empty() && mesh->instances.count > 0)
            {
                name = toString(mesh->instances.data[0]->name);
            }
            names.push_back(name.empty() ? std::format("Mesh {}", mesh->typed_id) : std::move(name));
        }
        const std::vector<std::string> keys = uniqueKeys(std::move(names));
        const std::string fileName = core::toUtf8(m_context.source.filename());

        std::vector<std::optional<MeshData>> meshes(m_scene.meshes.count);
        const auto build = [&](std::size_t index) {
            core::Result<MeshData> mesh =
                importMesh(*m_scene.meshes.data[index], m_materialIds, m_meshScales[index], m_nodeScales);
            if (!mesh)
            {
                DEVEX_LOG_WARNING("Skipping mesh '{}' of '{}': {}", keys[index], fileName, mesh.error());
                return;
            }
            meshes[index] = std::move(*mesh);
        };
        if (m_context.jobs != nullptr)
        {
            m_context.jobs->parallelFor(meshes.size(), build);
        }
        else
        {
            for (std::size_t index = 0; index < meshes.size(); ++index)
            {
                build(index);
            }
        }

        m_meshIds.resize(meshes.size());
        for (std::size_t index = 0; index < meshes.size(); ++index)
        {
            if (meshes[index])
            {
                const AssetId id = m_context.subAssets.acquire(AssetType::Mesh, keys[index]);
                m_meshIds[index] = id;
                m_artifacts.push_back({id, AssetType::Mesh, keys[index], encodeMesh(*meshes[index])});
            }
        }
    }

    void addNode(ModelData& model, const ufbx_node& source, std::int32_t parent)
    {
        ModelNode node;
        node.parent = parent;
        node.name = toString(source.name);
        if (source.mesh != nullptr && source.mesh->typed_id < m_meshIds.size())
        {
            node.mesh = m_meshIds[source.mesh->typed_id];
            if (node.mesh.isValid() && source.mesh->skin_deformers.count > 0)
            {
                // The skin deformer, until every node has its place in the model.
                node.skin = static_cast<std::int32_t>(source.mesh->skin_deformers.data[0]->typed_id);
            }
            if (node.name.empty())
            {
                node.name = toString(source.mesh->name);
            }
        }
        if (node.name.empty())
        {
            node.name = std::format("Node {}", source.typed_id);
        }
        // The baked scale leaves its root, and spreads the translations below it.
        const float baked = m_nodeScales[source.typed_id];
        const bool root = isRootNode(source);
        node.translation = toVec3(source.local_transform.translation) * (root ? 1.0f : baked);
        node.rotation = toQuat(source.local_transform.rotation);
        node.scale = toVec3(source.local_transform.scale) / (root ? baked : 1.0f);

        const auto index = static_cast<std::int32_t>(model.nodes.size());
        m_nodeToModel[source.typed_id] = index;
        model.nodes.push_back(std::move(node));
        for (const ufbx_node* child : source.children)
        {
            addNode(model, *child, index);
        }
    }

    // Turns the skin deformer of every node into a skin of the model, whose joints are model nodes.
    void resolveSkins(ModelData& model)
    {
        std::vector<std::int32_t> modelSkins(m_scene.skin_deformers.count, -1);
        for (ModelNode& node : model.nodes)
        {
            if (node.skin < 0)
            {
                continue;
            }
            const auto deformer = static_cast<std::size_t>(node.skin);
            if (modelSkins[deformer] < 0)
            {
                ModelSkin skin;
                for (const ufbx_skin_cluster* cluster : m_scene.skin_deformers.data[deformer]->clusters)
                {
                    // A bone outside the hierarchy leaves its vertices at their bind pose.
                    skin.joints.push_back(cluster->bone_node != nullptr ? m_nodeToModel[cluster->bone_node->typed_id] : -1);
                }
                modelSkins[deformer] = static_cast<std::int32_t>(model.skins.size());
                model.skins.push_back(std::move(skin));
            }
            node.skin = modelSkins[deformer];
        }
    }

    // One AnimationClip asset per animation stack of the file, baked into linear keys. Channels target
    // the nodes by name, so that a clip plays on any skeleton whose bones carry the same names.
    void importAnimations(ModelData& model)
    {
        std::vector<std::string> names;
        for (const ufbx_anim_stack* stack : m_scene.anim_stacks)
        {
            std::string name = toString(stack->name);
            names.push_back(name.empty() ? std::format("Animation {}", stack->typed_id) : std::move(name));
        }
        const std::vector<std::string> keys = uniqueKeys(std::move(names));
        const std::string fileName = core::toUtf8(m_context.source.filename());

        for (std::size_t index = 0; index < m_scene.anim_stacks.count; ++index)
        {
            ufbx_bake_opts options{};
            options.trim_start_time = true;
            options.key_reduction_enabled = true;
            options.key_reduction_rotation = true;
            ufbx_error error{};
            const std::unique_ptr<ufbx_baked_anim, BakedAnimationDeleter> baked(
                ufbx_bake_anim(&m_scene, m_scene.anim_stacks.data[index]->anim, &options, &error));
            if (!baked)
            {
                DEVEX_LOG_WARNING("Skipping animation '{}' of '{}': {}", keys[index], fileName,
                                  toString(error.description));
                continue;
            }

            AnimationClipData clip;
            clip.name = keys[index];
            for (const ufbx_baked_node& bakedNode : baked->nodes)
            {
                const std::int32_t node = bakedNode.typed_id < m_nodeToModel.size() ? m_nodeToModel[bakedNode.typed_id] : -1;
                if (node < 0)
                {
                    continue;
                }
                const ModelNode& rest = model.nodes[static_cast<std::size_t>(node)];
                const float bakedScale = m_nodeScales[bakedNode.typed_id];
                const bool root = isRootNode(*m_scene.nodes.data[bakedNode.typed_id]);
                std::optional<std::uint32_t> joint;
                for (std::optional<AnimationChannel> channel :
                     {vectorChannel(bakedNode.translation_keys, bakedNode.constant_translation, rest.translation,
                                    AnimationPath::Translation, root ? 1.0f : bakedScale),
                      rotationChannel(bakedNode.rotation_keys, bakedNode.constant_rotation, rest.rotation),
                      vectorChannel(bakedNode.scale_keys, bakedNode.constant_scale, rest.scale, AnimationPath::Scale,
                                    root ? 1.0f / bakedScale : 1.0f)})
                {
                    if (!channel)
                    {
                        continue;
                    }
                    if (!joint)
                    {
                        joint = static_cast<std::uint32_t>(clip.joints.size());
                        clip.joints.push_back(rest.name);
                    }
                    channel->joint = *joint;
                    clip.duration = std::max(clip.duration, channel->times.back());
                    clip.channels.push_back(std::move(*channel));
                }
            }
            if (clip.channels.empty())
            {
                // Files often carry a stack that moves nothing.
                continue;
            }
            if (core::Result<void> valid = validate(clip); !valid)
            {
                DEVEX_LOG_WARNING("Skipping animation '{}' of '{}': {}", keys[index], fileName, valid.error());
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
        m_nodeToModel.assign(m_scene.nodes.count, -1);
        // The root of the file is implicit, and holds no transform once the axes and units are
        // converted.
        for (const ufbx_node* root : m_scene.root_node->children)
        {
            addNode(model, *root, -1);
        }
        resolveSkins(model);
        importAnimations(model);
        m_artifacts.push_back({m_context.mainId, AssetType::Model, m_context.name, encodeModel(model)});
    }

    ImportContext& m_context;
    const ufbx_scene& m_scene;
    const std::vector<SceneImage>& m_images;
    std::map<TextureUse, AssetId> m_textureIds;
    std::map<PackedUse, AssetId> m_packedIds;
    // By typed identifier of ufbx.
    std::vector<AssetId> m_materialIds;
    // The scale baked into each node, that of its root, and into each mesh, that of the nodes showing
    // it.
    std::vector<float> m_nodeScales;
    std::vector<float> m_meshScales;
    std::vector<AssetId> m_meshIds;
    std::vector<std::int32_t> m_nodeToModel;
    std::vector<ImportedArtifact> m_artifacts;
};

} // namespace

core::Result<std::vector<std::filesystem::path>> findFbxDependencies(const std::filesystem::path& file)
{
    core::Result<LoadedScene> loaded = loadScene(file, 1.0, false);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    return dependenciesOf(findImages(*loaded->scene, file.parent_path()), std::move(loaded->openedFiles));
}

core::Result<ImportResult> importFbxFile(ImportContext& context)
{
    const double scale = context.numberOption("scale", 1.0);
    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the scale must be above zero, not {}", scale);
    }
    core::Result<LoadedScene> loaded = loadScene(context.source, scale, true);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }

    const std::vector<SceneImage> images = findImages(*loaded->scene, context.source.parent_path());
    FbxImport import(context, *loaded->scene, images);
    core::Result<ImportResult> result = import.run();
    if (result)
    {
        result->dependencies = dependenciesOf(images, std::move(loaded->openedFiles));
    }
    return result;
}

} // namespace devex::asset
