// The 3D of the OpenGL renderer, as WebGpuScene.cpp is WebGPU's: the light of the environment baked from the sky by
// triangles over the faces of cubes, the meshes of the frame by batches whose instances are attributes, each given the
// local lights that shine on it, the shadows of the sun and of the local lights, the blended surfaces among the sprites,
// and the luminance measured for the automatic exposure.
#include "GlRenderer.hpp"

#include "../Lighting.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Photometry.hpp>
#include <devex/render/ShadowAtlas.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace devex::render::opengl {
namespace {

using namespace gl;

// The uniform blocks of shaders/gl_scene.slang, gl_mesh.slang, gl_shadow.slang and gl_ibl.slang, as std140 lays them out.
struct SceneData
{
    math::Mat4 viewProjection{1.0f};
    math::Mat4 view{1.0f};
    math::Vec3 cameraPosition{0.0f};
    float exposure = 1.0f;
    math::Vec3 sunDirection{0.0f, -1.0f, 0.0f};
    std::uint32_t sunEnabled = 0;
    math::Vec3 sunIlluminance{0.0f};
    std::uint32_t lightCount = 0;
    math::Vec3 sky{1.0f};
    float environmentRotation = 0.0f;
    float specularMipCount = 1.0f;
    std::uint32_t padding0 = 0;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    std::array<math::Mat4, 4> cascadeViewProjections{};
    math::Vec4 cascadeSplits{0.0f};
    math::Vec4 cascadeTexelSizes{0.0f};
    float shadowDistance = 0.0f;
    std::uint32_t sunCastsShadows = 0;
    float time = 0.0f;
    float padding1 = 0.0f;
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Vec3 fogColor{0.0f};
    float fogDensity = 0.0f;
    float fogHeight = 0.0f;
    float fogFalloff = 1.0f;
    float fogStart = 0.0f;
    float fogMaxOpacity = 1.0f;
    float fogSunScattering = 0.0f;
    float fogSunAnisotropy = 0.0f;
    float fogSkyBlend = 0.0f;
    float padding2 = 0.0f;
    math::Mat4 inverseViewProjection{1.0f};
};
constexpr std::size_t maxLights = 32;
constexpr std::size_t maxShadowViews = 64;
constexpr std::size_t lightsPerObject = 8;
constexpr std::size_t maxDecals = 64;
constexpr std::size_t decalsPerObject = 8;
// The atlases of the decals: as wide as gl_mesh.slang says, every texture no larger than half of it, as WebGPU's.
constexpr std::uint32_t decalAtlasSize = 2048;
// A decal of the frame, as gl_mesh.slang reads it: its box, its fades, and where its textures lie in the atlases.
struct AtlasDecalData
{
    math::Mat4 worldToDecal{1.0f};
    math::Vec4 axisX{0.0f};
    math::Vec4 axisZ{0.0f};
    math::Vec4 axisY{0.0f};
    math::Vec4 modulate{1.0f};
    float albedoMix = 1.0f;
    float emissionEnergy = 1.0f;
    float normalFade = 0.0f;
    float upperFade = 0.3f;
    float lowerFade = 0.3f;
    float fadeBegin = 0.0f;
    float fadeLength = 0.0f;
    float padding0 = 0.0f;
    std::array<math::Vec4, 4> rects{};
};
struct DecalData
{
    std::array<AtlasDecalData, maxDecals> decals{};
};
struct LightData
{
    std::array<GpuLight, maxLights> lights{};
};
struct ShadowViewData
{
    std::array<ShadowView, maxShadowViews> views{};
};
struct MaterialData
{
    math::Vec4 baseColorFactor{1.0f};
    math::Vec3 emissiveFactor{0.0f};
    float alphaCutoff = 0.5f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    std::uint32_t alphaMode = 0;
    std::uint32_t doubleSided = 0;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};
// An instance as gl_mesh.slang reads its attributes: the columns of its matrix, the bytes of its lights, its bones, the
// bytes of its decals.
struct InstanceData
{
    math::Mat4 world{1.0f};
    std::array<std::uint32_t, 2> lights{0xFFFFFFFFU, 0xFFFFFFFFU};
    std::uint32_t firstBone = 0;
    std::uint32_t boneCount = 0;
    std::array<std::uint32_t, 2> decals{0xFFFFFFFFU, 0xFFFFFFFFU};
};
struct ParticleDrawData
{
    std::uint32_t facing = 0;
    std::uint32_t flags = 0;
    std::uint32_t textured = 0;
    std::uint32_t padding0 = 0;
    math::Vec2 sheet{1.0f};
    float softness = 0.0f;
    std::uint32_t padding1 = 0;
    std::array<std::uint32_t, 2> lights{0xFFFFFFFFU, 0xFFFFFFFFU};
    std::uint32_t padding2 = 0;
    std::uint32_t padding3 = 0;
};
// A terrain as gl_terrain.slang reads it, with the local lights that shine on it.
struct TerrainData
{
    math::Vec3 origin{0.0f};
    float size = 1.0f;
    std::uint32_t resolution = 0;
    std::uint32_t layerCount = 0;
    float textureSize = 4.0f;
    float blendSharpness = 0.5f;
    std::uint32_t triplanar = 0;
    std::int32_t autoBaseLayer = 0;
    std::int32_t autoSlopeLayer = -1;
    float autoSlope = 0.0f;
    std::int32_t autoHeightLayer = -1;
    float autoHeight = 0.0f;
    float autoBlend = 0.0f;
    float windStrength = 0.0f;
    float windSpeed = 0.0f;
    float grassDistance = 0.0f;
    float leafRange = 0.0f;
    std::uint32_t gridCells = terrainBlockCells;
    math::Vec3 grassColor{0.0f};
    float grassHeight = 0.0f;
    math::Vec3 grassTipColor{0.0f};
    float grassWidth = 0.0f;
    std::array<math::Vec4, 8> baseColorFactors{};
    std::array<math::Vec4, 8> layerFactors{};
    std::array<std::uint32_t, 2> lights{0xFFFFFFFFU, 0xFFFFFFFFU};
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};
struct TerrainDrawData
{
    math::Vec3 cameraPosition{0.0f};
    std::uint32_t bladesPerTile = 1;
    float bladeWidthScale = 1.0f;
    float time = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};
struct BlitData
{
    float targetWidth = 1.0f;
    float targetHeight = 1.0f;
    float sourceLevels = 1.0f;
    float padding0 = 0.0f;
    float originX = 0.0f;
    float originY = 0.0f;
    float padding1 = 0.0f;
    float padding2 = 0.0f;
};
// A segment of a ribbon as gl_particle.slang reads it: the point it starts at, then the one it ends at.
struct RibbonSegment
{
    GpuTrailPoint from;
    GpuTrailPoint to;
};
struct BakeData
{
    std::uint32_t size = 0;
    float roughness = 0.0f;
    std::uint32_t sampleCount = 0;
    float sourceLod = 0.0f;
    float sourceSize = 0.0f;
    std::uint32_t face = 0;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};
static_assert(sizeof(SceneData) == 688 && sizeof(GpuLight) == 64 && sizeof(LightData) == 2048 && sizeof(ShadowView) == 80 &&
              sizeof(MaterialData) == 64 && sizeof(InstanceData) == 88 && sizeof(BakeData) == 32 && sizeof(ParticleDrawData) == 48 &&
              sizeof(GpuParticle) == 64 && sizeof(RibbonSegment) == 96 && sizeof(TerrainData) == 384 && sizeof(TerrainDrawData) == 32 &&
              sizeof(GpuTerrainNode) == 16 && sizeof(BlitData) == 32 && sizeof(AtlasDecalData) == 224 && sizeof(DecalData) == 14336);

// The textures of the layers of a terrain: as large, and with as many levels, as WebGPU's.
constexpr std::uint32_t terrainLayerSize = 512;
constexpr std::uint32_t terrainLayerMips = 10;

// The sizes and the samples of the bakes, as WebGPU's.
constexpr std::uint32_t radianceSize = 512;
constexpr std::uint32_t specularSize = 256;
constexpr std::uint32_t specularMipCount = 6;
constexpr std::uint32_t irradianceSize = 32;
constexpr std::uint32_t brdfTableSize = 128;
constexpr std::uint32_t prefilterSamples = 256;
constexpr std::uint32_t irradianceSamples = 512;
constexpr std::uint32_t brdfSamples = 1024;
constexpr std::uint32_t shadowAtlasCell = 512;
constexpr std::size_t largeShadowLights = 2;
// The grid the luminance is measured on.
constexpr std::uint32_t luminanceGridWidth = 64;
constexpr std::uint32_t luminanceGridHeight = 36;
// How many bones a row of the texture of bones holds, four texels each.
constexpr std::uint32_t bonesPerRow = 256;

constexpr std::uint32_t skinnedFlag = 1;
constexpr std::uint32_t blendedFlag = 2;
constexpr std::uint32_t bothSidesFlag = 4;

// Half floats of 1.
constexpr std::array<std::uint16_t, 4> halfWhite{0x3C00, 0x3C00, 0x3C00, 0x3C00};

// Vulkan's clip space, Y down, which the matrices of the shadows read: drawn so in OpenGL, whose maps start from their
// bottom row, they are read back with the same coordinates. Only the depth, from 0 to 1, is stretched over that of OpenGL.
[[nodiscard]] math::Mat4 shadowCorrection() noexcept
{
    math::Mat4 correction{1.0f};
    correction[2][2] = 2.0f;
    correction[3][2] = -1.0f;
    return correction;
}

[[nodiscard]] math::Mat4 flipY() noexcept
{
    math::Mat4 flip{1.0f};
    flip[1][1] = -1.0f;
    return flip;
}

void bindTextureTarget(GLint unit, GLenum target, GLuint texture, GLuint sampler)
{
    gl::ActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    gl::BindTexture(target, texture);
    gl::BindSampler(static_cast<GLuint>(unit), sampler);
}

[[nodiscard]] GLuint makeSampler(GLenum minify, GLenum magnify, GLenum wrapS, GLenum wrapT)
{
    GLuint sampler = 0;
    gl::GenSamplers(1, &sampler);
    gl::SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minify));
    gl::SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magnify));
    gl::SamplerParameteri(sampler, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrapS));
    gl::SamplerParameteri(sampler, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrapT));
    gl::SamplerParameteri(sampler, GL_TEXTURE_WRAP_R, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    return sampler;
}

// A framebuffer that writes only depth, into `texture` or into one layer of it.
[[nodiscard]] GLuint depthFramebuffer(GLuint texture, std::optional<GLint> layer)
{
    GLuint framebuffer = 0;
    gl::GenFramebuffers(1, &framebuffer);
    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (layer)
    {
        gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture, 0, *layer);
    }
    else
    {
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    }
    const GLenum none = GL_NONE;
    gl::DrawBuffers(1, &none);
    gl::ReadBuffer(GL_NONE);
    return framebuffer;
}

} // namespace

