#include <devex/asset/Artifact.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/runtime/AssetManager.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace devex::runtime {
namespace {

// What a frame hands to the renderer at most: the copy into its staging memory is done on the main
// thread.
constexpr std::size_t finishedBytesPerFrame = std::size_t{64} << 20;

[[nodiscard]] std::size_t sizeOf(const asset::MeshData& mesh) noexcept
{
    return mesh.vertices.size() * sizeof(asset::Vertex) + mesh.indices.size() * sizeof(std::uint32_t) +
           mesh.skin.size() * sizeof(asset::VertexSkin);
}

[[nodiscard]] std::size_t sizeOf(const asset::TextureData& texture) noexcept
{
    std::size_t bytes = 0;
    for (const asset::TextureMip& mip : texture.mips)
    {
        bytes += mip.bytes.size();
    }
    return bytes;
}

} // namespace

AssetManager::AssetManager(render::Renderer* renderer, asset::AssetSource* source, core::JobSystem* jobs) noexcept
    : m_renderer(renderer)
    , m_source(source)
    , m_jobs(jobs)
{
}

AssetManager::~AssetManager()
{
    cancelLoads();
}

void AssetManager::startLoad(asset::AssetId id, asset::AssetType type)
{
    const std::uint64_t request = ++m_nextRequest;
    m_loading.insert_or_assign(id, request);
    asset::ArtifactReader reader = m_source->artifactReader(id);
    // Runs on a worker: it touches nothing but what it was given.
    const auto load = [id, type, request](const asset::ArtifactReader& read) {
        DEVEX_PROFILE_SCOPE(type == asset::AssetType::Mesh ? "Load a mesh" : "Load a texture");
        FinishedLoad finished{.id = id, .type = type, .request = request};
        const core::Result<std::vector<std::byte>> bytes = read();
        if (!bytes)
        {
            finished.error = bytes.error();
            return finished;
        }
        if (type == asset::AssetType::Mesh)
        {
            core::Result<asset::MeshData> mesh = asset::decodeMesh(*bytes);
            if (!mesh)
            {
                finished.error = mesh.error();
                return finished;
            }
            finished.bytes = sizeOf(*mesh);
            finished.data = std::move(*mesh);
        }
        else
        {
            core::Result<asset::TextureData> texture = asset::decodeTexture(*bytes);
            if (!texture)
            {
                finished.error = texture.error();
                return finished;
            }
            finished.bytes = sizeOf(*texture);
            finished.data = std::move(*texture);
        }
        return finished;
    };

    if (m_jobs == nullptr)
    {
        FinishedLoad finished = load(reader);
        static_cast<void>(apply(finished));
        return;
    }
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(m_queue->mutex);
        generation = m_queue->generation;
    }
    m_jobs->schedule([queue = m_queue, reader = std::move(reader), load, generation] {
        {
            // A load the manager no longer awaits does not touch its source, which may be gone.
            const std::scoped_lock lock(queue->mutex);
            if (queue->generation != generation)
            {
                return;
            }
            ++queue->running;
        }
        FinishedLoad finished = load(reader);
        const std::scoped_lock lock(queue->mutex);
        --queue->running;
        if (queue->generation == generation)
        {
            queue->finished.push_back(std::move(finished));
        }
        queue->changed.notify_all();
    });
}

bool AssetManager::apply(FinishedLoad& load)
{
    const auto awaited = m_loading.find(load.id);
    // A reimport asked for a newer version meanwhile, or the asset was forgotten.
    if (awaited == m_loading.end() || awaited->second != load.request)
    {
        return false;
    }
    m_loading.erase(awaited);
    const char* const what = load.type == asset::AssetType::Mesh ? "mesh" : "texture";
    if (load.error)
    {
        DEVEX_LOG_ERROR("Cannot load {} {}: {}", what, load.id.uuid, *load.error);
        m_failed.insert(load.id);
        return false;
    }

    if (auto* const data = std::get_if<asset::MeshData>(&load.data))
    {
        core::Result<render::MeshHandle> handle = m_renderer->createMesh(*data);
        if (!handle)
        {
            DEVEX_LOG_ERROR("Cannot load mesh {}: {}", load.id.uuid, handle.error());
            m_failed.insert(load.id);
            return false;
        }
        LoadedMesh mesh{.handle = *handle, .gpuBytes = load.bytes};
        for (const asset::Submesh& submesh : asset::submeshesOf(*data))
        {
            mesh.submeshMaterials.push_back(submesh.material);
        }
        // A reimported mesh replaces the previous one, which was drawn until now.
        releaseMesh(load.id);
        m_meshes.insert_or_assign(load.id, OwnedMesh{std::move(mesh), true});
        return false;
    }

    const asset::TextureData& data = std::get<asset::TextureData>(load.data);
    core::Result<render::TextureHandle> handle = m_renderer->createTexture(data);
    if (!handle)
    {
        DEVEX_LOG_ERROR("Cannot load texture {}: {}", load.id.uuid, handle.error());
        m_failed.insert(load.id);
        return false;
    }
    releaseTexture(load.id);
    m_textures.insert_or_assign(load.id, *handle);
    m_textureBytes.insert_or_assign(load.id, load.bytes);
    // Kept beside the handle, for the images whose borders stay unstretched.
    if (!data.mips.empty())
    {
        m_textureSizes.insert_or_assign(load.id, math::Extent2D{data.mips.front().width, data.mips.front().height});
    }
    return true;
}

