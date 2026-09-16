#include <devex/runtime/AssetRegistry.hpp>

namespace devex::runtime {

void AssetRegistry::registerMesh(asset::AssetId id, render::MeshHandle mesh)
{
    m_meshes.insert_or_assign(id, mesh);
}

void AssetRegistry::unregisterMesh(asset::AssetId id)
{
    m_meshes.erase(id);
}

render::MeshHandle AssetRegistry::findMesh(asset::AssetId id) const noexcept
{
    const auto found = m_meshes.find(id);
    return found == m_meshes.end() ? render::MeshHandle{} : found->second;
}

} // namespace devex::runtime
