#include <devex/asset/import/GltfImporter.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <numeric>
#include <utility>

namespace devex::asset {
namespace {

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

[[nodiscard]] core::Result<MeshData> importPrimitive(const fastgltf::Asset& asset,
                                                     const fastgltf::Primitive& primitive)
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
    return mesh;
}

} // namespace

core::Result<ImportedScene> importGltf(const std::filesystem::path& path)
{
    const std::string displayPath = core::toUtf8(path);

    fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
    {
        return core::makeError(core::ErrorCode::Io, "cannot read '{}': {}", displayPath,
                               fastgltf::getErrorMessage(data.error()));
    }

    fastgltf::Parser parser;
    fastgltf::Expected<fastgltf::Asset> asset =
        parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers);
    if (asset.error() != fastgltf::Error::None)
    {
        return core::makeError(core::ErrorCode::Parse, "cannot parse '{}': {}", displayPath,
                               fastgltf::getErrorMessage(asset.error()));
    }

    ImportedScene scene;
    // For each glTF mesh, the imported meshes created from its primitives.
    std::vector<std::vector<std::size_t>> meshesOfGltfMesh(asset->meshes.size());
    for (std::size_t gltfMesh = 0; gltfMesh < asset->meshes.size(); ++gltfMesh)
    {
        const fastgltf::Mesh& mesh = asset->meshes[gltfMesh];
        for (std::size_t primitive = 0; primitive < mesh.primitives.size(); ++primitive)
        {
            core::Result<MeshData> imported = importPrimitive(asset.get(), mesh.primitives[primitive]);
            if (!imported)
            {
                DEVEX_LOG_WARNING("Skipping primitive {} of mesh '{}' in '{}': {}", primitive,
                                  mesh.name, displayPath, imported.error());
                continue;
            }
            meshesOfGltfMesh[gltfMesh].push_back(scene.meshes.size());
            scene.meshes.push_back({std::string(std::string_view(mesh.name)), std::move(*imported)});
        }
    }

    const std::size_t sceneIndex = asset->defaultScene ? *asset->defaultScene : 0;
    if (sceneIndex < asset->scenes.size())
    {
        fastgltf::iterateSceneNodes(
            asset.get(), sceneIndex, fastgltf::math::fmat4x4(),
            [&](fastgltf::Node& node, const fastgltf::math::fmat4x4& matrix) {
                if (!node.meshIndex)
                {
                    return;
                }
                const math::Mat4 transform = toMat4(matrix);
                for (const std::size_t mesh : meshesOfGltfMesh[*node.meshIndex])
                {
                    scene.instances.push_back({mesh, transform});
                }
            });
    }

    DEVEX_LOG_DEBUG("Imported '{}': {} meshes, {} instances", displayPath, scene.meshes.size(),
                    scene.instances.size());
    return scene;
}

} // namespace devex::asset