core::Result<void> GlRenderer::createSceneResources()
{
    const auto link = [this](Program& program, std::string_view vertex, std::string_view fragment) -> core::Result<void> {
        core::Result<Program> linked = linkProgram(vertex, fragment);
        if (!linked)
        {
            return std::unexpected(linked.error());
        }
        program = *linked;
        return {};
    };
    const std::array<std::tuple<Program*, std::string_view, std::string_view>, 15> programs{{
        {&m_groundProgram, "groundVertex", "groundFragment"},
        {&m_groundShadowProgram, "groundShadowVertex", "groundShadowFragment"},
        {&m_grassProgram, "grassVertex", "grassFragment"},
        {&m_blitProgram, "blitVertex", "blitFragment"},
        {&m_particleProgram, "particleVertex", "particleFragment"},
        {&m_ribbonProgram, "ribbonVertex", "ribbonFragment"},
        {&m_meshProgram, "meshVertex", "meshFragment"},
        {&m_skinnedProgram, "skinnedVertex", "meshFragment"},
        {&m_shadowProgram, "shadowVertex", "shadowFragment"},
        {&m_skinnedShadowProgram, "skinnedShadowVertex", "shadowFragment"},
        {&m_cubeProgram, "bakeVertex", "equirectangularToCube"},
        {&m_prefilterProgram, "bakeVertex", "prefilterSpecular"},
        {&m_irradianceProgram, "bakeVertex", "irradiance"},
        {&m_brdfProgram, "bakeVertex", "brdfTable"},
        {&m_luminanceProgram, "bakeVertex", "luminanceFragment"},
    }};
    for (const auto& [program, vertex, fragment] : programs)
    {
        if (core::Result<void> linked = link(*program, vertex, fragment); !linked)
        {
            return linked;
        }
    }
    for (const Program* mesh : {&m_meshProgram, &m_skinnedProgram})
    {
        bindProgram(*mesh,
                    {{"SceneData", binding::scene},
                     {"LightData", binding::lights},
                     {"ShadowViewData", binding::shadowViews},
                     {"MaterialData", binding::material},
                     {"DecalData", binding::decals}},
                    {{"irradianceEnvironment", unit::irradiance},
                     {"specularEnvironment", unit::specular},
                     {"brdfLut", unit::brdf},
                     {"cascadeMap", unit::cascades},
                     {"atlasMap", unit::atlas},
                     {"baseColorTexture", unit::first},
                     {"metallicRoughnessTexture", unit::second},
                     {"normalTexture", unit::third},
                     {"occlusionTexture", unit::fourth},
                     {"emissiveTexture", unit::fifth},
                     {"boneTexture", unit::bones},
                     {"decalColors", unit::decalColors},
                     {"decalSurfaces", unit::decalSurfaces}});
    }
    for (const Program* particles : {&m_particleProgram, &m_ribbonProgram})
    {
        bindProgram(*particles,
                    {{"SceneData", binding::scene},
                     {"LightData", binding::lights},
                     {"ShadowViewData", binding::shadowViews},
                     {"ParticleDraw", binding::particleDraw}},
                    {{"irradianceEnvironment", unit::irradiance},
                     {"specularEnvironment", unit::specular},
                     {"brdfLut", unit::brdf},
                     {"cascadeMap", unit::cascades},
                     {"atlasMap", unit::atlas},
                     {"particleTexture", unit::first},
                     {"sceneDepth", unit::second}});
    }
    for (const Program* terrain : {&m_groundProgram, &m_groundShadowProgram, &m_grassProgram})
    {
        bindProgram(*terrain,
                    {{"SceneData", binding::scene},
                     {"LightData", binding::lights},
                     {"ShadowViewData", binding::shadowViews},
                     {"TerrainData", binding::terrain},
                     {"TerrainDrawData", binding::terrainDraw},
                     {"ViewData", binding::shadowView}},
                    {{"irradianceEnvironment", unit::irradiance},
                     {"specularEnvironment", unit::specular},
                     {"brdfLut", unit::brdf},
                     {"cascadeMap", unit::cascades},
                     {"atlasMap", unit::atlas},
                     {"heightMap", unit::first},
                     {"slopeMap", unit::second},
                     {"paintMap0", unit::third},
                     {"paintMap1", unit::fourth},
                     {"detailMap", unit::fifth},
                     {"layerColors", unit::layerColors},
                     {"layerSurfaces", unit::layerSurfaces},
                     {"layerNormals", unit::layerNormals}});
    }
    bindProgram(m_blitProgram, {{"BlitData", binding::blit}}, {{"sourceImage", unit::first}});
    for (const Program* shadow : {&m_shadowProgram, &m_skinnedShadowProgram})
    {
        bindProgram(*shadow, {{"ViewData", binding::shadowView}, {"MaterialData", binding::material}},
                    {{"baseColorTexture", unit::first}, {"boneTexture", unit::bones}});
    }
    for (const Program* bake : {&m_cubeProgram, &m_prefilterProgram, &m_irradianceProgram, &m_brdfProgram, &m_luminanceProgram})
    {
        bindProgram(*bake, {{"BakeData", binding::bake}},
                    {{"equirectangular", unit::first}, {"radiance", unit::second}, {"sceneTexture", unit::third}});
    }

    const auto uniform = [](GLuint& buffer, std::size_t size) {
        gl::GenBuffers(1, &buffer);
        gl::BindBuffer(GL_UNIFORM_BUFFER, buffer);
        gl::BufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
    };
    uniform(m_sceneBuffer, sizeof(SceneData));
    uniform(m_lightBuffer, sizeof(LightData));
    uniform(m_shadowViewBuffer, sizeof(ShadowViewData));
    uniform(m_bakeBuffer, sizeof(BakeData));
    gl::GenBuffers(1, &m_shadowMatrixBuffer);
    gl::GenBuffers(1, &m_instanceBuffer);
    gl::GenBuffers(1, &m_particleBuffer);
    gl::GenBuffers(1, &m_ribbonBuffer);
    gl::GenBuffers(1, &m_particleDrawBuffer);
    gl::GenBuffers(1, &m_terrainNodeBuffer);
    gl::GenBuffers(1, &m_grassTileBuffer);
    gl::GenBuffers(1, &m_terrainBuffer);
    gl::GenBuffers(1, &m_terrainDrawBuffer);
    uniform(m_blitBuffer, sizeof(BlitData));
    uniform(m_decalBuffer, sizeof(DecalData));
    const auto aligned = [this](std::size_t size) {
        const auto alignment = static_cast<std::size_t>(m_uniformAlignment);
        return (size + alignment - 1) / alignment * alignment;
    };
    m_terrainStride = aligned(sizeof(TerrainData));
    m_terrainDrawStride = aligned(sizeof(TerrainDrawData));
    // A block is a grid of terrainBlockCells cells on a side, its four quarters one after the other, as Vulkan has it, so
    // that a block can draw a quarter of itself where its smaller blocks are not drawn; and a blade of grass, two vertices
    // at each of three heights then its tip, or far away one triangle from its foot to its tip.
    constexpr std::uint32_t cells = terrainBlockCells;
    constexpr std::uint32_t half = cells / 2;
    std::vector<std::uint32_t> grid;
    for (std::uint32_t quarter = 0; quarter < 4; ++quarter)
    {
        const std::uint32_t firstX = (quarter & 1U) * half;
        const std::uint32_t firstZ = (quarter >> 1U) * half;
        for (std::uint32_t z = firstZ; z < firstZ + half; ++z)
        {
            for (std::uint32_t x = firstX; x < firstX + half; ++x)
            {
                const std::uint32_t corner = z * (cells + 1) + x;
                const std::uint32_t right = corner + 1;
                const std::uint32_t below = corner + cells + 1;
                grid.insert(grid.end(), {corner, below, right, right, below, below + 1});
            }
        }
    }
    constexpr std::array<std::uint32_t, 18> blade{0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5, 4, 5, 6, 0, 1, 6};
    // The blocks and the tiles of grass are instances of two attributes: corner and size, then level.
    gl::GenVertexArrays(1, &m_terrainArray);
    gl::BindVertexArray(m_terrainArray);
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    for (const auto& [buffer, indices] : {std::pair{&m_terrainGrid, std::span<const std::uint32_t>(grid)},
                                          std::pair{&m_grassBlade, std::span<const std::uint32_t>(blade)}})
    {
        gl::GenBuffers(1, buffer);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, *buffer);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(), GL_STATIC_DRAW);
    }
    gl::BindVertexArray(0);
    m_particleDrawStride = (sizeof(ParticleDrawData) + static_cast<std::size_t>(m_uniformAlignment) - 1) /
                           static_cast<std::size_t>(m_uniformAlignment) * static_cast<std::size_t>(m_uniformAlignment);
    // A particle is an instance of four attributes, a segment of a ribbon one of six: its two points.
    gl::GenVertexArrays(1, &m_particleArray);
    gl::BindVertexArray(m_particleArray);
    for (GLuint location = 0; location < 4; ++location)
    {
        gl::EnableVertexAttribArray(location);
        gl::VertexAttribDivisor(location, 1);
    }
    gl::GenVertexArrays(1, &m_ribbonArray);
    gl::BindVertexArray(m_ribbonArray);
    for (GLuint location = 0; location < 6; ++location)
    {
        gl::EnableVertexAttribArray(location);
        gl::VertexAttribDivisor(location, 1);
    }
    gl::BindVertexArray(0);

    m_pointSampler = makeSampler(GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    m_skySampler = makeSampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_CLAMP_TO_EDGE);
    m_cubeSampler = makeSampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    m_shadowSampler = makeSampler(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    gl::SamplerParameteri(m_shadowSampler, GL_TEXTURE_COMPARE_MODE, static_cast<GLint>(GL_COMPARE_REF_TO_TEXTURE));
    gl::SamplerParameteri(m_shadowSampler, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(GL_LEQUAL));
    m_clampMipSampler = makeSampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    if (!m_es)
    {
        // OpenGL ES reads cubes across their seams by itself.
        gl::Enable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        m_depthClamp = true;
    }

    // The shadow maps: smaller on a phone, as WebGPU's. A depth of floats cannot be filtered by OpenGL ES, which compares
    // depths of 24 bits between texels.
    if (platform::isMobile())
    {
        m_shadowMapSize = 1024;
        m_shadowAtlasSize = 2048;
    }
    m_shadowDepthFormat = m_es ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT32F;
    const GLenum depthType = m_es ? GL_UNSIGNED_INT : GL_FLOAT;
    gl::GenTextures(1, &m_cascadeMap);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, m_cascadeMap);
    gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    gl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, static_cast<GLint>(m_shadowDepthFormat), static_cast<GLsizei>(m_shadowMapSize),
                   static_cast<GLsizei>(m_shadowMapSize), 4, 0, GL_DEPTH_COMPONENT, depthType, nullptr);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    for (GLint layer = 0; layer < 4; ++layer)
    {
        m_cascadeFramebuffers[static_cast<std::size_t>(layer)] = depthFramebuffer(m_cascadeMap, layer);
    }
    gl::GenTextures(1, &m_atlasMap);
    gl::BindTexture(GL_TEXTURE_2D, m_atlasMap);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(m_shadowDepthFormat), static_cast<GLsizei>(m_shadowAtlasSize),
                   static_cast<GLsizei>(m_shadowAtlasSize), 0, GL_DEPTH_COMPONENT, depthType, nullptr);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    m_atlasFramebuffer = depthFramebuffer(m_atlasMap, std::nullopt);
    if (const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE)
    {
        return core::makeError(core::ErrorCode::Graphics, "OpenGL cannot draw the depth of shadows: {:#x}", status);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);

    gl::GenTextures(1, &m_boneTexture);
    gl::GenBuffers(1, &m_luminanceBuffer);
    gl::BindBuffer(GL_PIXEL_PACK_BUFFER, m_luminanceBuffer);
    gl::BufferData(GL_PIXEL_PACK_BUFFER, std::size_t{luminanceGridWidth} * luminanceGridHeight * 4 * sizeof(float), nullptr,
                   GL_STREAM_READ);
    gl::BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_defaultMaterial = createMaterial(MaterialDesc{});
    bakeBrdfTable();
    bakeEnvironment({});
    return {};
}

void GlRenderer::destroySceneResources() noexcept
{
    for (const Program* program : {&m_meshProgram, &m_skinnedProgram, &m_shadowProgram, &m_skinnedShadowProgram, &m_cubeProgram,
                                   &m_prefilterProgram, &m_irradianceProgram, &m_brdfProgram, &m_luminanceProgram,
                                   &m_particleProgram, &m_ribbonProgram})
    {
        gl::DeleteProgram(program->program);
    }
    const GLuint buffers[] = {m_sceneBuffer,    m_lightBuffer,    m_shadowViewBuffer,  m_bakeBuffer,         m_shadowMatrixBuffer,
                              m_instanceBuffer, m_luminanceBuffer, m_particleBuffer, m_ribbonBuffer, m_particleDrawBuffer};
    gl::DeleteBuffers(10, buffers);
    const GLuint arrays[] = {m_particleArray, m_ribbonArray, m_terrainArray};
    gl::DeleteVertexArrays(3, arrays);
    const GLuint terrainBuffers[] = {m_terrainGrid,   m_grassBlade,        m_terrainNodeBuffer, m_grassTileBuffer,
                                     m_terrainBuffer, m_terrainDrawBuffer, m_blitBuffer,        m_decalBuffer};
    gl::DeleteBuffers(8, terrainBuffers);
    const GLuint atlases[] = {m_decalColors.texture, m_decalSurfaces.texture};
    gl::DeleteTextures(2, atlases);
    for (const TerrainLayers& layers : m_terrainLayers)
    {
        const GLuint textures[] = {layers.colors, layers.surfaces, layers.normals};
        gl::DeleteTextures(3, textures);
    }
    for (const Program* program : {&m_groundProgram, &m_groundShadowProgram, &m_grassProgram, &m_blitProgram})
    {
        gl::DeleteProgram(program->program);
    }
    gl::DeleteFramebuffers(1, &m_depthCopyFramebuffer);
    gl::DeleteTextures(1, &m_depthCopy);
    const GLuint samplers[] = {m_pointSampler, m_skySampler, m_cubeSampler, m_shadowSampler, m_clampMipSampler};
    gl::DeleteSamplers(5, samplers);
    const GLuint textures[] = {m_flatNormal, m_brdfTable, m_boneTexture, m_cascadeMap, m_atlasMap, m_sceneDepth,
                               m_environment.irradiance, m_environment.specular};
    gl::DeleteTextures(8, textures);
    gl::DeleteFramebuffers(4, m_cascadeFramebuffers.data());
    gl::DeleteFramebuffers(1, &m_atlasFramebuffer);
    destroyTarget(m_luminanceTarget);
    if (m_luminanceFence != nullptr)
    {
        gl::DeleteSync(m_luminanceFence);
    }
}

