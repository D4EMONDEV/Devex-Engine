#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/render/RenderWorld.hpp>

#include <unordered_map>

namespace devex::runtime {

// Resolves asset identifiers to loaded GPU resources. Built-in meshes are registered at startup;
// the future asset database will fill it from .dvxmeta files.
class AssetRegistry
{
public:
    // Replaces any mesh registered under the same identifier.
    void registerMesh(asset::AssetId id, render::MeshHandle mesh);
    void unregisterMesh(asset::AssetId id);

    // Returns an invalid handle when no mesh is registered under the identifier.
    [[nodiscard]] render::MeshHandle findMesh(asset::AssetId id) const noexcept;

private:
    std::unordered_map<asset::AssetId, render::MeshHandle> m_meshes;
};

} // namespace devex::runtime