void AssetManager::finishLoads()
{
    finishLoads(finishedBytesPerFrame);
}

void AssetManager::finishLoads(std::size_t byteBudget)
{
    DEVEX_PROFILE_SCOPE("Finish loads");
    {
        const std::scoped_lock lock(m_queue->mutex);
        for (FinishedLoad& finished : m_queue->finished)
        {
            m_finished.push_back(std::move(finished));
        }
        m_queue->finished.clear();
    }
    std::size_t spent = 0;
    bool textures = false;
    while (!m_finished.empty())
    {
        FinishedLoad& next = m_finished.front();
        // The first one always goes, so that a large texture does not wait forever.
        if (spent > 0 && spent + next.bytes > byteBudget)
        {
            break;
        }
        spent += next.bytes;
        textures |= apply(next);
        m_finished.pop_front();
    }
    // Materials waiting for these textures sample them from now on.
    if (textures)
    {
        refreshMaterials();
    }
}

void AssetManager::preload(asset::AssetId id)
{
    const asset::AssetInfo* const info = id.isValid() && m_source != nullptr ? m_source->find(id) : nullptr;
    if (info == nullptr)
    {
        return;
    }
    switch (info->type)
    {
    case asset::AssetType::Mesh:
        static_cast<void>(mesh(id));
        break;
    case asset::AssetType::Texture:
        static_cast<void>(texture(id));
        break;
    case asset::AssetType::Material:
        static_cast<void>(material(id));
        break;
    case asset::AssetType::Font:
        static_cast<void>(font(id));
        break;
    default:
        // The others load when they are asked for.
        break;
    }
}

bool AssetManager::isReady(asset::AssetId id)
{
    if (!id.isValid())
    {
        return true;
    }
    if (const auto found = m_meshes.find(id); found != m_meshes.end())
    {
        return m_renderer == nullptr || m_renderer->isReady(found->second.mesh.handle);
    }
    const asset::AssetInfo* const info = m_source != nullptr ? m_source->find(id) : nullptr;
    if (info == nullptr || m_failed.contains(id))
    {
        // Nothing will come: waiting for it would wait forever.
        return true;
    }
    switch (info->type)
    {
    case asset::AssetType::Mesh:
        return m_renderer == nullptr;
    case asset::AssetType::Texture: {
        const auto found = m_textures.find(id);
        return m_renderer == nullptr || (found != m_textures.end() && m_renderer->isReady(found->second));
    }
    case asset::AssetType::Material: {
        const auto found = m_materials.find(id);
        if (found == m_materials.end())
        {
            return m_renderer == nullptr;
        }
        const asset::MaterialData& data = found->second.data;
        for (const asset::AssetId texture : {data.baseColorTexture, data.metallicRoughnessTexture, data.normalTexture,
                                             data.occlusionTexture, data.emissiveTexture})
        {
            if (!isReady(texture))
            {
                return false;
            }
        }
        return true;
    }
    case asset::AssetType::Font: {
        const auto found = m_fonts.find(id);
        return found != m_fonts.end() &&
               (m_renderer == nullptr || !found->second.atlas.isValid() || m_renderer->isReady(found->second.atlas));
    }
    default:
        return true;
    }
}

std::size_t AssetManager::pendingLoads() const noexcept
{
    return m_loading.size();
}

void AssetManager::waitForLoads()
{
    while (!m_loading.empty() && m_jobs != nullptr)
    {
        {
            std::unique_lock lock(m_queue->mutex);
            m_queue->changed.wait(lock, [this] { return !m_queue->finished.empty(); });
        }
        finishLoads(std::numeric_limits<std::size_t>::max());
    }
    finishLoads(std::numeric_limits<std::size_t>::max());
}

