#pragma once

#include <devex/animation/Clip.hpp>
#include <devex/audio/Clip.hpp>
#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetMemory.hpp>
#include <devex/asset/FontData.hpp>
#include <devex/asset/MaterialData.hpp>
#include <devex/asset/ModelData.hpp>
#include <devex/asset/ThemeData.hpp>
#include <devex/asset/AssetSource.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devex::runtime {

// A font and the atlas of distances it is drawn from. The atlas is invalid without a renderer,
// which still leaves the metrics usable to lay text out.
struct LoadedFont
{
    std::shared_ptr<const asset::FontData> data;
    render::TextureHandle atlas;
};

struct LoadedMesh
{
    render::MeshHandle handle;
    // Material of each submesh; invalid ones use the default material.
    std::vector<asset::AssetId> submeshMaterials;
    // What its vertices and indices take on the GPU.
    std::size_t gpuBytes = 0;
};

// Resolves asset identifiers to loaded resources. Assets are loaded from their source, the asset
// database of a project or the package of an exported game, the first time they are needed, and
// loaded again when an import changes
// them. Applications can also register resources they create, such as the built-in meshes.
class AssetManager
{
public:
    // Both may be null: without a renderer nothing reaches the GPU, and without a source only
    // registered resources are known. The renderer and the source must outlive the manager.
    AssetManager(render::Renderer* renderer, asset::AssetSource* source) noexcept;

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
    // The vertices and triangles of a mesh on the CPU, for physics, including the built-in meshes;
    // null when the mesh cannot be loaded.
    [[nodiscard]] const asset::MeshData* meshData(asset::AssetId id);
    // The text of a scene: its source file, or its imported artifact when the source cannot be read.
    // It is kept until an import changes the scene, so that prefab instances can still be compared
    // with the previous version of their prefab once it changed on disk.
    [[nodiscard]] core::Result<std::string> sceneText(asset::AssetId id);

    // A sound, loaded (and decoded, for a clip that is) once and shared by every sound that plays it;
    // null when it cannot be loaded.
    [[nodiscard]] std::shared_ptr<const audio::Clip> audioClip(asset::AssetId id);

    // An animation, decoded once and shared by every animator that plays it; null when it cannot
    // be loaded.
    [[nodiscard]] std::shared_ptr<const animation::Clip> animationClip(asset::AssetId id);
    // The size of a texture in pixels, or nothing when it is not loaded. A nine-slice keeps its
    // corners at the size they were drawn at, which needs the size of the image.
    [[nodiscard]] math::Extent2D textureSize(asset::AssetId id) const noexcept;

    // The look an interface follows. Themes need no renderer: they are read as they are written.
    [[nodiscard]] std::shared_ptr<const asset::ThemeData> theme(asset::AssetId id);

    // What the loaded assets take, by type and for the heaviest ones. Read when asked: the
    // profiler of the editor asks a few times a second at most.
    [[nodiscard]] asset::MemoryReport memoryReport(std::size_t largest = 12) const;

    // A font with its atlas, loaded once and shared by every text drawn with it; null when it
    // cannot be loaded.
    [[nodiscard]] const LoadedFont* font(asset::AssetId id);

    // Reloads the loaded assets that an import changed and releases removed ones.
    void handleEvents(std::span<const asset::AssetEvent> events);

    [[nodiscard]] asset::AssetSource* source() const noexcept;
    // Releases every asset loaded from the current source, keeping registered meshes, then loads
    // from the new one, which may be null. Used when the editor opens another project.
    void setSource(asset::AssetSource* source);

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
    [[nodiscard]] bool loadFont(asset::AssetId id);
    [[nodiscard]] render::MaterialDesc describe(const asset::MaterialData& material);
    void releaseMesh(asset::AssetId id);
    void releaseTexture(asset::AssetId id);
    void releaseMaterial(asset::AssetId id);
    void releaseFont(asset::AssetId id);
    // Points every loaded material at the current handles of its textures.
    void refreshMaterials();

    render::Renderer* m_renderer;
    asset::AssetSource* m_source;
    std::unordered_map<asset::AssetId, OwnedMesh> m_meshes;
    std::unordered_map<asset::AssetId, render::TextureHandle> m_textures;
    // What each texture takes on the GPU, all its mip levels.
    std::unordered_map<asset::AssetId, std::size_t> m_textureBytes;
    // What each animation was read from: its curves take about as much once decoded.
    std::unordered_map<asset::AssetId, std::size_t> m_animationBytes;
    std::unordered_map<asset::AssetId, math::Extent2D> m_textureSizes;
    std::unordered_map<asset::AssetId, LoadedMaterial> m_materials;
    std::unordered_map<asset::AssetId, asset::ModelData> m_models;
    std::unordered_map<asset::AssetId, asset::MeshData> m_meshData;
    std::unordered_map<asset::AssetId, std::string> m_sceneTexts;
    std::unordered_map<asset::AssetId, std::shared_ptr<const audio::Clip>> m_audioClips;
    std::unordered_map<asset::AssetId, std::shared_ptr<const animation::Clip>> m_animationClips;
    std::unordered_map<asset::AssetId, std::shared_ptr<const asset::ThemeData>> m_themes;
    std::unordered_map<asset::AssetId, LoadedFont> m_fonts;
    // Assets whose loading failed, retried once an import changes them.
    std::unordered_set<asset::AssetId> m_failed;
};

} // namespace devex::runtime
