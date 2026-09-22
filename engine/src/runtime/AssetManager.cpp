#include <devex/asset/Artifact.hpp>
#include <devex/core/Log.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/runtime/AssetManager.hpp>

#include <utility>

namespace devex::runtime {

AssetManager::AssetManager(render::Renderer* renderer, asset::AssetSource* source) noexcept
    : m_renderer(renderer)
    , m_source(source)
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
    if (found == m_models.end() && id.isValid() && m_source != nullptr &&
        !m_failed.contains(id) && m_source->find(id) != nullptr && loadModel(id))
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
    if (m_source == nullptr || m_source->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
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
    if (m_source == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "scene {} is not part of a project", id.uuid);
    }
    core::Result<std::string> text = m_source->sceneText(id);
    if (text)
    {
        m_sceneTexts.emplace(id, *text);
    }
    return text;
}

std::shared_ptr<const audio::Clip> AssetManager::audioClip(asset::AssetId id)
{
    if (const auto found = m_audioClips.find(id); found != m_audioClips.end())
    {
        return found->second;
    }
    // Sounds need no renderer.
    if (!id.isValid() || m_source == nullptr || m_failed.contains(id) || m_source->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    core::Result<asset::AudioClipData> data = bytes ? asset::decodeAudioClip(*bytes)
                                                    : core::Result<asset::AudioClipData>(std::unexpected(bytes.error()));
    core::Result<std::shared_ptr<const audio::Clip>> clip =
        data ? audio::Clip::create(std::move(*data)) : core::Result<std::shared_ptr<const audio::Clip>>(std::unexpected(data.error()));
    if (!clip)
    {
        DEVEX_LOG_ERROR("Cannot load audio clip {}: {}", id.uuid, clip.error());
        m_failed.insert(id);
        return nullptr;
    }
    m_audioClips.emplace(id, *clip);
    return *clip;
}

std::shared_ptr<const animation::Clip> AssetManager::animationClip(asset::AssetId id)
{
    if (const auto found = m_animationClips.find(id); found != m_animationClips.end())
    {
        return found->second;
    }
    // Animations need no renderer.
    if (!id.isValid() || m_source == nullptr || m_failed.contains(id) || m_source->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    core::Result<asset::AnimationClipData> data =
        bytes ? asset::decodeAnimation(*bytes)
              : core::Result<asset::AnimationClipData>(std::unexpected(bytes.error()));
    core::Result<std::shared_ptr<const animation::Clip>> clip =
        data ? animation::Clip::create(std::move(*data))
             : core::Result<std::shared_ptr<const animation::Clip>>(std::unexpected(data.error()));
    if (!clip)
    {
        DEVEX_LOG_ERROR("Cannot load animation {}: {}", id.uuid, clip.error());
        m_failed.insert(id);
        return nullptr;
    }
    m_animationClips.emplace(id, *clip);
    return *clip;
}

math::Extent2D AssetManager::textureSize(asset::AssetId id) const noexcept
{
    const auto found = m_textureSizes.find(id);
    return found != m_textureSizes.end() ? found->second : math::Extent2D{};
}

std::shared_ptr<const asset::ThemeData> AssetManager::theme(asset::AssetId id)
{
    if (const auto found = m_themes.find(id); found != m_themes.end())
    {
        return found->second;
    }
    if (!id.isValid() || m_source == nullptr || m_failed.contains(id) ||
        m_source->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    core::Result<asset::ThemeData> data =
        bytes ? asset::decodeTheme(*bytes)
              : core::Result<asset::ThemeData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load theme {}: {}", id.uuid, data.error());
        m_failed.insert(id);
        return nullptr;
    }
    auto theme = std::make_shared<const asset::ThemeData>(std::move(*data));
    m_themes.emplace(id, theme);
    return theme;
}

const LoadedFont* AssetManager::font(asset::AssetId id)
{
    auto found = m_fonts.find(id);
    // Fonts load without a renderer as well: their metrics alone lay text out, in tests and in
    // headless tools.
    if (found == m_fonts.end() && id.isValid() && m_source != nullptr && !m_failed.contains(id) &&
        m_source->find(id) != nullptr && loadFont(id))
    {
        found = m_fonts.find(id);
    }
    return found != m_fonts.end() ? &found->second : nullptr;
}

void AssetManager::handleEvents(std::span<const asset::AssetEvent> events)
{
    for (const asset::AssetEvent& event : events)
    {
        m_meshData.erase(event.id);
        // Sounds playing keep the clip they had; the next ones play the new one.
        m_audioClips.erase(event.id);
        m_animationClips.erase(event.id);
        // A theme is read again on the next frame, which shows an edited look at once.
        m_themes.erase(event.id);
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
        case asset::AssetType::Font:
            if (m_fonts.contains(event.id))
            {
                releaseFont(event.id);
                if (!removed)
                {
                    static_cast<void>(loadFont(event.id));
                }
            }
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
        case asset::AssetType::AudioClip:
        case asset::AssetType::AnimationClip:
        case asset::AssetType::Theme:
            break;
        }
    }
    if (texturesChanged)
    {
        refreshMaterials();
    }
}

asset::AssetSource* AssetManager::source() const noexcept
{
    return m_source;
}

void AssetManager::setSource(asset::AssetSource* source)
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
    while (!m_fonts.empty())
    {
        releaseFont(m_fonts.begin()->first);
    }
    m_models.clear();
    m_meshData.clear();
    m_sceneTexts.clear();
    m_audioClips.clear();
    m_animationClips.clear();
    m_themes.clear();
    m_failed.clear();
    m_source = source;
}

