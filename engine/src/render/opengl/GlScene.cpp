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
// An instance as gl_mesh.slang reads its attributes: the columns of its matrix, the bytes of its lights, its bones.
struct InstanceData
{
    math::Mat4 world{1.0f};
    std::array<std::uint32_t, 2> lights{0xFFFFFFFFU, 0xFFFFFFFFU};
    std::uint32_t firstBone = 0;
    std::uint32_t boneCount = 0;
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
              sizeof(MaterialData) == 64 && sizeof(InstanceData) == 80 && sizeof(BakeData) == 32);

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
    const std::array<std::tuple<Program*, std::string_view, std::string_view>, 9> programs{{
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
                     {"MaterialData", binding::material}},
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
                     {"boneTexture", unit::bones}});
    }
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

    m_pointSampler = makeSampler(GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    m_skySampler = makeSampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_CLAMP_TO_EDGE);
    m_cubeSampler = makeSampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    m_shadowSampler = makeSampler(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    gl::SamplerParameteri(m_shadowSampler, GL_TEXTURE_COMPARE_MODE, static_cast<GLint>(GL_COMPARE_REF_TO_TEXTURE));
    gl::SamplerParameteri(m_shadowSampler, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(GL_LEQUAL));
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
                                   &m_prefilterProgram, &m_irradianceProgram, &m_brdfProgram, &m_luminanceProgram})
    {
        gl::DeleteProgram(program->program);
    }
    const GLuint buffers[] = {m_sceneBuffer, m_lightBuffer, m_shadowViewBuffer, m_bakeBuffer, m_shadowMatrixBuffer, m_instanceBuffer,
                              m_luminanceBuffer};
    gl::DeleteBuffers(7, buffers);
    const GLuint samplers[] = {m_pointSampler, m_skySampler, m_cubeSampler, m_shadowSampler};
    gl::DeleteSamplers(4, samplers);
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
    // At most 8 an object: those that reach its box, the nearest first.
    std::vector<std::pair<float, std::uint32_t>> reaching;
    const auto lightsOf = [&](const math::Aabb& box, math::Vec3 centre) {
        reaching.clear();
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
    };

    // The instances the camera sees, by batches of one mesh, submesh, material and way to be drawn; blended ones alone,
    // the farthest first.
    struct Visible
    {
        const MeshInstance* instance;
        const GpuMesh* mesh;
        MaterialHandle material;
        std::uint32_t flags;
        float distance;
        math::Aabb box;
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
                           skinned ? math::Aabb{} : box});
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
        instances.push_back({.world = entry.instance->transform,
                             .lights = lightsOf(entry.box, math::Vec3(entry.instance->transform[3])),
                             .firstBone = entry.instance->firstBone,
                             .boneCount = entry.instance->boneCount});
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

void GlRenderer::orderTransparent()
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
            m_transparentDraws.push_back({.index = items[at].index});
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