void AssetManager::cancelLoads()
{
    {
        std::unique_lock lock(m_queue->mutex);
        ++m_queue->generation;
        m_queue->changed.wait(lock, [this] { return m_queue->running == 0; });
        m_queue->finished.clear();
    }
    m_finished.clear();
    m_loading.clear();
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
    if (found == m_meshes.end() && !m_loading.contains(id) && canLoad(id))
    {
        startLoad(id, asset::AssetType::Mesh);
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

std::shared_ptr<const asset::ShaderData> AssetManager::materialShader(asset::AssetId id)
{
    if (!material(id).isValid())
    {
        return nullptr;
    }
    const asset::AssetId shaderId = m_materials.at(id).data.shader;
    return shaderId.isValid() ? shader(shaderId) : nullptr;
}

core::Result<void> AssetManager::setShaderParameter(asset::AssetId id, std::string_view name, math::Vec4 value)
{
    const std::shared_ptr<const asset::ShaderData> data = materialShader(id);
    const asset::ShaderParameter* const uniform = data != nullptr ? data->findParameter(name) : nullptr;
    if (uniform == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "the material {} has no shader uniform '{}'", id.uuid, name);
    }
    if (uniform->type == asset::ShaderParameterType::Texture)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "'{}' is a texture: give it a texture", name);
    }
    if (uniform->instance)
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "'{}' is an instance uniform: each object gives it with setInstanceShaderParameter", name);
    }
    LoadedMaterial& loaded = m_materials.at(id);
    asset::MaterialParameter* given = nullptr;
    for (asset::MaterialParameter& parameter : loaded.data.parameters)
    {
        given = parameter.name == name ? &parameter : given;
    }
    if (given == nullptr)
    {
        given = &loaded.data.parameters.emplace_back(asset::MaterialParameter{.name = std::string(name)});
    }
    given->value = value;
    given->components = static_cast<std::uint8_t>(asset::componentCount(uniform->type));
    given->texture = {};
    m_renderer->updateMaterial(loaded.handle, describe(loaded.data));
    m_changedMaterials.insert(id);
    return {};
}

core::Result<void> AssetManager::setShaderTexture(asset::AssetId id, std::string_view name, asset::AssetId textureId)
{
    const std::shared_ptr<const asset::ShaderData> data = materialShader(id);
    const asset::ShaderParameter* const uniform = data != nullptr ? data->findParameter(name) : nullptr;
    if (uniform == nullptr || uniform->type != asset::ShaderParameterType::Texture)
    {
        return core::makeError(core::ErrorCode::NotFound, "the material {} has no texture uniform '{}'", id.uuid, name);
    }
    LoadedMaterial& loaded = m_materials.at(id);
    std::erase_if(loaded.data.parameters, [&](const asset::MaterialParameter& parameter) { return parameter.name == name; });
    loaded.data.parameters.push_back({.name = std::string(name), .texture = textureId});
    m_renderer->updateMaterial(loaded.handle, describe(loaded.data));
    m_changedMaterials.insert(id);
    return {};
}

std::optional<math::Vec4> AssetManager::shaderParameter(asset::AssetId id, std::string_view name)
{
    const std::shared_ptr<const asset::ShaderData> data = materialShader(id);
    const asset::ShaderParameter* const uniform = data != nullptr ? data->findParameter(name) : nullptr;
    if (uniform == nullptr || uniform->type == asset::ShaderParameterType::Texture)
    {
        return std::nullopt;
    }
    const asset::MaterialParameter* const given = m_materials.at(id).data.findParameter(name);
    return given != nullptr && !given->texture.isValid() && !uniform->instance ? given->value : uniform->defaultValue;
}

void AssetManager::resetShaderParameters()
{
    for (const asset::AssetId id : std::exchange(m_changedMaterials, {}))
    {
        if (m_materials.contains(id) && canLoad(id))
        {
            static_cast<void>(loadMaterial(id));
        }
    }
}

render::TextureHandle AssetManager::texture(asset::AssetId id)
{
    auto found = m_textures.find(id);
    if (found == m_textures.end() && !m_loading.contains(id) && canLoad(id))
    {
        startLoad(id, asset::AssetType::Texture);
        found = m_textures.find(id);
    }
    return found != m_textures.end() ? found->second : render::TextureHandle{};
}