bool AssetManager::canLoad(asset::AssetId id) const
{
    // Assets still importing are not in the database yet; their import event triggers a retry.
    return id.isValid() && m_renderer != nullptr && m_source != nullptr &&
           !m_failed.contains(id) && m_source->find(id) != nullptr;
}

bool AssetManager::loadMesh(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
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
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
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
    // Kept beside the handle, for the images whose borders stay unstretched.
    if (!data->mips.empty())
    {
        m_textureSizes.insert_or_assign(id,
                                        math::Extent2D{data->mips.front().width,
                                                       data->mips.front().height});
    }
    return true;
}

bool AssetManager::loadMaterial(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
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

bool AssetManager::loadFont(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    core::Result<asset::FontData> data =
        bytes ? asset::decodeFont(*bytes) : core::Result<asset::FontData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load font {}: {}", id.uuid, data.error());
        m_failed.insert(id);
        return false;
    }

    LoadedFont loaded;
    if (m_renderer != nullptr)
    {
        // The atlas holds distances, not colors: one channel, read as it was written.
        asset::TextureData image{.format = asset::TextureFormat::R8Unorm};
        image.mips.push_back({.width = data->atlasWidth,
                              .height = data->atlasHeight,
                              .bytes = std::vector<std::byte>(
                                  reinterpret_cast<const std::byte*>(data->atlas.data()),
                                  reinterpret_cast<const std::byte*>(data->atlas.data() + data->atlas.size()))});
        const core::Result<render::TextureHandle> atlas = m_renderer->createTexture(image);
        if (!atlas)
        {
            DEVEX_LOG_ERROR("Cannot load the atlas of font {}: {}", id.uuid, atlas.error());
            m_failed.insert(id);
            return false;
        }
        loaded.atlas = *atlas;
    }
    loaded.data = std::make_shared<const asset::FontData>(std::move(*data));
    m_fonts.insert_or_assign(id, std::move(loaded));
    return true;
}

bool AssetManager::loadModel(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
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
        m_textureSizes.erase(id);
    }
}

void AssetManager::releaseFont(asset::AssetId id)
{
    if (const auto found = m_fonts.find(id); found != m_fonts.end())
    {
        if (m_renderer != nullptr && found->second.atlas.isValid())
        {
            m_renderer->destroyTexture(found->second.atlas);
        }
        m_fonts.erase(found);
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