GLuint GlRenderer::whiteCube() const
{
    // A sky of one colour lights as white maps tinted by that colour, as Vulkan bakes a white texture.
    GLuint cube = 0;
    gl::GenTextures(1, &cube);
    gl::BindTexture(GL_TEXTURE_CUBE_MAP, cube);
    gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (GLenum face = 0; face < 6; ++face)
    {
        gl::TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, static_cast<GLint>(GL_RGBA16F), 1, 1, 0, GL_RGBA, GL_HALF_FLOAT,
                       halfWhite.data());
    }
    gl::BindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return cube;
}

void GlRenderer::bakeFaces(const Program& program, GLuint cube, std::uint32_t size, std::uint32_t mips, const BakeSettings& settings)
{
    GLuint framebuffer = 0;
    gl::GenFramebuffers(1, &framebuffer);
    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl::UseProgram(program.program);
    gl::BindVertexArray(m_emptyArray);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::bake, m_bakeBuffer, 0, sizeof(BakeData));
    for (std::uint32_t mip = 0; mip < mips; ++mip)
    {
        const std::uint32_t levelSize = std::max(size >> mip, 1U);
        gl::Viewport(0, 0, static_cast<GLsizei>(levelSize), static_cast<GLsizei>(levelSize));
        BakeData data{.size = levelSize,
                      .roughness = mips > 1 && &program == &m_prefilterProgram
                                       ? static_cast<float>(mip) / static_cast<float>(mips - 1)
                                       : settings.roughness,
                      .sampleCount = settings.sampleCount,
                      .sourceLod = settings.sourceLod,
                      .sourceSize = settings.sourceSize};
        // Every level of the radiance cube samples the map at a matching detail.
        if (settings.sourceWidth > 0)
        {
            data.sourceLod =
                std::max(std::log2(static_cast<float>(settings.sourceWidth) / (4.0f * static_cast<float>(levelSize))), 0.0f);
        }
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cube,
                                     static_cast<GLint>(mip));
            data.face = face;
            gl::BindBuffer(GL_UNIFORM_BUFFER, m_bakeBuffer);
            gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
            gl::DrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &framebuffer);
}

void GlRenderer::bakeBrdfTable()
{
    // Its values lie between 0 and 1: 8 bits keep them where half floats cannot be drawn into.
    gl::GenTextures(1, &m_brdfTable);
    gl::BindTexture(GL_TEXTURE_2D, m_brdfTable);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(m_halfFloatTargets ? GL_RGBA16F : GL_RGBA8), brdfTableSize, brdfTableSize, 0,
                   GL_RGBA, m_halfFloatTargets ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    GLuint framebuffer = 0;
    gl::GenFramebuffers(1, &framebuffer);
    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_brdfTable, 0);
    gl::Viewport(0, 0, brdfTableSize, brdfTableSize);
    const BakeData data{.size = brdfTableSize, .sampleCount = brdfSamples};
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_bakeBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::bake, m_bakeBuffer, 0, sizeof(BakeData));
    gl::UseProgram(m_brdfProgram.program);
    gl::BindVertexArray(m_emptyArray);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &framebuffer);
}

void GlRenderer::bakeEnvironment(TextureHandle sky)
{
    const GLuint previous[] = {m_environment.irradiance, m_environment.specular};
    gl::DeleteTextures(2, previous);
    m_environment = {.sky = sky, .baked = true};
    const GpuTexture* const source = m_textures.find(sky);
    // A uniform sky, or a system that cannot draw half floats, lights with white maps tinted by the colour of the sky.
    if (source == nullptr || !source->bindable || !m_halfFloatTargets)
    {
        m_environment.irradiance = whiteCube();
        m_environment.specular = whiteCube();
        return;
    }
    const auto cube = [](std::uint32_t size, std::uint32_t mips) {
        GLuint made = 0;
        gl::GenTextures(1, &made);
        gl::BindTexture(GL_TEXTURE_CUBE_MAP, made);
        gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(mips - 1));
        for (std::uint32_t mip = 0; mip < mips; ++mip)
        {
            const auto levelSize = static_cast<GLsizei>(std::max(size >> mip, 1U));
            for (GLenum face = 0; face < 6; ++face)
            {
                gl::TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, static_cast<GLint>(mip), static_cast<GLint>(GL_RGBA16F), levelSize,
                               levelSize, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
            }
        }
        gl::BindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return made;
    };
    const auto radianceMips = static_cast<std::uint32_t>(std::bit_width(radianceSize));
    const GLuint radiance = cube(radianceSize, radianceMips);
    m_environment.specular = cube(specularSize, specularMipCount);
    m_environment.irradiance = cube(irradianceSize, 1);
    m_environment.specularMips = specularMipCount;

    // The map wraps around, and the cube is read between its texels, as the samplers of vulkan::EnvironmentBaker.
    bindTextureTarget(unit::first, GL_TEXTURE_2D, source->texture, m_skySampler);
    bakeFaces(m_cubeProgram, radiance, radianceSize, radianceMips, {.sourceWidth = source->size.width});
    bindTextureTarget(unit::second, GL_TEXTURE_CUBE_MAP, radiance, m_cubeSampler);
    bakeFaces(m_prefilterProgram, m_environment.specular, specularSize, specularMipCount,
              {.sampleCount = prefilterSamples, .sourceSize = static_cast<float>(radianceSize)});
    bakeFaces(m_irradianceProgram, m_environment.irradiance, irradianceSize, 1,
              {.sampleCount = irradianceSamples, .sourceSize = static_cast<float>(radianceSize)});
    bindTextureTarget(unit::second, GL_TEXTURE_CUBE_MAP, 0, 0);
    gl::DeleteTextures(1, &radiance);
}

float GlRenderer::sortDistance(math::Vec3 point, math::Vec3 cameraPosition) const noexcept
{
    if (m_world.camera.projection == Projection::Orthographic)
    {
        return -(m_world.camera.view * math::Vec4(point, 1.0f)).z;
    }
    const math::Vec3 toCamera = point - cameraPosition;
    return math::dot(toCamera, toCamera);
}

