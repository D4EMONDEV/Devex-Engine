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
    m_animationBytes.insert_or_assign(id, bytes ? bytes->size() : 0);
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
        m_animationBytes.erase(event.id);
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

asset::MemoryReport AssetManager::memoryReport(std::size_t largest) const
{
    std::vector<asset::AssetMemory> assets;
    const auto add = [&](asset::AssetId id, asset::AssetType type, std::size_t cpu, std::size_t gpu) {
        if (cpu == 0 && gpu == 0)
        {
            return;
        }
        const asset::AssetInfo* const info = m_source != nullptr ? m_source->find(id) : nullptr;
        assets.push_back({.id = id,
                          .name = info != nullptr ? info->name : id.uuid.toString(),
                          .type = type,
                          .cpuBytes = cpu,
                          .gpuBytes = gpu});
    };
    for (const auto& [id, mesh] : m_meshes)
    {
        if (mesh.owned)
        {
            add(id, asset::AssetType::Mesh, 0, mesh.mesh.gpuBytes);
        }
    }
    // The copies kept on the CPU for colliders and skinning.
    for (const auto& [id, data] : m_meshData)
    {
        add(id, asset::AssetType::Mesh,
            data.vertices.size() * sizeof(asset::Vertex) + data.indices.size() * sizeof(std::uint32_t) +
                data.skin.size() * sizeof(asset::VertexSkin),
            0);
    }
    for (const auto& [id, bytes] : m_textureBytes)
    {
        add(id, asset::AssetType::Texture, 0, bytes);
    }
    for (const auto& [id, font] : m_fonts)
    {
        const std::size_t atlas = font.data != nullptr ? font.data->atlas.size() : 0;
        const std::size_t glyphs = font.data != nullptr ? font.data->glyphs.size() * sizeof(asset::FontGlyph) +
                                                              font.data->kerning.size() * sizeof(asset::FontKerning)
                                                        : 0;
        add(id, asset::AssetType::Font, atlas + glyphs, font.atlas.isValid() ? atlas : 0);
    }
    for (const auto& [id, clip] : m_audioClips)
    {
        add(id, asset::AssetType::AudioClip, clip != nullptr ? clip->samples().size_bytes() : 0, 0);
    }
    for (const auto& [id, bytes] : m_animationBytes)
    {
        add(id, asset::AssetType::AnimationClip, bytes, 0);
    }
    for (const auto& [id, model] : m_models)
    {
        add(id, asset::AssetType::Model, model.nodes.size() * sizeof(asset::ModelNode), 0);
    }
    for (const auto& [id, text] : m_sceneTexts)
    {
        add(id, asset::AssetType::Scene, text.size(), 0);
    }

    // A mesh counted on the GPU and on the CPU is one asset.
    std::ranges::sort(assets, {}, &asset::AssetMemory::id);
    std::vector<asset::AssetMemory> merged;
    for (asset::AssetMemory& entry : assets)
    {
        if (!merged.empty() && merged.back().id == entry.id)
        {
            merged.back().cpuBytes += entry.cpuBytes;
            merged.back().gpuBytes += entry.gpuBytes;
            continue;
        }
        merged.push_back(std::move(entry));
    }

    asset::MemoryReport report;
    for (const asset::AssetMemory& entry : merged)
    {
        auto line = std::ranges::find(report.types, entry.type, &asset::MemoryByType::type);
        if (line == report.types.end())
        {
            report.types.push_back({.type = entry.type});
            line = report.types.end() - 1;
        }
        ++line->count;
        line->cpuBytes += entry.cpuBytes;
        line->gpuBytes += entry.gpuBytes;
    }
    const auto weight = [](const auto& entry) { return entry.cpuBytes + entry.gpuBytes; };
    std::ranges::sort(report.types, std::ranges::greater{}, weight);
    std::ranges::sort(merged, std::ranges::greater{}, weight);
    merged.resize(std::min(merged.size(), largest));
    report.largest = std::move(merged);
    return report;
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
    m_animationBytes.clear();
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

    LoadedMesh mesh{.handle = *handle,
                    .gpuBytes = data->vertices.size() * sizeof(asset::Vertex) +
                                data->indices.size() * sizeof(std::uint32_t) +
                                data->skin.size() * sizeof(asset::VertexSkin)};
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
    std::size_t textureBytes = 0;
    for (const asset::TextureMip& mip : data->mips)
    {
        textureBytes += mip.bytes.size();
    }
    m_textureBytes.insert_or_assign(id, textureBytes);
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
        m_textureBytes.erase(id);
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
