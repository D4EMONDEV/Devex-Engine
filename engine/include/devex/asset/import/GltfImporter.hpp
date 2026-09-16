#pragma once

#include <devex/asset/MeshData.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace devex::asset {

struct ImportedMesh
{
    std::string name;
    MeshData data;
};

// A placement of an imported mesh in the scene.
struct ImportedInstance
{
    std::size_t mesh = 0;
    math::Mat4 transform{1.0f};
};

struct ImportedScene
{
    std::vector<ImportedMesh> meshes;
    std::vector<ImportedInstance> instances;
};

// Imports the triangle geometry of a .gltf or .glb file. Each primitive becomes a mesh, and the
// default scene provides the instances with their world transforms. glTF already follows the
// engine conventions, so no conversion is applied.
[[nodiscard]] core::Result<ImportedScene> importGltf(const std::filesystem::path& path);

} // namespace devex::asset