void GlRenderer::prepareMeshes(const ViewMatrices& matrices, const Frustum& frustum, math::Extent2D sceneSize)
{
    // The environment follows the sky texture of the world.
    if (!m_environment.baked || m_world.environment.sky != m_environment.sky ||
        (m_environment.sky.isValid() && !m_textures.contains(m_environment.sky)))
    {
        bakeEnvironment(m_world.environment.sky);
    }

    const RenderSun& sun = m_world.sun;
    const float sunLength = math::length(sun.direction);
    std::vector<GpuLight> all;
    packLights(m_world, all);

    // The cascades of the sun as Vulkan fits them, and the views of the atlas the lights that cast shadows are given.
    const RenderCamera& camera = m_world.camera;
    const float aspectRatio = static_cast<float>(sceneSize.width) / static_cast<float>(std::max(sceneSize.height, 1U));
    const bool sunEnabled = std::max({sun.illuminance.r, sun.illuminance.g, sun.illuminance.b}) > 0.0f;
    std::optional<ShadowCascades> cascades;
    if (sunEnabled && sun.castShadows && sun.shadowDistance > camera.nearPlane)
    {
        cascades = computeShadowCascades(math::inverse(camera.view), ViewVolume::of(camera, aspectRatio), camera.nearPlane,
                                         sun.shadowDistance, sun.direction, m_shadowMapSize);
    }
    const ShadowAssignment assigned =
        assignShadowViews(m_world.lights, frustum, matrices.cameraPosition,
                          ShadowAtlasLayout{.size = m_shadowAtlasSize, .cell = shadowAtlasCell, .largeLights = largeShadowLights}, flipY());
    for (std::size_t index = 0; index < all.size() && index < assigned.firstView.size(); ++index)
    {
        all[index].firstShadowView = assigned.firstView[index];
        if (assigned.firstView[index] >= 0)
        {
            all[index].shadowFar = assigned.far[index];
        }
    }

    // At most 32 lights a frame, as Godot's Compatibility: those the camera sees, the nearest first.
    std::vector<std::pair<float, std::uint32_t>> seen;
    for (std::uint32_t index = 0; index < all.size(); ++index)
    {
        const GpuLight& light = all[index];
        const bool everywhere = light.range <= 0.0f;
        if (everywhere || frustum.intersects(math::Aabb{.min = light.position - light.range, .max = light.position + light.range}))
        {
            const math::Vec3 toCamera = light.position - matrices.cameraPosition;
            seen.emplace_back(everywhere ? 0.0f : math::dot(toCamera, toCamera), index);
        }
    }
    std::ranges::stable_sort(seen, {}, &std::pair<float, std::uint32_t>::first);
    m_lights.clear();
    for (std::size_t index = 0; index < seen.size() && index < maxLights; ++index)
    {
        m_lights.push_back(all[seen[index].second]);
    }

    prepareDecals(frustum, matrices.cameraPosition);

    // The instances the camera sees, by batches of one mesh, submesh, material and way to be drawn; blended ones alone,
    // the farthest first. A skinned one, always drawn, has no box for its lights, but its rest for its decals.
    struct Visible
    {
        const MeshInstance* instance;
        const GpuMesh* mesh;
        MaterialHandle material;
        std::uint32_t flags;
        float distance;
        math::Aabb box;
        math::Aabb rest;
    };
    std::vector<Visible> visible;
    for (const MeshInstance& instance : m_world.meshes)
    {
        const GpuMesh* const mesh = m_meshes.find(instance.mesh);
        if (mesh == nullptr || instance.submesh >= mesh->submeshes.size())
        {
            continue;
        }
        const bool skinned = instance.boneCount > 0 && mesh->skin != 0;
        const math::Aabb box = mesh->bounds.isEmpty() ? math::Aabb{} : math::transform(instance.transform, mesh->bounds);
        // A skinned pose reaches past the box of its rest: it is always drawn.
        if (!skinned && !box.isEmpty() && !frustum.intersects(box))
        {
            continue;
        }
        const MaterialHandle material = m_materials.contains(instance.material) ? instance.material : m_defaultMaterial;
        const MaterialDesc& description = m_materials.find(material)->description;
        std::uint32_t flags = skinned ? skinnedFlag : 0;
        flags |= description.doubleSided ? bothSidesFlag : 0;
        flags |= description.alphaMode == asset::AlphaMode::Blend ? blendedFlag : 0;
        visible.push_back({&instance, mesh, material, flags, sortDistance(math::Vec3(instance.transform[3]), matrices.cameraPosition),
                           skinned ? math::Aabb{} : box, box});
    }
    std::ranges::stable_sort(visible, [](const Visible& left, const Visible& right) {
        const bool leftBlended = (left.flags & blendedFlag) != 0;
        const bool rightBlended = (right.flags & blendedFlag) != 0;
        if (leftBlended != rightBlended)
        {
            return !leftBlended;
        }
        if (leftBlended)
        {
            return left.distance > right.distance;
        }
        return std::tuple(left.flags, left.material.index, left.mesh, left.instance->submesh) <
               std::tuple(right.flags, right.material.index, right.mesh, right.instance->submesh);
    });
    m_meshBatches.clear();
    std::vector<InstanceData> instances;
    instances.reserve(visible.size());
    for (const Visible& entry : visible)
    {
        const bool blended = (entry.flags & blendedFlag) != 0;
        if (blended || m_meshBatches.empty() || m_meshBatches.back().mesh != entry.mesh ||
            m_meshBatches.back().submesh != entry.instance->submesh || m_meshBatches.back().material != entry.material ||
            m_meshBatches.back().flags != entry.flags)
        {
            m_meshBatches.push_back({.mesh = entry.mesh,
                                     .submesh = entry.instance->submesh,
                                     .material = entry.material,
                                     .flags = entry.flags,
                                     .firstInstance = static_cast<std::uint32_t>(instances.size()),
                                     .distance = entry.distance});
        }
        ++m_meshBatches.back().instanceCount;
        const math::Vec3 centre(entry.instance->transform[3]);
        instances.push_back({.world = entry.instance->transform,
                             .lights = lightsReaching(entry.box, centre),
                             .firstBone = entry.instance->firstBone,
                             .boneCount = entry.instance->boneCount,
                             .decals = decalsReaching(entry.rest, centre)});
    }

    // The casters of each view of the shadows, by batches as the camera's, after them: opaque and cut-out surfaces cast
    // shadows, blended ones do not.
    struct ShadowViewOf
    {
        math::Mat4 drawn;
        Frustum frustum;
        bool atlas;
        std::uint32_t layer;
        std::array<float, 4> viewport;
    };
    std::vector<ShadowViewOf> views;
    if (cascades)
    {
        for (std::uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            const math::Mat4& read = cascades->viewProjections[cascade];
            views.push_back({shadowCorrection() * read, frustumOf(read), false, cascade, {}});
        }
    }
    for (const ShadowView& view : assigned.views)
    {
        const auto size = static_cast<float>(m_shadowAtlasSize);
        views.push_back({shadowCorrection() * view.viewProjection, frustumOf(view.viewProjection), true, 0,
                         {view.tile.x * size, view.tile.y * size, view.tile.z * size, view.tile.w * size}});
    }
    m_shadowBatches.clear();
    m_shadowPasses.clear();
    struct Caster
    {
        const MeshInstance* instance;
        const GpuMesh* mesh;
        MaterialHandle material;
        std::uint32_t flags;
    };
    std::vector<Caster> casters;
    for (const ShadowViewOf& view : views)
    {
        casters.clear();
        for (const MeshInstance& instance : m_world.meshes)
        {
            const GpuMesh* const mesh = m_meshes.find(instance.mesh);
            if (mesh == nullptr || instance.submesh >= mesh->submeshes.size())
            {
                continue;
            }
            const MaterialHandle material = m_materials.contains(instance.material) ? instance.material : m_defaultMaterial;
            if (m_materials.find(material)->description.alphaMode == asset::AlphaMode::Blend)
            {
                continue;
            }
            const bool skinned = instance.boneCount > 0 && mesh->skin != 0;
            if (!skinned && !mesh->bounds.isEmpty() && !view.frustum.intersects(math::transform(instance.transform, mesh->bounds)))
            {
                continue;
            }
            casters.push_back({&instance, mesh, material, skinned ? skinnedFlag : 0U});
        }
        std::ranges::sort(casters, [](const Caster& left, const Caster& right) {
            return std::tuple(left.flags, left.material.index, left.mesh, left.instance->submesh) <
                   std::tuple(right.flags, right.material.index, right.mesh, right.instance->submesh);
        });
        ShadowPass pass{.firstBatch = static_cast<std::uint32_t>(m_shadowBatches.size()),
                        .atlas = view.atlas,
                        .layer = view.layer,
                        .viewport = view.viewport};
        for (const Caster& caster : casters)
        {
            if (m_shadowBatches.size() == pass.firstBatch || m_shadowBatches.back().mesh != caster.mesh ||
                m_shadowBatches.back().submesh != caster.instance->submesh || m_shadowBatches.back().material != caster.material ||
                m_shadowBatches.back().flags != caster.flags)
            {
                m_shadowBatches.push_back({.mesh = caster.mesh,
                                           .submesh = caster.instance->submesh,
                                           .material = caster.material,
                                           .flags = caster.flags,
                                           .firstInstance = static_cast<std::uint32_t>(instances.size())});
            }
            ++m_shadowBatches.back().instanceCount;
            instances.push_back({.world = caster.instance->transform,
                                 .firstBone = caster.instance->firstBone,
                                 .boneCount = caster.instance->boneCount});
        }
        pass.batchCount = static_cast<std::uint32_t>(m_shadowBatches.size()) - pass.firstBatch;
        m_shadowPasses.push_back(pass);
    }

    // The instances, the matrices of the views of the shadows, the lights, the views of the atlas and the bones.
    if (!instances.empty())
    {
        growBuffer(m_instanceBuffer, GL_ARRAY_BUFFER, m_instanceCapacity, instances.size() * sizeof(InstanceData));
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instances.size() * sizeof(InstanceData)), instances.data());
    }
    const std::size_t matrixStride = (sizeof(math::Mat4) + static_cast<std::size_t>(m_uniformAlignment) - 1) /
                                     static_cast<std::size_t>(m_uniformAlignment) * static_cast<std::size_t>(m_uniformAlignment);
    if (!views.empty())
    {
        std::vector<std::byte> shadowMatrices(views.size() * matrixStride);
        for (std::size_t view = 0; view < views.size(); ++view)
        {
            std::memcpy(shadowMatrices.data() + view * matrixStride, &views[view].drawn, sizeof(math::Mat4));
        }
        growBuffer(m_shadowMatrixBuffer, GL_UNIFORM_BUFFER, m_shadowMatrixCapacity, shadowMatrices.size());
        gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(shadowMatrices.size()), shadowMatrices.data());
    }
    LightData lights;
    std::ranges::copy(m_lights, lights.lights.begin());
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_lightBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(lights), &lights);
    ShadowViewData shadowViews;
    std::ranges::copy(std::span(assigned.views).first(std::min(assigned.views.size(), maxShadowViews)), shadowViews.views.begin());
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_shadowViewBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(shadowViews), &shadowViews);
    if (!m_world.boneMatrices.empty())
    {
        // Four texels a bone, its columns, which the shaders fetch without filtering.
        const auto count = static_cast<std::uint32_t>(m_world.boneMatrices.size());
        const std::uint32_t rows = (count + bonesPerRow - 1) / bonesPerRow;
        std::vector<math::Mat4> padded(std::size_t{rows} * bonesPerRow, math::Mat4{1.0f});
        std::ranges::copy(m_world.boneMatrices, padded.begin());
        gl::BindTexture(GL_TEXTURE_2D, m_boneTexture);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (rows != m_boneRows)
        {
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA32F), static_cast<GLsizei>(bonesPerRow * 4),
                           static_cast<GLsizei>(rows), 0, GL_RGBA, GL_FLOAT, padded.data());
            m_boneRows = rows;
        }
        else
        {
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(bonesPerRow * 4), static_cast<GLsizei>(rows), GL_RGBA, GL_FLOAT,
                              padded.data());
        }
        gl::BindTexture(GL_TEXTURE_2D, 0);
    }

    // The scene the shaders read.
    SceneData scene{
        .viewProjection = matrices.viewProjection,
        .view = m_world.camera.view,
        .cameraPosition = matrices.cameraPosition,
        .exposure = m_exposure,
        .sunDirection = sunLength > 0.0f ? sun.direction / sunLength : math::Vec3{0.0f, -1.0f, 0.0f},
        .sunEnabled = sunEnabled ? 1U : 0U,
        .sunIlluminance = sun.illuminance,
        .lightCount = static_cast<std::uint32_t>(m_lights.size()),
        .sky = m_world.environment.color * m_world.environment.intensity,
        .environmentRotation = m_world.environment.rotation,
        .specularMipCount = static_cast<float>(m_environment.specularMips),
        .viewportWidth = static_cast<float>(sceneSize.width),
        .viewportHeight = static_cast<float>(sceneSize.height),
        .time = m_shaderTime,
    };
    scene.skyInverseViewProjection = math::inverse(matrices.skyViewProjection);
    scene.inverseViewProjection = matrices.inverseViewProjection;
    const GpuAtmosphere air = packAtmosphere(m_world, matrices.cameraPosition.y).gpu;
    scene.fogColor = air.fogColor;
    scene.fogDensity = air.fogDensity;
    scene.fogHeight = air.fogHeight;
    scene.fogFalloff = air.fogFalloff;
    scene.fogStart = air.fogStart;
    scene.fogMaxOpacity = air.fogMaxOpacity;
    scene.fogSunScattering = air.fogSunScattering;
    scene.fogSunAnisotropy = air.fogSunAnisotropy;
    scene.fogSkyBlend = air.fogSkyBlend;
    if (cascades)
    {
        scene.sunCastsShadows = 1;
        scene.shadowDistance = sun.shadowDistance;
        for (std::size_t cascade = 0; cascade < 4; ++cascade)
        {
            scene.cascadeViewProjections[cascade] = cascades->viewProjections[cascade];
            scene.cascadeSplits[static_cast<int>(cascade)] = cascades->splitDistances[cascade];
            scene.cascadeTexelSizes[static_cast<int>(cascade)] = cascades->texelSizes[cascade];
        }
    }
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_sceneBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(scene), &scene);
}

std::array<std::uint32_t, 2> GlRenderer::lightsReaching(const math::Aabb& box, math::Vec3 centre) const
{
    // At most 8 an object, as Godot's Compatibility: those that reach its box, the nearest first.
    std::vector<std::pair<float, std::uint32_t>> reaching;
    for (std::uint32_t index = 0; index < m_lights.size(); ++index)
    {
        const GpuLight& light = m_lights[index];
        const math::Vec3 nearest = box.isEmpty() ? centre : math::max(box.min, math::min(light.position, box.max));
        const math::Vec3 toLight = light.position - nearest;
        const float distance = math::dot(toLight, toLight);
        if (light.range <= 0.0f || distance <= light.range * light.range)
        {
            reaching.emplace_back(distance, index);
        }
    }
    std::ranges::stable_sort(reaching, {}, &std::pair<float, std::uint32_t>::first);
    std::array<std::uint32_t, 2> packed{0xFFFFFFFFU, 0xFFFFFFFFU};
    for (std::size_t slot = 0; slot < reaching.size() && slot < lightsPerObject; ++slot)
    {
        const std::uint32_t shift = static_cast<std::uint32_t>(slot % 4) * 8;
        std::uint32_t& word = packed[slot / 4];
        word = (word & ~(0xFFU << shift)) | (reaching[slot].second << shift);
    }
    return packed;
}

