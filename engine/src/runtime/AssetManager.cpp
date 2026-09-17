#include <devex/asset/Artifact.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/runtime/AssetManager.hpp>

#include <utility>

namespace devex::runtime {

AssetManager::AssetManager(render::Renderer* renderer, asset::AssetDatabase* database) noexcept
    : m_renderer(renderer)
    , m_database(database)
{
}

void AssetManager::registerMesh(asset::AssetId id, render::MeshHandle mesh,
                                std::vector<asset::AssetId> submeshMaterials)
{
    releaseMesh(id);
    if (submeshMaterials.empty())
    {
        submeshMaterials.resize(1);
    }
    m_meshes.insert_or_assign(id, OwnedMesh{{mesh, std::move(submeshMaterials)}, false});
    m_failed.erase(id);
}

const LoadedMesh* AssetManager::mesh(asset::AssetId id)
{
    auto found = m_meshes.find(id);
    if (found == m_meshes.end() && canLoad(id) && loadMesh(id))
    {
        found = m_meshes.find(id);
    }
    return found != m_meshes.end() ? &found->second.mesh : nullptr;
}

render::MaterialHandle AssetManager::material(asset::AssetId id)
{
    auto found = m_materials.find(id);
    if (found == m_materials.end() && canLoad(id) && loadMaterial(id))
    {
        found = m_materials.find(id);
    }
    return found != m_materials.end() ? found->second.handle : render::MaterialHandle{};
}

render::TextureHandle AssetManager::texture(asset::AssetId id)
{
    auto found = m_textures.find(id);
    if (found == m_textures.end() && canLoad(id) && loadTexture(id))
    {
        found = m_textures.find(id);
    }
    return found != m_textures.end() ? found->second : render::TextureHandle{};
}

const asset::ModelData* AssetManager::model(asset::AssetId id)
{
    auto found = m_models.find(id);
    if (found == m_models.end() && id.isValid() && m_database != nullptr &&
        !m_failed.contains(id) && m_database->find(id) != nullptr && loadModel(id))
    {
        found = m_models.find(id);
    }
    return found != m_models.end() ? &found->second : nullptr;
}

const asset::MeshData* AssetManager::meshData(asset::AssetId id)
{
    if (const auto found = m_meshData.find(id); found != m_meshData.end())
    {
        return &found->second;
    }
    if (std::optional<asset::MeshData> builtin = asset::makeBuiltinMesh(id))
    {
        return &m_meshData.emplace(id, std::move(*builtin)).first->second;
    }
    if (m_database == nullptr || m_database->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = m_database->loadArtifact(id);
    core::Result<asset::MeshData> data =
        bytes ? asset::decodeMesh(*bytes) : core::Result<asset::MeshData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load the triangles of mesh {}: {}", id.uuid, data.error());
        return nullptr;
    }
    return &m_meshData.emplace(id, std::move(*data)).first->second;
}

core::Result<std::string> AssetManager::sceneText(asset::AssetId id)
{
    if (const auto found = m_sceneTexts.find(id); found != m_sceneTexts.end())
    {
        return found->second;
    }
    if (m_database == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "scene {} is not part of a project", id.uuid);
    }
    const std::optional<asset::SourceFile> source = m_database->sourceOf(id);
    const std::optional<std::filesystem::path> path =
        source && source->importer == "scene" ? m_database->project().absolutePath(source->path) : std::nullopt;
    if (!path)
    {
        return core::makeError(core::ErrorCode::NotFound, "scene {} does not exist", id.uuid);
    }
    core::Result<std::string> text = core::readTextFile(*path);
    if (!text)
    {
        const core::Result<std::vector<std::byte>> bytes = m_database->loadArtifact(id);
        if (!bytes)
        {
            return core::makeError(text.error().code, "cannot read {}: {}", source->path, text.error().message);
        }
        text = asset::decodeScene(*bytes);
        if (!text)
        {
            return std::unexpected(text.error());
        }
    }
    m_sceneTexts.emplace(id, *text);
    return text;
}

void AssetManager::handleEvents(std::span<const asset::AssetEvent> events)
{
    for (const asset::AssetEvent& event : events)
    {
        m_meshData.erase(event.id);
    }
    bool texturesChanged = false;
    for (const asset::AssetEvent& event : events)
    {
        m_failed.erase(event.id);
        const bool removed = event.change == asset::AssetChange::Removed;
        switch (event.type)
        {
        case asset::AssetType::Mesh:
            if (const auto found = m_meshes.find(event.id);
                found != m_meshes.end() && found->second.owned)
            {
                releaseMesh(event.id);
                if (!removed)
                {
                    static_cast<void>(loadMesh(event.id));
                }
            }
            break;
        case asset::AssetType::Texture:
            if (m_textures.contains(event.id))
            {
                releaseTexture(event.id);
                if (!removed)
                {
                    static_cast<void>(loadTexture(event.id));
                }
            }
            // Materials loaded before the texture was imported pick it up as well.
            texturesChanged = true;
            break;
        case asset::AssetType::Material:
            if (m_materials.contains(event.id))
            {
                // Reloaded under the same handle, so draws keep referring to it.
                if (removed || !loadMaterial(event.id))
                {
                    releaseMaterial(event.id);
                }
            }
            break;
        case asset::AssetType::Model:
            if (m_models.erase(event.id) > 0 && !removed)
            {
                static_cast<void>(loadModel(event.id));
            }
            break;
        case asset::AssetType::Scene:
            // Read again when needed. Instances of the scene are rebuilt by the application.
            m_sceneTexts.erase(event.id);
            break;
        }
    }
    if (texturesChanged)
    {
        refreshMaterials();
    }
}