core::Result<void> AssetManager::setTexture(asset::AssetId id, const asset::TextureData& data)
{
    if (m_renderer == nullptr)
    {
        return core::makeError(core::ErrorCode::Unsupported, "there is no renderer to make the texture");
    }
    core::Result<render::TextureHandle> handle = m_renderer->createTexture(data);
    if (!handle)
    {
        return std::unexpected(handle.error());
    }
    releaseTexture(id);
    m_textures.insert_or_assign(id, *handle);
    if (!data.mips.empty())
    {
        m_textureSizes.insert_or_assign(id, math::Extent2D{data.mips.front().width, data.mips.front().height});
    }
    m_failed.erase(id);
    return {};
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

std::shared_ptr<const asset::CurveData> AssetManager::curve(asset::AssetId id)
{
    if (const auto found = m_curves.find(id); found != m_curves.end())
    {
        return found->second;
    }
    if (!id.isValid() || m_source == nullptr || m_failed.contains(id) || m_source->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    core::Result<asset::CurveData> data =
        bytes ? asset::decodeCurve(*bytes) : core::Result<asset::CurveData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load curve {}: {}", id.uuid, data.error());
        m_failed.insert(id);
        return nullptr;
    }
    auto curve = std::make_shared<const asset::CurveData>(std::move(*data));
    m_curves.emplace(id, curve);
    return curve;
}

namespace {

// Reads a small asset once, keeps it shared, and remembers the ones that fail.
template <typename Data, typename Decode>
[[nodiscard]] std::shared_ptr<const Data> loadShared(
    asset::AssetId id, asset::AssetSource* source, std::unordered_set<asset::AssetId>& failed,
    std::unordered_map<asset::AssetId, std::shared_ptr<const Data>>& loaded, std::string_view kind, Decode decode)
{
    if (const auto found = loaded.find(id); found != loaded.end())
    {
        return found->second;
    }
    if (!id.isValid() || source == nullptr || failed.contains(id) || source->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = source->loadArtifact(id);
    core::Result<Data> data = bytes ? decode(*bytes) : core::Result<Data>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load {} {}: {}", kind, id.uuid, data.error());
        failed.insert(id);
        return nullptr;
    }
    auto shared = std::make_shared<const Data>(std::move(*data));
    loaded.emplace(id, shared);
    return shared;
}

} // namespace

std::shared_ptr<const asset::SpriteData> AssetManager::sprite(asset::AssetId id)
{
    return loadShared<asset::SpriteData>(id, m_source, m_failed, m_sprites, "sprite",
                                         [](std::span<const std::byte> bytes) { return asset::decodeSprite(bytes); });
}

std::shared_ptr<const asset::SpriteFramesData> AssetManager::spriteFrames(asset::AssetId id)
{
    return loadShared<asset::SpriteFramesData>(
        id, m_source, m_failed, m_spriteFrames, "sprite frames",
        [](std::span<const std::byte> bytes) { return asset::decodeSpriteFrames(bytes); });
}

std::shared_ptr<const asset::TilesetData> AssetManager::tileset(asset::AssetId id)
{
    return loadShared<asset::TilesetData>(id, m_source, m_failed, m_tilesets, "tileset",
                                          [](std::span<const std::byte> bytes) { return asset::decodeTileset(bytes); });
}

std::shared_ptr<const asset::AnimatorData> AssetManager::animator(asset::AssetId id)
{
    return loadShared<asset::AnimatorData>(id, m_source, m_failed, m_animators, "animator",
                                           [](std::span<const std::byte> bytes) { return asset::decodeAnimator(bytes); });
}

std::shared_ptr<const asset::NavMeshData> AssetManager::navMesh(asset::AssetId id)
{
    return loadShared<asset::NavMeshData>(id, m_source, m_failed, m_navMeshes, "navigation mesh",
                                          [](std::span<const std::byte> bytes) { return asset::decodeNavMesh(bytes); });
}

render::ReflectionProbeHandle AssetManager::reflectionProbe(asset::AssetId id)
{
    if (const auto found = m_reflectionProbes.find(id); found != m_reflectionProbes.end())
    {
        return found->second;
    }
    if (!canLoad(id))
    {
        return {};
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    const core::Result<asset::ReflectionProbeData> data =
        bytes ? asset::decodeReflectionProbe(*bytes) : core::Result<asset::ReflectionProbeData>(std::unexpected(bytes.error()));
    core::Result<render::ReflectionProbeHandle> probe =
        data ? m_renderer->createReflectionProbe(*data) : core::Result<render::ReflectionProbeHandle>(std::unexpected(data.error()));
    if (!probe)
    {
        DEVEX_LOG_ERROR("Cannot load reflection probe {}: {}", id.uuid, probe.error());
        m_failed.insert(id);
        return {};
    }
    m_reflectionProbes.emplace(id, *probe);
    return *probe;
}

render::IrradianceVolumeHandle AssetManager::irradianceVolume(asset::AssetId id)
{
    if (const auto found = m_irradianceVolumes.find(id); found != m_irradianceVolumes.end())
    {
        return found->second;
    }
    if (!canLoad(id))
    {
        return {};
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    const core::Result<asset::IrradianceVolumeData> data =
        bytes ? asset::decodeIrradianceVolume(*bytes) : core::Result<asset::IrradianceVolumeData>(std::unexpected(bytes.error()));
    core::Result<render::IrradianceVolumeHandle> volume =
        data ? m_renderer->createIrradianceVolume(*data) : core::Result<render::IrradianceVolumeHandle>(std::unexpected(data.error()));
    if (!volume)
    {
        DEVEX_LOG_ERROR("Cannot load irradiance volume {}: {}", id.uuid, volume.error());
        m_failed.insert(id);
        return {};
    }
    m_irradianceVolumes.emplace(id, *volume);
    return *volume;
}

std::shared_ptr<const asset::TranslationData> AssetManager::translation(asset::AssetId id)
{
    return loadShared<asset::TranslationData>(id, m_source, m_failed, m_translations, "translation table",
                                              [](std::span<const std::byte> bytes) { return asset::decodeTranslation(bytes); });
}

namespace {

// Rows of the textures of a terrain, from `first` for `count` of them.
[[nodiscard]] std::vector<std::byte> terrainHeightRows(const asset::TerrainMaps& maps, std::uint32_t first, std::uint32_t count)
{
    std::vector<std::byte> rows(std::size_t{maps.resolution} * count * sizeof(float));
    std::memcpy(rows.data(), maps.heights.data() + std::size_t{first} * maps.resolution, rows.size());
    return rows;
}

// The slopes between the samples around each one, in meters of height per cell, as half floats.
[[nodiscard]] std::vector<std::byte> terrainSlopeRows(const asset::TerrainMaps& maps, std::uint32_t first, std::uint32_t count)
{
    const std::uint32_t resolution = maps.resolution;
    const std::uint32_t last = resolution - 1;
    std::vector<std::uint16_t> halves(std::size_t{resolution} * count * 4);
    const auto height = [&maps, resolution](std::uint32_t x, std::uint32_t z) { return maps.heights[std::size_t{z} * resolution + x]; };
    for (std::uint32_t row = 0; row < count; ++row)
    {
        const std::uint32_t z = first + row;
        const std::uint32_t back = z > 0 ? z - 1 : z;
        const std::uint32_t front = std::min(z + 1, last);
        for (std::uint32_t x = 0; x < resolution; ++x)
        {
            const std::uint32_t left = x > 0 ? x - 1 : x;
            const std::uint32_t right = std::min(x + 1, last);
            const float slopeX = (height(right, z) - height(left, z)) / static_cast<float>(right - left);
            const float slopeZ = (height(x, front) - height(x, back)) / static_cast<float>(front - back);
            std::uint16_t* const texel = halves.data() + (std::size_t{row} * resolution + x) * 4;
            texel[0] = glm::packHalf1x16(slopeX);
            texel[1] = glm::packHalf1x16(slopeZ);
            texel[2] = 0;
            texel[3] = glm::packHalf1x16(1.0f);
        }
    }
    std::vector<std::byte> rows(halves.size() * sizeof(std::uint16_t));
    std::memcpy(rows.data(), halves.data(), rows.size());
    return rows;
}

// The shares of four layers painted, from firstLayer.
[[nodiscard]] std::vector<std::byte> terrainPaintRows(const asset::TerrainMaps& maps, std::uint32_t first, std::uint32_t count,
                                                      std::uint32_t firstLayer)
{
    std::vector<std::byte> rows(std::size_t{maps.resolution} * count * 4);
    for (std::size_t texel = 0; texel < std::size_t{maps.resolution} * count; ++texel)
    {
        const std::size_t sample = std::size_t{first} * maps.resolution + texel;
        for (std::uint32_t channel = 0; channel < 4; ++channel)
        {
            rows[texel * 4 + channel] = static_cast<std::byte>(maps.paint[sample * asset::maxTerrainLayers + firstLayer + channel]);
        }
    }
    return rows;
}

// The holes in red, the grass in green.
[[nodiscard]] std::vector<std::byte> terrainDetailRows(const asset::TerrainMaps& maps, std::uint32_t first, std::uint32_t count)
{
    std::vector<std::byte> rows(std::size_t{maps.resolution} * count * 4);
    for (std::size_t texel = 0; texel < std::size_t{maps.resolution} * count; ++texel)
    {
        const std::size_t sample = std::size_t{first} * maps.resolution + texel;
        rows[texel * 4] = static_cast<std::byte>(maps.holes[sample]);
        rows[texel * 4 + 1] = static_cast<std::byte>(maps.grass[sample]);
        rows[texel * 4 + 2] = std::byte{0};
        rows[texel * 4 + 3] = std::byte{255};
    }
    return rows;
}

[[nodiscard]] asset::TextureData terrainTexture(asset::TextureFormat format, asset::TextureFilter filter, std::uint32_t resolution,
                                                std::vector<std::byte> bytes)
{
    asset::TextureData texture{.format = format, .filter = filter};
    texture.mips.push_back({.width = resolution, .height = resolution, .bytes = std::move(bytes)});
    return texture;
}

// The lowest and highest heights and the most grass of the blocks a region of samples touches.
void updateBlocks(AssetManager::LoadedTerrain& terrain, const asset::TerrainRegion& region)
{
    const asset::TerrainMaps& maps = terrain.maps;
    const std::uint32_t cells = render::terrainBlockCells;
    const std::uint32_t blocks = (maps.resolution - 1) / cells;
    terrain.blockHeights.resize(std::size_t{blocks} * blocks);
    terrain.blockGrass.resize(std::size_t{blocks} * blocks);
    // A sample on the edge of a block belongs to the blocks on both sides.
    const std::uint32_t firstX = region.x > 0 ? (region.x - 1) / cells : 0;
    const std::uint32_t firstZ = region.z > 0 ? (region.z - 1) / cells : 0;
    const std::uint32_t lastX = std::min((region.x + region.width) / cells, blocks - 1);
    const std::uint32_t lastZ = std::min((region.z + region.height) / cells, blocks - 1);
    for (std::uint32_t blockZ = firstZ; blockZ <= lastZ; ++blockZ)
    {
        for (std::uint32_t blockX = firstX; blockX <= lastX; ++blockX)
        {
            math::Vec2 range{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
            std::uint8_t grass = 0;
            for (std::uint32_t z = blockZ * cells; z <= (blockZ + 1) * cells; ++z)
            {
                for (std::uint32_t x = blockX * cells; x <= (blockX + 1) * cells; ++x)
                {
                    const std::size_t sample = std::size_t{z} * maps.resolution + x;
                    range = {std::min(range.x, maps.heights[sample]), std::max(range.y, maps.heights[sample])};
                    grass = std::max(grass, maps.grass[sample]);
                }
            }
            terrain.blockHeights[std::size_t{blockZ} * blocks + blockX] = range;
            terrain.blockGrass[std::size_t{blockZ} * blocks + blockX] = grass;
        }
    }
}

} // namespace

AssetManager::LoadedTerrain* AssetManager::terrain(asset::AssetId id)
{
    if (const auto found = m_terrains.find(id); found != m_terrains.end())
    {
        return &found->second;
    }
    // The maps load without a renderer as well, for the physics of headless tools and tests.
    if (!id.isValid() || m_source == nullptr || m_failed.contains(id) || m_source->find(id) == nullptr)
    {
        return nullptr;
    }
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    core::Result<asset::TerrainMaps> maps =
        bytes ? asset::decodeTerrain(*bytes) : core::Result<asset::TerrainMaps>(std::unexpected(bytes.error()));
    if (!maps)
    {
        DEVEX_LOG_ERROR("Cannot load terrain {}: {}", id.uuid, maps.error());
        m_failed.insert(id);
        return nullptr;
    }
    LoadedTerrain& loaded = m_terrains[id];
    loaded.maps = std::move(*maps);
    updateBlocks(loaded, {.width = loaded.maps.resolution, .height = loaded.maps.resolution});
    if (m_renderer != nullptr)
    {
        const asset::TerrainMaps& data = loaded.maps;
        const std::uint32_t size = data.resolution;
        render::TerrainTextures& textures = loaded.textures;
        const auto create = [this, id](asset::TextureData texture) {
            core::Result<render::TextureHandle> created = m_renderer->createTexture(texture);
            if (!created)
            {
                DEVEX_LOG_ERROR("Cannot upload terrain {}: {}", id.uuid, created.error());
                return render::TextureHandle{};
            }
            return *created;
        };
        textures.heights = create(terrainTexture(asset::TextureFormat::R32Float, asset::TextureFilter::Nearest, size,
                                                 terrainHeightRows(data, 0, size)));
        textures.slopes = create(terrainTexture(asset::TextureFormat::Rgba16Float, asset::TextureFilter::Linear, size,
                                                terrainSlopeRows(data, 0, size)));
        textures.paint0 = create(terrainTexture(asset::TextureFormat::Rgba8Unorm, asset::TextureFilter::Linear, size,
                                                terrainPaintRows(data, 0, size, 0)));
        textures.paint1 = create(terrainTexture(asset::TextureFormat::Rgba8Unorm, asset::TextureFilter::Linear, size,
                                                terrainPaintRows(data, 0, size, 4)));
        textures.detail = create(terrainTexture(asset::TextureFormat::Rgba8Unorm, asset::TextureFilter::Linear, size,
                                                terrainDetailRows(data, 0, size)));
    }
    return &loaded;
}

void AssetManager::terrainChanged(asset::AssetId id, const asset::TerrainRegion& region)
{
    const auto found = m_terrains.find(id);
    if (found == m_terrains.end() || region.empty())
    {
        return;
    }
    LoadedTerrain& terrain = found->second;
    ++terrain.version;
    terrain.edited = true;
    updateBlocks(terrain, region);
    if (m_renderer == nullptr)
    {
        return;
    }
    const asset::TerrainMaps& maps = terrain.maps;
    const std::uint32_t last = maps.resolution - 1;
    const std::uint32_t first = std::min(region.z, last);
    const std::uint32_t count = std::min(region.height, maps.resolution - first);
    // The slopes of the rows around the region change with it.
    const std::uint32_t slopeFirst = first > 0 ? first - 1 : 0;
    const std::uint32_t slopeCount = std::min(first + count + 1, maps.resolution) - slopeFirst;
    const auto update = [this, id](render::TextureHandle texture, std::uint32_t top, std::uint32_t rows, const std::vector<std::byte>& bytes) {
        if (core::Result<void> updated = m_renderer->updateTexture(texture, top, rows, bytes); !updated && texture.isValid())
        {
            DEVEX_LOG_ERROR("Cannot update terrain {}: {}", id.uuid, updated.error());
        }
    };
    update(terrain.textures.heights, first, count, terrainHeightRows(maps, first, count));
    update(terrain.textures.slopes, slopeFirst, slopeCount, terrainSlopeRows(maps, slopeFirst, slopeCount));
    update(terrain.textures.paint0, first, count, terrainPaintRows(maps, first, count, 0));
    update(terrain.textures.paint1, first, count, terrainPaintRows(maps, first, count, 4));
    update(terrain.textures.detail, first, count, terrainDetailRows(maps, first, count));
}

void AssetManager::terrainSaved(asset::AssetId id)
{
    if (const auto found = m_terrains.find(id); found != m_terrains.end())
    {
        found->second.edited = false;
    }
}

void AssetManager::releaseTerrain(asset::AssetId id)
{
    const auto found = m_terrains.find(id);
    if (found == m_terrains.end())
    {
        return;
    }
    if (m_renderer != nullptr)
    {
        const render::TerrainTextures& textures = found->second.textures;
        for (const render::TextureHandle texture : {textures.heights, textures.slopes, textures.paint0, textures.paint1, textures.detail})
        {
            m_renderer->destroyTexture(texture);
        }
    }
    m_terrains.erase(found);
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
                (found != m_meshes.end() && found->second.owned) || m_loading.contains(event.id))
            {
                // The previous version is drawn until the new one is loaded.
                if (removed)
                {
                    releaseMesh(event.id);
                    m_loading.erase(event.id);
                }
                else if (canLoad(event.id))
                {
                    startLoad(event.id, asset::AssetType::Mesh);
                }
            }
            break;
        case asset::AssetType::Texture:
            if (m_textures.contains(event.id) || m_loading.contains(event.id))
            {
                if (removed)
                {
                    releaseTexture(event.id);
                    m_loading.erase(event.id);
                }
                else if (canLoad(event.id))
                {
                    startLoad(event.id, asset::AssetType::Texture);
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
        case asset::AssetType::Curve:
            // Read again when a tween asks for it.
            m_curves.erase(event.id);
            break;
        case asset::AssetType::Sprite:
            m_sprites.erase(event.id);
            break;
        case asset::AssetType::SpriteFrames:
            m_spriteFrames.erase(event.id);
            break;
        case asset::AssetType::Tileset:
            m_tilesets.erase(event.id);
            break;
        case asset::AssetType::Animator:
            m_animators.erase(event.id);
            break;
        case asset::AssetType::NavMesh:
            m_navMeshes.erase(event.id);
            break;
        case asset::AssetType::ReflectionProbe:
            // Loaded again when a frame draws it.
            if (const auto found = m_reflectionProbes.find(event.id); found != m_reflectionProbes.end())
            {
                m_renderer->destroyReflectionProbe(found->second);
                m_reflectionProbes.erase(found);
            }
            break;
        case asset::AssetType::IrradianceVolume:
            if (const auto found = m_irradianceVolumes.find(event.id); found != m_irradianceVolumes.end())
            {
                m_renderer->destroyIrradianceVolume(found->second);
                m_irradianceVolumes.erase(found);
            }
            break;
        case asset::AssetType::Translation:
            m_translations.erase(event.id);
            break;
        case asset::AssetType::Terrain:
            // Loaded again when a frame draws it, unless the editor changed it since it was saved.
            if (const auto found = m_terrains.find(event.id); found != m_terrains.end() && (removed || !found->second.edited))
            {
                releaseTerrain(event.id);
            }
            break;
        case asset::AssetType::Shader:
            if (m_shaders.contains(event.id))
            {
                // Its pipelines are rebuilt under the same handle, and its materials follow its uniforms.
                if (removed || !loadShader(event.id))
                {
                    releaseShader(event.id);
                }
                texturesChanged = true;
            }
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
    // The workers read from the source that goes away.
    cancelLoads();
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
    while (!m_shaders.empty())
    {
        releaseShader(m_shaders.begin()->first);
    }
    for (const auto& [id, probe] : m_reflectionProbes)
    {
        m_renderer->destroyReflectionProbe(probe);
    }
    m_reflectionProbes.clear();
    for (const auto& [id, volume] : m_irradianceVolumes)
    {
        m_renderer->destroyIrradianceVolume(volume);
    }
    m_irradianceVolumes.clear();
    while (!m_terrains.empty())
    {
        releaseTerrain(m_terrains.begin()->first);
    }
    m_models.clear();
    m_meshData.clear();
    m_sceneTexts.clear();
    m_audioClips.clear();
    m_animationClips.clear();
    m_animationBytes.clear();
    m_themes.clear();
    m_curves.clear();
    m_sprites.clear();
    m_spriteFrames.clear();
    m_tilesets.clear();
    m_animators.clear();
    m_navMeshes.clear();
    m_failed.clear();
    m_source = source;
}

bool AssetManager::canLoad(asset::AssetId id) const
{
    // Assets still importing are not in the database yet; their import event triggers a retry.
    return id.isValid() && m_renderer != nullptr && m_source != nullptr &&
           !m_failed.contains(id) && m_source->find(id) != nullptr;
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

std::shared_ptr<const asset::ShaderData> AssetManager::shader(asset::AssetId id)
{
    auto found = m_shaders.find(id);
    if (found == m_shaders.end() && id.isValid() && m_source != nullptr && !m_failed.contains(id) &&
        m_source->find(id) != nullptr && loadShader(id))
    {
        found = m_shaders.find(id);
    }
    return found != m_shaders.end() ? found->second.data : nullptr;
}

bool AssetManager::loadShader(asset::AssetId id)
{
    const core::Result<std::vector<std::byte>> bytes = m_source->loadArtifact(id);
    core::Result<asset::ShaderData> data =
        bytes ? asset::decodeShader(*bytes) : core::Result<asset::ShaderData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_ERROR("Cannot load shader {}: {}", id.uuid, data.error());
        m_failed.insert(id);
        return false;
    }
    const asset::AssetInfo* const info = m_source->find(id);
    const std::string name = info != nullptr ? info->name : id.uuid.toString();
    if (!data->compiled())
    {
        DEVEX_LOG_WARNING("Shader {} does not compile: its materials draw as the default material", name);
    }

    LoadedShader& loaded = m_shaders[id];
    if (m_renderer != nullptr && data->compiled())
    {
        if (loaded.handle.isValid())
        {
            if (core::Result<void> updated = m_renderer->updateShader(loaded.handle, *data); !updated)
            {
                DEVEX_LOG_ERROR("Cannot build the pipelines of shader {}: {}", name, updated.error());
            }
        }
        else if (core::Result<render::ShaderHandle> created = m_renderer->createShader(*data))
        {
            loaded.handle = *created;
        }
        else
        {
            DEVEX_LOG_ERROR("Cannot build the pipelines of shader {}: {}", name, created.error());
        }
    }
    else if (loaded.handle.isValid())
    {
        // It no longer compiles: its materials draw as the default one until it does again.
        m_renderer->destroyShader(loaded.handle);
        loaded.handle = {};
    }
    loaded.data = std::make_shared<const asset::ShaderData>(std::move(*data));
    return true;
}

void AssetManager::releaseShader(asset::AssetId id)
{
    if (const auto found = m_shaders.find(id); found != m_shaders.end())
    {
        if (m_renderer != nullptr && found->second.handle.isValid())
        {
            m_renderer->destroyShader(found->second.handle);
        }
        m_shaders.erase(found);
    }
}

render::MaterialDesc AssetManager::describe(const asset::MaterialData& material)
{
    // A shader of the project: its uniforms, by name, or their defaults.
    render::ShaderHandle shaderHandle;
    std::vector<math::Vec4> parameters;
    std::vector<render::MaterialTexture> textures;
    if (material.shader.isValid())
    {
        const std::shared_ptr<const asset::ShaderData> data = shader(material.shader);
        const auto found = m_shaders.find(material.shader);
        if (data != nullptr && found != m_shaders.end() && found->second.handle.isValid())
        {
            shaderHandle = found->second.handle;
            for (std::uint32_t slot = 0; slot < data->parameters.size(); ++slot)
            {
                const asset::ShaderParameter& uniform = data->parameters[slot];
                // An instance uniform: the default of the objects that give it nothing.
                const asset::MaterialParameter* const given = uniform.instance ? nullptr : material.findParameter(uniform.name);
                if (uniform.type != asset::ShaderParameterType::Texture)
                {
                    parameters.push_back(given != nullptr && !given->texture.isValid() ? given->value : uniform.defaultValue);
                    continue;
                }
                parameters.emplace_back(0.0f);
                textures.push_back({
                    .slot = slot,
                    .texture = given != nullptr ? texture(given->texture) : render::TextureHandle{},
                    .fallback = uniform.hint == asset::ShaderHint::Black    ? render::TextureFallback::Black
                                : uniform.hint == asset::ShaderHint::Normal ? render::TextureFallback::Normal
                                                                            : render::TextureFallback::White,
                });
            }
        }
    }
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
        .shader = shaderHandle,
        .parameters = std::move(parameters),
        .textures = std::move(textures),
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