void GlRenderer::fillDecalAtlas(DecalAtlas& atlas, std::vector<TextureHandle> textures, bool colors)
{
    const auto keyOf = [](TextureHandle handle) { return (std::uint64_t{handle.index} << 32) | handle.generation; };
    std::ranges::sort(textures, {}, keyOf);
    textures.erase(std::ranges::unique(textures, {}, keyOf).begin(), textures.end());
    std::vector<std::uint64_t> key{m_textureRevision};
    for (const TextureHandle texture : textures)
    {
        key.push_back(keyOf(texture));
    }
    if (key == atlas.key)
    {
        return;
    }
    atlas.key = std::move(key);
    atlas.rects.clear();
    if (textures.empty())
    {
        return;
    }
    if (atlas.texture == 0)
    {
        gl::GenTextures(1, &atlas.texture);
        gl::BindTexture(GL_TEXTURE_2D, atlas.texture);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(colors ? GL_SRGB8_ALPHA8 : GL_RGBA8), static_cast<GLsizei>(decalAtlasSize),
                       static_cast<GLsizei>(decalAtlasSize), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl::BindTexture(GL_TEXTURE_2D, 0);
    }
    // On shelves, the tallest first, each texture at its size but no larger than half the atlas, all halved together
    // until they fit; a texel of room around each keeps its neighbours from bleeding in. As WebGPU places them.
    std::vector<math::Extent2D> sizes;
    for (const TextureHandle texture : textures)
    {
        const GpuTexture* const found = m_textures.find(texture);
        sizes.push_back(found != nullptr ? found->size : math::Extent2D{1, 1});
    }
    std::vector<std::size_t> order(textures.size());
    for (std::size_t index = 0; index < order.size(); ++index)
    {
        order[index] = index;
    }
    std::ranges::sort(order, std::greater{}, [&](std::size_t index) { return sizes[index].height; });
    // The corner and the size of each in pixels.
    std::vector<std::array<std::uint32_t, 4>> placed(textures.size());
    for (float scale = 1.0f; scale >= 1.0f / 64.0f; scale *= 0.5f)
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t shelf = 0;
        bool fits = true;
        for (const std::size_t index : order)
        {
            const math::Extent2D size = sizes[index];
            const float largest = static_cast<float>(std::max(size.width, size.height));
            const float shrink = std::min(1.0f, static_cast<float>(decalAtlasSize / 2) / largest) * scale;
            const auto width = std::max(static_cast<std::uint32_t>(std::lround(static_cast<float>(size.width) * shrink)), 1U);
            const auto height = std::max(static_cast<std::uint32_t>(std::lround(static_cast<float>(size.height) * shrink)), 1U);
            if (x + width + 2 > decalAtlasSize)
            {
                x = 0;
                y += shelf;
                shelf = 0;
            }
            if (y + height + 2 > decalAtlasSize)
            {
                fits = false;
                break;
            }
            placed[index] = {x + 1, y + 1, width, height};
            x += width + 2;
            shelf = std::max(shelf, height + 2);
        }
        if (fits)
        {
            break;
        }
    }
    // Each texture drawn into its rectangle from the mip that fits it; the colours encoded back into sRGB.
    GLuint framebuffer = 0;
    gl::GenFramebuffers(1, &framebuffer);
    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, atlas.texture, 0);
    if (const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE)
    {
        DEVEX_LOG_WARNING("OpenGL cannot draw the textures of the decals into their atlas: {:#x}", status);
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl::DeleteFramebuffers(1, &framebuffer);
        return;
    }
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    if (!m_es)
    {
        colors ? gl::Enable(GL_FRAMEBUFFER_SRGB) : gl::Disable(GL_FRAMEBUFFER_SRGB);
    }
    gl::Viewport(0, 0, static_cast<GLsizei>(decalAtlasSize), static_cast<GLsizei>(decalAtlasSize));
    gl::ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    gl::UseProgram(m_blitProgram.program);
    gl::BindVertexArray(m_emptyArray);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::blit, m_blitBuffer, 0, sizeof(BlitData));
    const auto atlasSize = static_cast<float>(decalAtlasSize);
    for (std::size_t index = 0; index < textures.size(); ++index)
    {
        const GpuTexture* const found = m_textures.find(textures[index]);
        const auto [x, y, width, height] = placed[index];
        if (found == nullptr || !found->bindable || width == 0)
        {
            continue;
        }
        const BlitData blit{.targetWidth = static_cast<float>(width),
                            .targetHeight = static_cast<float>(height),
                            .sourceLevels = static_cast<float>(found->mips),
                            .originX = static_cast<float>(x),
                            .originY = static_cast<float>(y)};
        gl::BindBuffer(GL_UNIFORM_BUFFER, m_blitBuffer);
        gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(blit), &blit);
        bindTexture(unit::first, found->texture, m_clampMipSampler);
        gl::Viewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        gl::DrawArrays(GL_TRIANGLES, 0, 3);
        // OpenGL counts the rows it draws into from the first stored, as the shaders read the atlas.
        atlas.rects.emplace_back(textures[index], math::Vec4(static_cast<float>(x) / atlasSize, static_cast<float>(y) / atlasSize,
                                                             static_cast<float>(width) / atlasSize, static_cast<float>(height) / atlasSize));
    }
    if (!m_es)
    {
        gl::Disable(GL_FRAMEBUFFER_SRGB);
    }
    gl::BindVertexArray(0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &framebuffer);
}

void GlRenderer::prepareDecals(const Frustum& frustum, math::Vec3 cameraPosition)
{
    // At most 64 a frame: those the camera sees, the nearest first; then in the order of the world, which they lie in.
    struct Seen
    {
        float distance;
        std::size_t index;
        DecalBox box;
        math::Aabb bounds;
    };
    std::vector<Seen> seen;
    for (std::size_t index = 0; index < m_world.decals.size(); ++index)
    {
        const RenderDecal& decal = m_world.decals[index];
        const std::optional<DecalBox> box = decalBox(decal);
        if (!box)
        {
            continue;
        }
        const math::Aabb bounds = math::transform(math::inverse(box->worldToDecal), math::Aabb{.min = math::Vec3{-1.0f}, .max = math::Vec3{1.0f}});
        if (!frustum.intersects(bounds))
        {
            continue;
        }
        const math::Vec3 toCamera = math::Vec3(decal.transform[3]) - cameraPosition;
        seen.push_back({math::dot(toCamera, toCamera), index, *box, bounds});
    }
    std::ranges::stable_sort(seen, {}, &Seen::distance);
    seen.resize(std::min(seen.size(), maxDecals));
    std::ranges::sort(seen, {}, &Seen::index);

    const auto usable = [this](TextureHandle texture) {
        const GpuTexture* const found = m_textures.find(texture);
        return found != nullptr && found->bindable;
    };
    std::vector<TextureHandle> colors;
    std::vector<TextureHandle> surfaces;
    for (const Seen& entry : seen)
    {
        const RenderDecal& decal = m_world.decals[entry.index];
        for (const TextureHandle texture : {decal.albedo, decal.emission})
        {
            if (usable(texture))
            {
                colors.push_back(texture);
            }
        }
        for (const TextureHandle texture : {decal.normal, decal.orm})
        {
            if (usable(texture))
            {
                surfaces.push_back(texture);
            }
        }
    }
    fillDecalAtlas(m_decalColors, std::move(colors), true);
    fillDecalAtlas(m_decalSurfaces, std::move(surfaces), false);
    // Where a texture lies in its atlas; nothing for one it does not hold.
    const auto rectOf = [&usable](const DecalAtlas& atlas, TextureHandle texture) {
        if (!usable(texture))
        {
            return math::Vec4{0.0f};
        }
        const auto found = std::ranges::find_if(atlas.rects, [&](const auto& placed) { return placed.first == texture; });
        return found != atlas.rects.end() ? found->second : math::Vec4{0.0f};
    };
    std::vector<AtlasDecalData> packed;
    m_decalBoxes.clear();
    for (const Seen& entry : seen)
    {
        const RenderDecal& decal = m_world.decals[entry.index];
        packed.push_back({.worldToDecal = entry.box.worldToDecal,
                          .axisX = entry.box.axisX,
                          .axisZ = entry.box.axisZ,
                          .axisY = entry.box.axisY,
                          .modulate = decal.modulate,
                          .albedoMix = decal.albedoMix,
                          .emissionEnergy = decal.emissionEnergy,
                          .normalFade = decal.normalFade,
                          .upperFade = decal.upperFade,
                          .lowerFade = decal.lowerFade,
                          .fadeBegin = decal.fadeBegin,
                          .fadeLength = decal.fadeLength,
                          .rects = {rectOf(m_decalColors, decal.albedo), rectOf(m_decalSurfaces, decal.normal),
                                    rectOf(m_decalSurfaces, decal.orm), rectOf(m_decalColors, decal.emission)}});
        m_decalBoxes.push_back(entry.bounds);
    }
    if (!packed.empty())
    {
        gl::BindBuffer(GL_UNIFORM_BUFFER, m_decalBuffer);
        gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(packed.size() * sizeof(AtlasDecalData)), packed.data());
    }
}

std::array<std::uint32_t, 2> GlRenderer::decalsReaching(const math::Aabb& box, math::Vec3 centre) const
{
    // At most 8 an object, as Godot's mobile renderer: those whose box meets its own, in the order they lie in.
    std::array<std::uint32_t, 2> packed{0xFFFFFFFFU, 0xFFFFFFFFU};
    std::size_t slot = 0;
    for (std::uint32_t index = 0; index < m_decalBoxes.size() && slot < decalsPerObject; ++index)
    {
        const math::Aabb& decal = m_decalBoxes[index];
        const math::Vec3 low = box.isEmpty() ? centre : box.min;
        const math::Vec3 high = box.isEmpty() ? centre : box.max;
        const bool meets = low.x <= decal.max.x && decal.min.x <= high.x && low.y <= decal.max.y && decal.min.y <= high.y &&
                           low.z <= decal.max.z && decal.min.z <= high.z;
        if (!meets)
        {
            continue;
        }
        const std::uint32_t shift = static_cast<std::uint32_t>(slot % 4) * 8;
        std::uint32_t& word = packed[slot / 4];
        word = (word & ~(0xFFU << shift)) | (index << shift);
        ++slot;
    }
    return packed;
}

void GlRenderer::prepareParticles(const ViewMatrices& matrices)
{
    std::vector<GpuParticle> particles;
    std::vector<GpuTrailPoint> points;
    packParticles(m_world, matrices.cameraPosition, particles, points);
    if (!particles.empty())
    {
        growBuffer(m_particleBuffer, GL_ARRAY_BUFFER, m_particleCapacity, particles.size() * sizeof(GpuParticle));
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(particles.size() * sizeof(GpuParticle)), particles.data());
    }
    // Each segment with the point it starts at and the next one, in the order of RenderWorld::trailSegments.
    std::vector<RibbonSegment> segments;
    segments.reserve(m_world.trailSegments.size());
    for (const std::uint32_t start : m_world.trailSegments)
    {
        segments.push_back(start + 1 < points.size() ? RibbonSegment{points[start], points[start + 1]} : RibbonSegment{});
    }
    if (!segments.empty())
    {
        growBuffer(m_ribbonBuffer, GL_ARRAY_BUFFER, m_ribbonCapacity, segments.size() * sizeof(RibbonSegment));
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(segments.size() * sizeof(RibbonSegment)), segments.data());
    }
    const std::vector<ParticleDraw>& draws = m_world.particleDraws;
    if (draws.empty())
    {
        return;
    }
    std::vector<std::byte> data(draws.size() * m_particleDrawStride);
    for (std::size_t index = 0; index < draws.size(); ++index)
    {
        const ParticleDraw& draw = draws[index];
        const GpuTexture* const texture = m_textures.find(draw.texture);
        const ParticleDrawData drawData{
            .facing = static_cast<std::uint32_t>(draw.facing),
            .flags = (draw.blend == ParticleBlend::Additive ? 1U : 0U) | (draw.lit ? 2U : 0U),
            .textured = texture != nullptr && texture->bindable ? 1U : 0U,
            .sheet = {static_cast<float>(std::max(draw.sheetColumns, 1U)), static_cast<float>(std::max(draw.sheetRows, 1U))},
            .softness = std::max(draw.softness, 0.0f),
            .lights = draw.lit ? lightsReaching(math::Aabb{}, draw.center) : std::array<std::uint32_t, 2>{0xFFFFFFFFU, 0xFFFFFFFFU},
        };
        std::memcpy(data.data() + index * m_particleDrawStride, &drawData, sizeof(drawData));
    }
    growBuffer(m_particleDrawBuffer, GL_UNIFORM_BUFFER, m_particleDrawCapacity, data.size());
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(data.size()), data.data());
}

void GlRenderer::copySceneDepth(math::Extent2D size)
{
    if (std::ranges::none_of(m_world.particleDraws, [](const ParticleDraw& draw) { return draw.count > 0 && draw.softness > 0.0f; }))
    {
        return;
    }
    if (m_depthCopy == 0 || m_depthCopySize != size)
    {
        gl::DeleteFramebuffers(1, &m_depthCopyFramebuffer);
        gl::DeleteTextures(1, &m_depthCopy);
        gl::GenTextures(1, &m_depthCopy);
        gl::BindTexture(GL_TEXTURE_2D, m_depthCopy);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT32F), static_cast<GLsizei>(size.width),
                       static_cast<GLsizei>(size.height), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        gl::BindTexture(GL_TEXTURE_2D, 0);
        gl::GenFramebuffers(1, &m_depthCopyFramebuffer);
        gl::BindFramebuffer(GL_FRAMEBUFFER, m_depthCopyFramebuffer);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthCopy, 0);
        const GLenum none = GL_NONE;
        gl::DrawBuffers(1, &none);
        gl::ReadBuffer(GL_NONE);
        m_depthCopySize = size;
    }
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, m_scene.framebuffer);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_depthCopyFramebuffer);
    const auto width = static_cast<GLint>(size.width);
    const auto height = static_cast<GLint>(size.height);
    gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_FRAMEBUFFER, m_scene.framebuffer);
}