asset::AssetDatabase* AssetManager::database() const noexcept
{
    return m_database;
}

void AssetManager::setDatabase(asset::AssetDatabase* database)
{
    std::vector<asset::AssetId> loaded;
    for (const auto& [id, mesh] : m_meshes)
    {
        if (mesh.owned)
        {
            loaded.push_back(id);
        }
    }
    for (const asset::AssetId id : loaded)
    {
        releaseMesh(id);
    }
    while (!m_materials.empty())
    {
        releaseMaterial(m_materials.begin()->first);
    }
    while (!m_textures.empty())
    {
        releaseTexture(m_textures.begin()->first);
    }
    m_models.clear();
    m_meshData.clear();
    m_sceneTexts.clear();
    m_failed.clear();
    m_database = database;
}

bool AssetManager::canLoad(asset::AssetId id) const
{
    // Assets still importing are not in the database yet; their import event triggers a retry.
    return id.isValid() && m_renderer != nullptr && m_database != nullptr &&
           !m_failed.contains(id) && m_database->find(id) != nullptr;
}

bool AssetManager::loadMesh(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_database->loadArtifact(id);
    core::Result<asset::MeshData> data = bytes ? asset::decodeMesh(*bytes)
                                               : core::Result<asset::MeshData>(std::unexpected(bytes.error()));
    core::Result<render::MeshHandle> handle =
        data ? m_renderer->createMesh(*data)
             : core::Result<render::MeshHandle>(std::unexpected(data.error()));
    if (!handle)
    {
        DEVEX_LOG_ERROR("Cannot load mesh {}: {}", id.uuid, handle.error());
        m_failed.insert(id);
        return false;
    }

    LoadedMesh mesh{.handle = *handle};
    for (const asset::Submesh& submesh : asset::submeshesOf(*data))
    {
        mesh.submeshMaterials.push_back(submesh.material);
    }
    m_meshes.insert_or_assign(id, OwnedMesh{std::move(mesh), true});
    return true;
}

bool AssetManager::loadTexture(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_database->loadArtifact(id);
    core::Result<asset::TextureData> data =
        bytes ? asset::decodeTexture(*bytes)
              : core::Result<asset::TextureData>(std::unexpected(bytes.error()));
    core::Result<render::TextureHandle> handle =
        data ? m_renderer->createTexture(*data)
             : core::Result<render::TextureHandle>(std::unexpected(data.error()));
    if (!handle)
    {
        DEVEX_LOG_ERROR("Cannot load texture {}: {}", id.uuid, handle.error());
        m_failed.insert(id);
        return false;
    }
    m_textures.insert_or_assign(id, *handle);
    return true;
}

bool AssetManager::loadMaterial(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_database->loadArtifact(id);
    core::Result<asset::MaterialData> data =
        bytes ? asset::decodeMaterial(*bytes)
              : core::Result<asset::MaterialData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load material {}: {}", id.uuid, data.error());
        m_failed.insert(id);
        return false;
    }

    const render::MaterialDesc description = describe(*data);
    if (const auto found = m_materials.find(id); found != m_materials.end())
    {
        m_renderer->updateMaterial(found->second.handle, description);
        found->second.data = std::move(*data);
    }
    else
    {
        m_materials.insert_or_assign(
            id, LoadedMaterial{m_renderer->createMaterial(description), std::move(*data)});
    }
    return true;
}

bool AssetManager::loadModel(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_database->loadArtifact(id);
    core::Result<asset::ModelData> data =
        bytes ? asset::decodeModel(*bytes)
              : core::Result<asset::ModelData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load model {}: {}", id.uuid, data.error());
        m_failed.insert(id);
        return false;
    }
    m_models.insert_or_assign(id, std::move(*data));
    return true;
}

render::MaterialDesc AssetManager::describe(const asset::MaterialData& material)
{
    return render::MaterialDesc{
        .baseColorFactor = material.baseColorFactor,
        .baseColorTexture = texture(material.baseColorTexture),
        .metallicFactor = material.metallicFactor,
        .roughnessFactor = material.roughnessFactor,
        .metallicRoughnessTexture = texture(material.metallicRoughnessTexture),
        .normalTexture = texture(material.normalTexture),
        .normalScale = material.normalScale,
        .occlusionTexture = texture(material.occlusionTexture),
        .occlusionStrength = material.occlusionStrength,
        .emissiveFactor = material.emissiveFactor,
        .emissiveTexture = texture(material.emissiveTexture),
        .alphaMode = material.alphaMode,
        .alphaCutoff = material.alphaCutoff,
        .doubleSided = material.doubleSided,
    };
}

void AssetManager::releaseMesh(asset::AssetId id)
{
    if (const auto found = m_meshes.find(id); found != m_meshes.end())
    {
        if (found->second.owned && m_renderer != nullptr)
        {
            m_renderer->destroyMesh(found->second.mesh.handle);
        }
        m_meshes.erase(found);
    }
}

void AssetManager::releaseTexture(asset::AssetId id)
{
    if (const auto found = m_textures.find(id); found != m_textures.end())
    {
        m_renderer->destroyTexture(found->second);
        m_textures.erase(found);
    }
}

void AssetManager::releaseMaterial(asset::AssetId id)
{
    if (const auto found = m_materials.find(id); found != m_materials.end())
    {
        m_renderer->destroyMaterial(found->second.handle);
        m_materials.erase(found);
    }
}

void AssetManager::refreshMaterials()
{
    for (auto& [id, material] : m_materials)
    {
        m_renderer->updateMaterial(material.handle, describe(material.data));
    }
}

} // namespace devex::runtime
