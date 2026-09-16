#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/MaterialData.hpp>
#include <devex/asset/ModelData.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devex::runtime {

struct LoadedMesh
{
    render::MeshHandle handle;
    // Material of each submesh; invalid ones use the default material.
    std::vector<asset::AssetId> submeshMaterials;
};

// Resolves asset identifiers to loaded resources. Assets of the project are loaded from the cache
// of the asset database the first time they are needed, and loaded again when an import changes
// them. Applications can also register resources they create, such as the built-in meshes.
class AssetManager
{
public:
    // Both may be null: without a renderer nothing reaches the GPU, and without a database only
    // registered resources are known. The renderer and the database must outlive the manager.
    AssetManager(render::Renderer* renderer, asset::AssetDatabase* database) noexcept;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Replaces any mesh known under the identifier. The manager does not destroy registered
    // meshes.
    void registerMesh(asset::AssetId id, render::MeshHandle mesh,
                      std::vector<asset::AssetId> submeshMaterials = {});

    // Null when the mesh is neither registered nor loadable.
    [[nodiscard]] const LoadedMesh* mesh(asset::AssetId id);
    // Invalid when the material cannot be loaded.
    [[nodiscard]] render::MaterialHandle material(asset::AssetId id);
    [[nodiscard]] render::TextureHandle texture(asset::AssetId id);
    // CPU data only; null when the model cannot be loaded.
    [[nodiscard]] const asset::ModelData* model(asset::AssetId id);

    // Reloads the loaded assets that an import changed and releases removed ones.
    void handleEvents(std::span<const asset::AssetEvent> events);

    [[nodiscard]] asset::AssetDatabase* database() const noexcept;

private:
    struct OwnedMesh
    {
        LoadedMesh mesh;
        // False for meshes registered by the application.
        bool owned = false;
    };

    struct LoadedMaterial
    {
        render::MaterialHandle handle;
        asset::MaterialData data;
    };

    [[nodiscard]] bool canLoad(asset::AssetId id) const;
    [[nodiscard]] bool loadMesh(asset::AssetId id);
    [[nodiscard]] bool loadTexture(asset::AssetId id);
    [[nodiscard]] bool loadMaterial(asset::AssetId id);
    [[nodiscard]] bool loadModel(asset::AssetId id);
    [[nodiscard]] render::MaterialDesc describe(const asset::MaterialData& material);
    void releaseMesh(asset::AssetId id);
    void releaseTexture(asset::AssetId id);
    void releaseMaterial(asset::AssetId id);
    // Points every loaded material at the current handles of its textures.
    void refreshMaterials();

    render::Renderer* m_renderer;
    asset::AssetDatabase* m_database;
    std::unordered_map<asset::AssetId, OwnedMesh> m_meshes;
    std::unordered_map<asset::AssetId, render::TextureHandle> m_textures;
    std::unordered_map<asset::AssetId, LoadedMaterial> m_materials;
    std::unordered_map<asset::AssetId, asset::ModelData> m_models;
    // Assets whose loading failed, retried once an import changes them.
    std::unordered_set<asset::AssetId> m_failed;
};

} // namespace devex::runtime