void GlRenderer::drawParticles(std::uint32_t index)
{
    const ParticleDraw& draw = m_world.particleDraws[index];
    if (draw.count == 0)
    {
        return;
    }
    // Premultiplied, behind what the opaque surfaces drew nearer, without hiding the others.
    gl::Enable(GL_BLEND);
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl::Disable(GL_CULL_FACE);
    gl::DepthFunc(GL_GEQUAL);
    gl::UseProgram(draw.ribbons ? m_ribbonProgram.program : m_particleProgram.program);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::particleDraw, m_particleDrawBuffer,
                        static_cast<GLintptr>(std::size_t{index} * m_particleDrawStride), sizeof(ParticleDrawData));
    const GpuTexture* const texture = m_textures.find(draw.texture);
    const bool textured = texture != nullptr && texture->bindable;
    bindTexture(unit::first, textured ? texture->texture : m_white, textured && texture->nearest ? m_nearestSampler : m_linearSampler);
    bindTextureTarget(unit::second, GL_TEXTURE_2D, m_depthCopy != 0 ? m_depthCopy : m_white, m_pointSampler);
    // OpenGL ES 3.0 has no first instance: the attributes start at the first of the draw.
    const auto at = [](std::size_t base, std::size_t offset) { return reinterpret_cast<const void*>(base + offset); };
    if (draw.ribbons)
    {
        gl::BindVertexArray(m_ribbonArray);
        gl::BindBuffer(GL_ARRAY_BUFFER, m_ribbonBuffer);
        const std::size_t base = std::size_t{draw.first} * sizeof(RibbonSegment);
        constexpr GLsizei stride = sizeof(RibbonSegment);
        for (GLuint point = 0; point < 2; ++point)
        {
            const std::size_t first = base + point * sizeof(GpuTrailPoint);
            gl::VertexAttribPointer(point * 3, 4, GL_FLOAT, GL_FALSE, stride, at(first, offsetof(GpuTrailPoint, position)));
            gl::VertexAttribPointer(point * 3 + 1, 4, GL_FLOAT, GL_FALSE, stride, at(first, offsetof(GpuTrailPoint, color)));
            gl::VertexAttribPointer(point * 3 + 2, 4, GL_FLOAT, GL_FALSE, stride, at(first, offsetof(GpuTrailPoint, direction)));
        }
    }
    else
    {
        gl::BindVertexArray(m_particleArray);
        gl::BindBuffer(GL_ARRAY_BUFFER, m_particleBuffer);
        const std::size_t base = std::size_t{draw.first} * sizeof(GpuParticle);
        constexpr GLsizei stride = sizeof(GpuParticle);
        gl::VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, at(base, offsetof(GpuParticle, position)));
        gl::VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, at(base, offsetof(GpuParticle, color)));
        gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, at(base, offsetof(GpuParticle, stretch)));
        gl::VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, at(base, offsetof(GpuParticle, frame)));
    }
    gl::DrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(draw.count));
    ++m_drawCalls;
    gl::BindVertexArray(0);
}

void GlRenderer::fillTerrainLayers(TerrainLayers& layers, const RenderTerrain& terrain)
{
    const std::uint32_t count = std::max(std::min<std::uint32_t>(terrain.layerCount, 8), 1U);
    // The textures of each layer, white or flat where its material has none.
    std::vector<std::uint64_t> key{m_textureRevision, count};
    std::vector<std::array<const GpuTexture*, 3>> sources;
    const auto keyOf = [](TextureHandle handle) { return (std::uint64_t{handle.index} << 32) | handle.generation; };
    for (std::uint32_t layer = 0; layer < count; ++layer)
    {
        const GpuMaterial* const material = layer < terrain.layerCount ? m_materials.find(terrain.layers[layer]) : nullptr;
        const auto textureOf = [&](TextureHandle handle) -> const GpuTexture* {
            const GpuTexture* const texture = material != nullptr ? m_textures.find(handle) : nullptr;
            const bool found = texture != nullptr && texture->bindable;
            key.push_back(found ? keyOf(handle) : 0);
            return found ? texture : nullptr;
        };
        sources.push_back({textureOf(material != nullptr ? material->description.baseColorTexture : TextureHandle{}),
                           textureOf(material != nullptr ? material->description.metallicRoughnessTexture : TextureHandle{}),
                           textureOf(material != nullptr ? material->description.normalTexture : TextureHandle{})});
    }
    if (layers.colors != 0 && key == layers.key)
    {
        return;
    }
    layers.key = std::move(key);
    const GLuint previous[] = {layers.colors, layers.surfaces, layers.normals};
    gl::DeleteTextures(3, previous);
    const std::array<std::pair<GLuint*, GLenum>, 3> arrays{
        {{&layers.colors, GL_SRGB8_ALPHA8}, {&layers.surfaces, GL_RGBA8}, {&layers.normals, GL_RGBA8}}};
    for (const auto& [array, format] : arrays)
    {
        gl::GenTextures(1, array);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, *array);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, terrainLayerMips - 1);
        for (std::uint32_t mip = 0; mip < terrainLayerMips; ++mip)
        {
            const auto side = static_cast<GLsizei>(std::max(terrainLayerSize >> mip, 1U));
            gl::TexImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(mip), static_cast<GLint>(format), side, side, static_cast<GLsizei>(count),
                           0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
    }
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    // Every level of every layer of every array, drawn from its texture at the mip that fits it, read as the layers are,
    // repeating, so that the edges of a tile blend into the next one.
    GLuint framebuffer = 0;
    gl::GenFramebuffers(1, &framebuffer);
    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    gl::UseProgram(m_blitProgram.program);
    gl::BindVertexArray(m_emptyArray);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::blit, m_blitBuffer, 0, sizeof(BlitData));
    // The OpenGL ES of the Android emulator encodes the colours drawn into sRGB only in a framebuffer that has held an sRGB
    // texture in 2D, not into the layer of an array alone: one is attached first, which the other systems do not mind.
    GLuint srgbTexture = 0;
    if (m_es)
    {
        constexpr std::array<std::uint8_t, 4> pixel{};
        gl::GenTextures(1, &srgbTexture);
        gl::BindTexture(GL_TEXTURE_2D, srgbTexture);
        gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_SRGB8_ALPHA8), 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        gl::BindTexture(GL_TEXTURE_2D, 0);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srgbTexture, 0);
    }
    for (std::size_t array = 0; array < arrays.size(); ++array)
    {
        // The colours are encoded back into sRGB as they are drawn: by OpenGL when told to, by OpenGL ES always.
        if (!m_es)
        {
            array == 0 ? gl::Enable(GL_FRAMEBUFFER_SRGB) : gl::Disable(GL_FRAMEBUFFER_SRGB);
        }
        for (std::uint32_t layer = 0; layer < count; ++layer)
        {
            const GpuTexture* const source = sources[layer][array];
            bindTexture(unit::first, source != nullptr ? source->texture : array == 2 ? m_flatNormal : m_white, m_linearSampler);
            for (std::uint32_t mip = 0; mip < terrainLayerMips; ++mip)
            {
                const auto side = static_cast<float>(std::max(terrainLayerSize >> mip, 1U));
                const BlitData blit{.targetWidth = side,
                                    .targetHeight = side,
                                    .sourceLevels = static_cast<float>(source != nullptr ? source->mips : 1U)};
                gl::BindBuffer(GL_UNIFORM_BUFFER, m_blitBuffer);
                gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(blit), &blit);
                gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *arrays[array].first, static_cast<GLint>(mip),
                                            static_cast<GLint>(layer));
                if (const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE)
                {
                    DEVEX_LOG_WARNING("OpenGL cannot draw the layers of a terrain into level {} of their array: {:#x}", mip, status);
                    continue;
                }
                gl::Viewport(0, 0, static_cast<GLsizei>(side), static_cast<GLsizei>(side));
                gl::DrawArrays(GL_TRIANGLES, 0, 3);
            }
        }
    }
    if (!m_es)
    {
        gl::Disable(GL_FRAMEBUFFER_SRGB);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &framebuffer);
    gl::DeleteTextures(1, &srgbTexture);
}

void GlRenderer::prepareTerrains(const ViewMatrices& matrices, const Frustum& frustum)
{
    m_terrainDrawings.clear();
    if (m_world.terrains.empty())
    {
        return;
    }
    const math::Vec3 eye = matrices.cameraPosition;
    m_terrainLayers.resize(m_world.terrains.size());
    std::vector<GpuTerrainNode> nodes;
    std::vector<GpuTerrainNode> tiles;
    std::vector<std::byte> uniforms;
    // The first draw is that of the blocks; three of grass follow for each terrain.
    std::vector<TerrainDrawData> draws{TerrainDrawData{.cameraPosition = eye, .time = m_shaderTime}};
    for (std::size_t index = 0; index < m_world.terrains.size(); ++index)
    {
        const RenderTerrain& terrain = m_world.terrains[index];
        const TerrainTextures& textures = terrain.textures;
        const std::array<const GpuTexture*, 5> found{m_textures.find(textures.heights), m_textures.find(textures.slopes),
                                                     m_textures.find(textures.paint0), m_textures.find(textures.paint1),
                                                     m_textures.find(textures.detail)};
        if (std::ranges::any_of(found, [](const GpuTexture* texture) { return texture == nullptr; }))
        {
            continue;
        }
        const std::optional<TerrainSelection> selection = selectTerrain(terrain, eye, frustum);
        if (!selection)
        {
            continue;
        }
        fillTerrainLayers(m_terrainLayers[index], terrain);
        TerrainDrawing drawing{.terrain = index, .castShadows = terrain.castShadows};
        for (std::size_t map = 0; map < found.size(); ++map)
        {
            drawing.maps[map] = found[map]->texture;
        }
        for (std::size_t group = 0; group < selection->nodes.size(); ++group)
        {
            drawing.firstNode[group] = static_cast<std::uint32_t>(nodes.size());
            drawing.nodeCount[group] = static_cast<std::uint32_t>(selection->nodes[group].size());
            nodes.insert(nodes.end(), selection->nodes[group].begin(), selection->nodes[group].end());
        }
        drawing.firstDraw = static_cast<std::uint32_t>(draws.size());
        for (std::size_t band = 0; band < selection->tiles.size(); ++band)
        {
            draws.push_back({.cameraPosition = eye,
                             .bladesPerTile = std::max(selection->blades[band], 1U),
                             .bladeWidthScale = selection->bladeWidth[band],
                             .time = m_shaderTime});
            drawing.firstTile[band] = static_cast<std::uint32_t>(tiles.size());
            drawing.tileCount[band] = static_cast<std::uint32_t>(selection->tiles[band].size());
            drawing.blades[band] = selection->blades[band];
            tiles.insert(tiles.end(), selection->tiles[band].begin(), selection->tiles[band].end());
        }
        // The lights of a terrain reach any of it, from far below to far above.
        const math::Aabb box{.min = terrain.origin - math::Vec3{0.0f, 1.0e4f, 0.0f},
                             .max = terrain.origin + math::Vec3{terrain.size, 1.0e4f, terrain.size}};
        TerrainData uniform{
            .origin = terrain.origin,
            .size = terrain.size,
            .resolution = terrain.resolution,
            .layerCount = std::min<std::uint32_t>(terrain.layerCount, 8),
            .textureSize = terrain.textureSize,
            .blendSharpness = terrain.blendSharpness,
            .triplanar = terrain.triplanar ? 1U : 0U,
            .autoBaseLayer = terrain.autoBaseLayer,
            .autoSlopeLayer = terrain.autoSlopeLayer,
            .autoSlope = terrain.autoSlope,
            .autoHeightLayer = terrain.autoHeightLayer,
            .autoHeight = terrain.autoHeight,
            .autoBlend = terrain.autoBlend,
            .windStrength = terrain.windStrength,
            .windSpeed = terrain.windSpeed,
            .grassDistance = terrain.grassDistance,
            .leafRange = selection->leafRange,
            .grassColor = terrain.grassColor,
            .grassHeight = terrain.grassHeight,
            .grassTipColor = terrain.grassTipColor,
            .grassWidth = terrain.grassWidth,
            .lights = lightsReaching(box, terrain.origin),
        };
        for (std::uint32_t layer = 0; layer < uniform.layerCount; ++layer)
        {
            const GpuMaterial* const material = m_materials.find(terrain.layers[layer]);
            uniform.baseColorFactors[layer] = material != nullptr ? material->description.baseColorFactor : math::Vec4{1.0f};
            uniform.layerFactors[layer] = material != nullptr ? math::Vec4(material->description.metallicFactor,
                                                                           material->description.roughnessFactor,
                                                                           material->description.normalScale, 0.0f)
                                                              : math::Vec4(0.0f, 1.0f, 1.0f, 0.0f);
        }
        uniforms.resize((m_terrainDrawings.size() + 1) * m_terrainStride);
        std::memcpy(uniforms.data() + m_terrainDrawings.size() * m_terrainStride, &uniform, sizeof(uniform));
        m_terrainDrawings.push_back(drawing);
    }
    if (m_terrainDrawings.empty())
    {
        return;
    }
    growBuffer(m_terrainBuffer, GL_UNIFORM_BUFFER, m_terrainCapacity, uniforms.size());
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(uniforms.size()), uniforms.data());
    std::vector<std::byte> drawBytes(draws.size() * m_terrainDrawStride);
    for (std::size_t draw = 0; draw < draws.size(); ++draw)
    {
        std::memcpy(drawBytes.data() + draw * m_terrainDrawStride, &draws[draw], sizeof(TerrainDrawData));
    }
    growBuffer(m_terrainDrawBuffer, GL_UNIFORM_BUFFER, m_terrainDrawCapacity, drawBytes.size());
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(drawBytes.size()), drawBytes.data());
    if (!nodes.empty())
    {
        growBuffer(m_terrainNodeBuffer, GL_ARRAY_BUFFER, m_terrainNodeCapacity, nodes.size() * sizeof(GpuTerrainNode));
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(nodes.size() * sizeof(GpuTerrainNode)), nodes.data());
    }
    if (!tiles.empty())
    {
        growBuffer(m_grassTileBuffer, GL_ARRAY_BUFFER, m_grassTileCapacity, tiles.size() * sizeof(GpuTerrainNode));
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(tiles.size() * sizeof(GpuTerrainNode)), tiles.data());
    }
}

void GlRenderer::bindTerrain(std::size_t drawn) const
{
    const TerrainDrawing& drawing = m_terrainDrawings[drawn];
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::terrain, m_terrainBuffer, static_cast<GLintptr>(drawn * m_terrainStride),
                        sizeof(TerrainData));
    // The heights, in floats, read without filtering; the other maps between their samples.
    bindTexture(unit::first, drawing.maps[0], m_pointSampler);
    for (std::size_t map = 1; map < drawing.maps.size(); ++map)
    {
        bindTexture(static_cast<GLuint>(unit::first + static_cast<GLint>(map)), drawing.maps[map], m_clampSampler);
    }
    const TerrainLayers& layers = m_terrainLayers[drawing.terrain];
    bindTextureTarget(unit::layerColors, GL_TEXTURE_2D_ARRAY, layers.colors, m_linearSampler);
    bindTextureTarget(unit::layerSurfaces, GL_TEXTURE_2D_ARRAY, layers.surfaces, m_linearSampler);
    bindTextureTarget(unit::layerNormals, GL_TEXTURE_2D_ARRAY, layers.normals, m_linearSampler);
}

void GlRenderer::drawTerrains(std::optional<std::size_t> shadowView)
{
    if (m_terrainDrawings.empty())
    {
        return;
    }
    if (!shadowView)
    {
        bindSceneTextures();
        gl::Disable(GL_CULL_FACE);
        gl::Disable(GL_BLEND);
        gl::DepthFunc(GL_GREATER);
    }
    gl::UseProgram(shadowView ? m_groundShadowProgram.program : m_groundProgram.program);
    // The draw of the blocks is the first.
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::terrainDraw, m_terrainDrawBuffer, 0, sizeof(TerrainDrawData));
    gl::BindVertexArray(m_terrainArray);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrainGrid);
    gl::BindBuffer(GL_ARRAY_BUFFER, m_terrainNodeBuffer);
    gl::VertexAttribDivisor(0, 1);
    gl::VertexAttribDivisor(1, 1);
    constexpr std::uint32_t quarterIndices = terrainBlockCells * terrainBlockCells / 4 * 6;
    for (std::size_t drawn = 0; drawn < m_terrainDrawings.size(); ++drawn)
    {
        const TerrainDrawing& drawing = m_terrainDrawings[drawn];
        if (shadowView && !drawing.castShadows)
        {
            continue;
        }
        bindTerrain(drawn);
        for (std::size_t group = 0; group < drawing.nodeCount.size(); ++group)
        {
            if (drawing.nodeCount[group] == 0)
            {
                continue;
            }
            // A whole block, or one of its quarters; its blocks are its instances.
            const std::size_t base = std::size_t{drawing.firstNode[group]} * sizeof(GpuTerrainNode);
            gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuTerrainNode), reinterpret_cast<const void*>(base));
            gl::VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(GpuTerrainNode),
                                     reinterpret_cast<const void*>(base + offsetof(GpuTerrainNode, level)));
            const std::uint32_t indexCount = group == 0 ? quarterIndices * 4 : quarterIndices;
            const std::uint32_t firstIndex = group == 0 ? 0 : static_cast<std::uint32_t>(group - 1) * quarterIndices;
            gl::DrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                                      reinterpret_cast<const void*>(std::size_t{firstIndex} * sizeof(std::uint32_t)),
                                      static_cast<GLsizei>(drawing.nodeCount[group]));
            ++m_drawCalls;
        }
    }
    gl::BindVertexArray(0);
}

void GlRenderer::drawGrass()
{
    if (m_terrainDrawings.empty())
    {
        return;
    }
    bindSceneTextures();
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_BLEND);
    gl::DepthFunc(GL_GREATER);
    gl::UseProgram(m_grassProgram.program);
    gl::BindVertexArray(m_terrainArray);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_grassBlade);
    gl::BindBuffer(GL_ARRAY_BUFFER, m_grassTileBuffer);
    for (std::size_t drawn = 0; drawn < m_terrainDrawings.size(); ++drawn)
    {
        const TerrainDrawing& drawing = m_terrainDrawings[drawn];
        bindTerrain(drawn);
        for (std::size_t band = 0; band < drawing.tileCount.size(); ++band)
        {
            if (drawing.tileCount[band] == 0 || drawing.blades[band] == 0)
            {
                continue;
            }
            gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::terrainDraw, m_terrainDrawBuffer,
                                static_cast<GLintptr>(std::size_t{drawing.firstDraw + band} * m_terrainDrawStride), sizeof(TerrainDrawData));
            // A tile is the instance of all its blades: its attributes move on once all of them are drawn.
            const std::size_t base = std::size_t{drawing.firstTile[band]} * sizeof(GpuTerrainNode);
            gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuTerrainNode), reinterpret_cast<const void*>(base));
            gl::VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(GpuTerrainNode),
                                     reinterpret_cast<const void*>(base + offsetof(GpuTerrainNode, level)));
            gl::VertexAttribDivisor(0, drawing.blades[band]);
            gl::VertexAttribDivisor(1, drawing.blades[band]);
            // Far away, one triangle a blade.
            const bool far = band + 1 == drawing.tileCount.size();
            gl::DrawElementsInstanced(GL_TRIANGLES, far ? 3 : 15, GL_UNSIGNED_INT,
                                      reinterpret_cast<const void*>(std::size_t{far ? 15U : 0U} * sizeof(std::uint32_t)),
                                      static_cast<GLsizei>(drawing.tileCount[band] * drawing.blades[band]));
            ++m_drawCalls;
        }
    }
    gl::VertexAttribDivisor(0, 1);
    gl::VertexAttribDivisor(1, 1);
    gl::BindVertexArray(0);
}

void GlRenderer::orderTransparent(const ViewMatrices& matrices)
{
    // Blended meshes lie in the layer "Default", which sprites place themselves around: by layer, then order, then from
    // the farthest. The blended batches come sorted, and sprite batches in this order among themselves, which a stable
    // sort keeps.
    struct Item
    {
        std::int32_t layer = 0;
        std::int32_t order = 0;
        float distance = 0.0f;
        bool sprites = false;
        bool particles = false;
        std::uint32_t index = 0;
    };
    std::vector<Item> items;
    for (std::uint32_t batch = 0; batch < m_meshBatches.size(); ++batch)
    {
        if ((m_meshBatches[batch].flags & blendedFlag) != 0)
        {
            items.push_back({.distance = m_meshBatches[batch].distance, .index = batch});
        }
    }
    for (std::uint32_t draw = 0; draw < m_world.particleDraws.size(); ++draw)
    {
        if (m_world.particleDraws[draw].count > 0)
        {
            items.push_back({.distance = sortDistance(m_world.particleDraws[draw].center, matrices.cameraPosition),
                             .particles = true,
                             .index = draw});
        }
    }
    for (std::uint32_t batch = 0; batch < m_spriteFrame.batches.size(); ++batch)
    {
        const SpriteBatch& sprites = m_spriteFrame.batches[batch];
        if (sprites.count > 0)
        {
            items.push_back({.layer = sprites.layer, .order = sprites.order, .distance = sprites.distance, .sprites = true, .index = batch});
        }
    }
    std::ranges::stable_sort(items, [](const Item& a, const Item& b) {
        if (a.layer != b.layer)
        {
            return a.layer < b.layer;
        }
        if (a.order != b.order)
        {
            return a.order < b.order;
        }
        return a.distance > b.distance;
    });
    // Sprite batches that follow each other lie one after the other among the sprites: they are drawn by runs of one texture.
    m_transparentDraws.clear();
    m_spriteRuns.clear();
    for (std::size_t at = 0; at < items.size();)
    {
        if (!items[at].sprites)
        {
            m_transparentDraws.push_back({.particles = items[at].particles, .index = items[at].index});
            ++at;
            continue;
        }
        const std::uint32_t first = m_spriteFrame.batches[items[at].index].first;
        std::uint32_t end = first;
        while (at < items.size() && items[at].sprites && m_spriteFrame.batches[items[at].index].first == end)
        {
            end += m_spriteFrame.batches[items[at].index].count;
            ++at;
        }
        TransparentDraw draw{.sprites = true, .firstRun = static_cast<std::uint32_t>(m_spriteRuns.size())};
        for (std::uint32_t index = first; index < end; ++index)
        {
            const std::uint32_t texture = m_spriteFrame.sprites[index].texture;
            if (m_spriteRuns.size() == draw.firstRun || m_spriteRuns.back().texture != texture)
            {
                m_spriteRuns.push_back({.first = index, .texture = texture});
            }
            ++m_spriteRuns.back().count;
        }
        draw.runCount = static_cast<std::uint32_t>(m_spriteRuns.size()) - draw.firstRun;
        m_transparentDraws.push_back(draw);
    }
}

GLuint GlRenderer::materialUniform(GpuMaterial& material) const
{
    if (material.uniform == 0)
    {
        gl::GenBuffers(1, &material.uniform);
        gl::BindBuffer(GL_UNIFORM_BUFFER, material.uniform);
        gl::BufferData(GL_UNIFORM_BUFFER, sizeof(MaterialData), nullptr, GL_DYNAMIC_DRAW);
    }
    if (!material.written)
    {
        const MaterialDesc& description = material.description;
        const MaterialData data{
            .baseColorFactor = description.baseColorFactor,
            .emissiveFactor = description.emissiveFactor,
            .alphaCutoff = description.alphaCutoff,
            .metallicFactor = description.metallicFactor,
            .roughnessFactor = description.roughnessFactor,
            .normalScale = description.normalScale,
            .occlusionStrength = description.occlusionStrength,
            .alphaMode = static_cast<std::uint32_t>(description.alphaMode),
            .doubleSided = description.doubleSided ? 1U : 0U,
        };
        gl::BindBuffer(GL_UNIFORM_BUFFER, material.uniform);
        gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
        material.written = true;
    }
    return material.uniform;
}

void GlRenderer::bindMaterial(GpuMaterial& material, bool shadow) const
{
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::material, materialUniform(material), 0, sizeof(MaterialData));
    // Invalid or destroyed textures sample as white, or as a flat normal for the normal texture.
    const auto bind = [this](GLint textureUnit, TextureHandle handle, GLuint fallback) {
        const GpuTexture* const texture = m_textures.find(handle);
        const bool found = texture != nullptr && texture->bindable;
        bindTexture(static_cast<GLuint>(textureUnit), found ? texture->texture : fallback,
                    found && texture->nearest ? m_nearestSampler : m_linearSampler);
    };
    const MaterialDesc& description = material.description;
    bind(unit::first, description.baseColorTexture, m_white);
    if (shadow)
    {
        return;
    }
    bind(unit::second, description.metallicRoughnessTexture, m_white);
    bind(unit::third, description.normalTexture, m_flatNormal);
    bind(unit::fourth, description.occlusionTexture, m_white);
    bind(unit::fifth, description.emissiveTexture, m_white);
}

void GlRenderer::bindInstances(const GpuMesh& mesh, std::uint32_t firstInstance) const
{
    gl::BindVertexArray(mesh.array);
    gl::BindBuffer(GL_ARRAY_BUFFER, m_instanceBuffer);
    // OpenGL ES 3.0 has no first instance: the attributes start at the batch.
    const std::size_t base = std::size_t{firstInstance} * sizeof(InstanceData);
    const auto at = [base](std::size_t offset) { return reinterpret_cast<const void*>(base + offset); };
    constexpr GLsizei stride = sizeof(InstanceData);
    for (GLuint column = 0; column < 4; ++column)
    {
        gl::VertexAttribPointer(6 + column, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(InstanceData, world) + column * sizeof(math::Vec4)));
    }
    gl::VertexAttribIPointer(10, 2, GL_UNSIGNED_INT, stride, at(offsetof(InstanceData, lights)));
    gl::VertexAttribIPointer(11, 2, GL_UNSIGNED_INT, stride, at(offsetof(InstanceData, firstBone)));
    gl::VertexAttribIPointer(12, 2, GL_UNSIGNED_INT, stride, at(offsetof(InstanceData, decals)));
}

void GlRenderer::bindSceneTextures() const
{
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::scene, m_sceneBuffer, 0, sizeof(SceneData));
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::lights, m_lightBuffer, 0, sizeof(LightData));
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::shadowViews, m_shadowViewBuffer, 0, sizeof(ShadowViewData));
    bindTextureTarget(unit::irradiance, GL_TEXTURE_CUBE_MAP, m_environment.irradiance, m_cubeSampler);
    bindTextureTarget(unit::specular, GL_TEXTURE_CUBE_MAP, m_environment.specular, m_cubeSampler);
    bindTextureTarget(unit::brdf, GL_TEXTURE_2D, m_brdfTable, m_clampSampler);
    bindTextureTarget(unit::cascades, GL_TEXTURE_2D_ARRAY, m_cascadeMap, m_shadowSampler);
    bindTextureTarget(unit::atlas, GL_TEXTURE_2D, m_atlasMap, m_shadowSampler);
    bindTextureTarget(unit::bones, GL_TEXTURE_2D, m_boneTexture, m_pointSampler);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::decals, m_decalBuffer, 0, sizeof(DecalData));
    bindTextureTarget(unit::decalColors, GL_TEXTURE_2D, m_decalColors.texture != 0 ? m_decalColors.texture : m_white, m_clampSampler);
    bindTextureTarget(unit::decalSurfaces, GL_TEXTURE_2D, m_decalSurfaces.texture != 0 ? m_decalSurfaces.texture : m_white,
                      m_clampSampler);
}

void GlRenderer::drawShadows()
{
    if (m_shadowPasses.empty())
    {
        return;
    }
    const std::size_t matrixStride = (sizeof(math::Mat4) + static_cast<std::size_t>(m_uniformAlignment) - 1) /
                                     static_cast<std::size_t>(m_uniformAlignment) * static_cast<std::size_t>(m_uniformAlignment);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_CULL_FACE);
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_LEQUAL);
    gl::DepthMask(GL_TRUE);
    gl::Enable(GL_POLYGON_OFFSET_FILL);
    if (m_depthClamp)
    {
        gl::Enable(GL_DEPTH_CLAMP);
    }
    if (gl::ClearDepthf != nullptr)
    {
        gl::ClearDepthf(1.0f);
    }
    else
    {
        gl::ClearDepth(1.0);
    }
    bindTextureTarget(unit::bones, GL_TEXTURE_2D, m_boneTexture, m_pointSampler);
    const auto drawBatches = [&](const ShadowPass& pass, std::size_t view) {
        gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::shadowView, m_shadowMatrixBuffer, static_cast<GLintptr>(view * matrixStride),
                            sizeof(math::Mat4));
        // Steep surfaces need more bias than those facing the light.
        gl::PolygonOffset(pass.atlas ? 2.0f : 1.5f, 0.0f);
        for (std::uint32_t index = pass.firstBatch; index < pass.firstBatch + pass.batchCount; ++index)
        {
            const MeshBatch& batch = m_shadowBatches[index];
            GpuMaterial* const material = m_materials.find(batch.material);
            if (material == nullptr)
            {
                continue;
            }
            gl::UseProgram((batch.flags & skinnedFlag) != 0 ? m_skinnedShadowProgram.program : m_shadowProgram.program);
            bindMaterial(*material, true);
            bindInstances(*batch.mesh, batch.firstInstance);
            const asset::Submesh& submesh = batch.mesh->submeshes[batch.submesh];
            gl::DrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(submesh.indexCount), GL_UNSIGNED_INT,
                                      reinterpret_cast<const void*>(std::size_t{submesh.firstIndex} * sizeof(std::uint32_t)),
                                      static_cast<GLsizei>(batch.instanceCount));
            ++m_drawCalls;
        }
        drawTerrains(view);
    };
    // Each cascade into its layer.
    for (std::size_t view = 0; view < m_shadowPasses.size(); ++view)
    {
        const ShadowPass& pass = m_shadowPasses[view];
        if (!pass.atlas)
        {
            gl::BindFramebuffer(GL_FRAMEBUFFER, m_cascadeFramebuffers[pass.layer]);
            gl::Viewport(0, 0, static_cast<GLsizei>(m_shadowMapSize), static_cast<GLsizei>(m_shadowMapSize));
            gl::Disable(GL_SCISSOR_TEST);
            gl::Clear(GL_DEPTH_BUFFER_BIT);
            drawBatches(pass, view);
        }
    }
    // The lights into their tiles of the atlas.
    if (std::ranges::any_of(m_shadowPasses, &ShadowPass::atlas))
    {
        gl::BindFramebuffer(GL_FRAMEBUFFER, m_atlasFramebuffer);
        gl::Disable(GL_SCISSOR_TEST);
        gl::Viewport(0, 0, static_cast<GLsizei>(m_shadowAtlasSize), static_cast<GLsizei>(m_shadowAtlasSize));
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        gl::Enable(GL_SCISSOR_TEST);
        for (std::size_t view = 0; view < m_shadowPasses.size(); ++view)
        {
            const ShadowPass& pass = m_shadowPasses[view];
            if (pass.atlas)
            {
                // OpenGL counts the rows of the atlas from the first stored, as the views read them.
                const auto x = static_cast<GLint>(pass.viewport[0]);
                const auto y = static_cast<GLint>(pass.viewport[1]);
                const auto width = static_cast<GLsizei>(pass.viewport[2]);
                const auto height = static_cast<GLsizei>(pass.viewport[3]);
                gl::Viewport(x, y, width, height);
                gl::Scissor(x, y, width, height);
                drawBatches(pass, view);
            }
        }
        gl::Disable(GL_SCISSOR_TEST);
    }
    gl::Disable(GL_POLYGON_OFFSET_FILL);
    if (m_depthClamp)
    {
        gl::Disable(GL_DEPTH_CLAMP);
    }
    gl::BindVertexArray(0);
}

void GlRenderer::drawMeshBatch(const MeshBatch& batch)
{
    GpuMaterial* const material = m_materials.find(batch.material);
    if (material == nullptr)
    {
        return;
    }
    const bool blended = (batch.flags & blendedFlag) != 0;
    gl::UseProgram((batch.flags & skinnedFlag) != 0 ? m_skinnedProgram.program : m_meshProgram.program);
    if ((batch.flags & bothSidesFlag) != 0)
    {
        gl::Disable(GL_CULL_FACE);
    }
    else
    {
        gl::Enable(GL_CULL_FACE);
        gl::CullFace(GL_BACK);
        gl::FrontFace(GL_CCW);
    }
    if (blended)
    {
        // A blended surface lets (1 - alpha) of what is behind it through.
        gl::Enable(GL_BLEND);
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        gl::Disable(GL_BLEND);
    }
    bindMaterial(*material, false);
    bindInstances(*batch.mesh, batch.firstInstance);
    const asset::Submesh& submesh = batch.mesh->submeshes[batch.submesh];
    gl::DrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(submesh.indexCount), GL_UNSIGNED_INT,
                              reinterpret_cast<const void*>(std::size_t{submesh.firstIndex} * sizeof(std::uint32_t)),
                              static_cast<GLsizei>(batch.instanceCount));
    ++m_drawCalls;
}

void GlRenderer::drawMeshes(bool blended)
{
    bindSceneTextures();
    gl::DepthFunc(GL_GREATER);
    for (const MeshBatch& batch : m_meshBatches)
    {
        if (((batch.flags & blendedFlag) != 0) == blended)
        {
            drawMeshBatch(batch);
        }
    }
    gl::BindVertexArray(0);
    gl::Disable(GL_BLEND);
}

void GlRenderer::drawTransparent()
{
    bindSceneTextures();
    for (const TransparentDraw& draw : m_transparentDraws)
    {
        if (draw.sprites)
        {
            drawSprites(draw.firstRun, draw.runCount);
        }
        else if (draw.particles)
        {
            drawParticles(draw.index);
        }
        else
        {
            // Tested against the opaque surfaces without writing their depth.
            gl::DepthFunc(GL_GREATER);
            drawMeshBatch(m_meshBatches[draw.index]);
        }
    }
    gl::BindVertexArray(0);
    gl::Disable(GL_BLEND);
}

void GlRenderer::updateExposure()
{
    const RenderCamera& camera = m_world.camera;
    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds =
        m_exposureInitialized ? std::clamp(std::chrono::duration<float>(now - m_lastExposureUpdate).count(), 0.0f, 0.25f) : 0.0f;
    m_lastExposureUpdate = now;
    if (!camera.autoExposure || !m_exposureInitialized)
    {
        m_ev100 = camera.ev100 - camera.exposureCompensation;
        m_targetEv100 = m_ev100;
        m_exposureInitialized = true;
        if (!camera.autoExposure)
        {
            m_snapExposure = false;
            return;
        }
    }
    // The grid a frame before measured, once the GPU is done with it.
    if (m_luminanceFence != nullptr)
    {
        const GLenum status = gl::ClientWaitSync(m_luminanceFence, 0, 0);
        if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
        {
            gl::DeleteSync(m_luminanceFence);
            m_luminanceFence = nullptr;
            constexpr std::size_t samples = std::size_t{luminanceGridWidth} * luminanceGridHeight;
            gl::BindBuffer(GL_PIXEL_PACK_BUFFER, m_luminanceBuffer);
            if (const auto* const mapped = static_cast<const float*>(
                    gl::MapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(samples * 4 * sizeof(float)), GL_MAP_READ_BIT));
                mapped != nullptr && m_measuredExposure > 0.0f)
            {
                std::vector<float> luminances(samples);
                for (std::size_t sample = 0; sample < samples; ++sample)
                {
                    luminances[sample] = mapped[sample * 4] / m_measuredExposure;
                }
                static_cast<void>(gl::UnmapBuffer(GL_PIXEL_PACK_BUFFER));
                if (const float average = averageLuminance(luminances); average > 0.0f)
                {
                    m_targetEv100 =
                        std::clamp(ev100FromAverageLuminance(average) - camera.exposureCompensation, camera.minEv100, camera.maxEv100);
                    if (m_snapExposure)
                    {
                        m_ev100 = m_targetEv100;
                        m_snapExposure = false;
                    }
                }
            }
            gl::BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
    }
    m_ev100 = adaptExposure(m_ev100, m_targetEv100, camera.adaptationSpeed, deltaSeconds);
}

void GlRenderer::measureLuminance()
{
    // A system that draws no half floats keeps the exposure the camera sets.
    if (!m_world.camera.autoExposure || m_luminanceFence != nullptr || !m_halfFloatTargets)
    {
        return;
    }
    if (core::Result<void> target =
            ensureTarget(m_luminanceTarget, {luminanceGridWidth, luminanceGridHeight}, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
        !target)
    {
        return;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, m_luminanceTarget.framebuffer);
    gl::Viewport(0, 0, luminanceGridWidth, luminanceGridHeight);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_BLEND);
    const BakeData grid{.size = luminanceGridWidth, .sourceSize = static_cast<float>(luminanceGridHeight)};
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_bakeBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(grid), &grid);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::bake, m_bakeBuffer, 0, sizeof(BakeData));
    gl::UseProgram(m_luminanceProgram.program);
    bindTextureTarget(unit::third, GL_TEXTURE_2D, m_scene.texture, m_clampSampler);
    gl::BindVertexArray(m_emptyArray);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    gl::BindBuffer(GL_PIXEL_PACK_BUFFER, m_luminanceBuffer);
    gl::ReadPixels(0, 0, luminanceGridWidth, luminanceGridHeight, GL_RGBA, GL_FLOAT, nullptr);
    gl::BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_luminanceFence = gl::FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_measuredExposure = m_exposure;
}

} // namespace devex::render::opengl
