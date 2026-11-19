// The shaders of projects in the OpenGL renderer, as WebGPU draws them: their programs, linked when first drawn from the
// GLSL the import translated, the uniforms and the textures of their materials, the values objects give their instance
// uniforms, and the sky a sky shader draws, laid out for the light of the environment.
#include "GlRenderer.hpp"

#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <string_view>
#include <utility>

namespace devex::render::opengl {
namespace {

using namespace gl;

constexpr std::size_t customSlotCount = 64;
constexpr std::size_t customTextureCount = 8;
constexpr std::size_t maxInstanceParameters = 1024;

// The entry points of the program that draws `which`, for a kind of shader.
[[nodiscard]] std::pair<std::string_view, std::string_view> entriesOf(asset::ShaderKind kind, std::size_t which)
{
    switch (which)
    {
    case 0:
        return {"meshVertex", "sceneFragment"};
    case 1:
        return {"skinnedVertex", "sceneFragment"};
    case 2:
        return {"shadowVertex", "shadowFragment"};
    case 3:
        return {"skinnedShadowVertex", "shadowFragment"};
    case 5:
        return {"skyVertex", "bakeFragment"};
    default:
        break;
    }
    switch (kind)
    {
    case asset::ShaderKind::CanvasItem:
        return {"spriteVertex", "spriteFragment"};
    case asset::ShaderKind::Particles:
        return {"particleVertex", "particleFragment"};
    case asset::ShaderKind::Sky:
        return {"skyVertex", "skyFragment"};
    case asset::ShaderKind::Spatial:
        break;
    }
    return {};
}

} // namespace

GlRenderer::GpuShader GlRenderer::gpuShaderOf(const asset::ShaderData& shader)
{
    return GpuShader{.kind = shader.kind,
                     .cull = shader.cull,
                     .blend = shader.blend,
                     .transparent = shader.transparent,
                     .castsShadows = shader.castsShadows,
                     .glsl = shader.glsl};
}

void GlRenderer::releaseShader(GpuShader& shader) noexcept
{
    for (GLuint& program : shader.programs)
    {
        if (program != 0)
        {
            gl::DeleteProgram(program);
            program = 0;
        }
    }
    shader.tried = {};
}

core::Result<ShaderHandle> GlRenderer::createShader(const asset::ShaderData& shader)
{
    GpuShader gpu = gpuShaderOf(shader);
    gpu.revision = ++m_textureRevision;
    return m_shaders.insert(std::move(gpu));
}

core::Result<void> GlRenderer::updateShader(ShaderHandle handle, const asset::ShaderData& shader)
{
    GpuShader* const stored = m_shaders.find(handle);
    if (stored == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "no such shader");
    }
    releaseShader(*stored);
    *stored = gpuShaderOf(shader);
    // The materials that draw with it bind it again.
    stored->revision = ++m_textureRevision;
    return {};
}

void GlRenderer::destroyShader(ShaderHandle shader)
{
    if (std::optional<GpuShader> removed = m_shaders.remove(shader))
    {
        releaseShader(*removed);
    }
}

GlRenderer::GpuShader* GlRenderer::shaderOf(const MaterialDesc& material, asset::ShaderKind kind)
{
    GpuShader* const shader = material.shader.isValid() ? m_shaders.find(material.shader) : nullptr;
    return shader != nullptr && shader->kind == kind && !shader->glsl.empty() ? shader : nullptr;
}

GlRenderer::GpuShader* GlRenderer::shaderOf(MaterialHandle material, asset::ShaderKind kind)
{
    const GpuMaterial* const found = material.isValid() ? m_materials.find(material) : nullptr;
    return found != nullptr ? shaderOf(found->description, kind) : nullptr;
}

GLuint GlRenderer::customProgram(GpuShader& shader, CustomProgram which)
{
    if (shader.tried[which])
    {
        return shader.programs[which];
    }
    shader.tried[which] = true;
    const auto [vertex, fragment] = entriesOf(shader.kind, which);
    const auto stageOf = [&shader](std::string_view entry) -> const asset::GlslStage* {
        const auto found = std::ranges::find(shader.glsl, entry, &asset::GlslStage::entry);
        return found != shader.glsl.end() ? &*found : nullptr;
    };
    const asset::GlslStage* const vertexStage = stageOf(vertex);
    const asset::GlslStage* const fragmentStage = stageOf(fragment);
    if (vertexStage == nullptr || fragmentStage == nullptr)
    {
        return 0;
    }
    core::Result<Program> linked =
        linkSources(vertex, m_es ? vertexStage->es : vertexStage->core, fragment, m_es ? fragmentStage->es : fragmentStage->core);
    if (!linked)
    {
        DEVEX_LOG_WARNING("{}: its materials draw as the standard one", linked.error());
        return 0;
    }
    const Program& program = *linked;
    // The textures of the material at units of their own, then what each kind reads beside them.
    constexpr std::array<const char*, customTextureCount> textures{"customTexture0", "customTexture1", "customTexture2", "customTexture3",
                                                                   "customTexture4", "customTexture5", "customTexture6", "customTexture7"};
    for (std::size_t index = 0; index < textures.size(); ++index)
    {
        bindProgram(program, {}, {{textures[index], unit::customTextures + static_cast<GLint>(index)}});
    }
    bindProgram(program,
                {{"SceneData", binding::scene},
                 {"LightData", binding::lights},
                 {"ShadowViewData", binding::shadowViews},
                 {"CustomSlots", binding::customMaterial},
                 {"CustomInstances", binding::customInstances},
                 {"ViewData", binding::shadowView},
                 {"SpriteFrame", binding::spriteFrame},
                 {"Light2DData", binding::lights2D},
                 {"ParticleDraw", binding::particleDraw}},
                {{"irradianceEnvironment", unit::irradiance},
                 {"specularEnvironment", unit::specular},
                 {"brdfLut", unit::brdf},
                 {"cascadeMap", unit::cascades},
                 {"atlasMap", unit::atlas},
                 {"boneTexture", unit::bones},
                 {"spriteTexture", unit::first},
                 {"normalTexture", unit::second},
                 {"shadows2D", unit::third},
                 {"particleTexture", unit::first},
                 {"sceneDepth", unit::second}});
    shader.programs[which] = program.program;
    return program.program;
}

void GlRenderer::bindCustomMaterial(GpuMaterial& material)
{
    const MaterialDesc& description = material.description;
    // The values of the uniforms, then the number of the texture in the slot of each texture uniform.
    if (material.customSlots == 0 || material.customRevision != m_textureRevision || !material.customWritten)
    {
        std::array<math::Vec4, customSlotCount> slots{};
        for (std::size_t slot = 0; slot < std::min(description.parameters.size(), slots.size()); ++slot)
        {
            slots[slot] = description.parameters[slot];
        }
        for (std::uint32_t index = 0; index < std::min<std::size_t>(description.textures.size(), customTextureCount); ++index)
        {
            if (const std::uint32_t slot = description.textures[index].slot; slot < slots.size())
            {
                slots[slot].x = std::bit_cast<float>(index);
            }
        }
        if (material.customSlots == 0)
        {
            gl::GenBuffers(1, &material.customSlots);
            gl::BindBuffer(GL_UNIFORM_BUFFER, material.customSlots);
            gl::BufferData(GL_UNIFORM_BUFFER, sizeof(slots), nullptr, GL_DYNAMIC_DRAW);
        }
        gl::BindBuffer(GL_UNIFORM_BUFFER, material.customSlots);
        gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(slots), slots.data());
        material.customRevision = m_textureRevision;
        material.customWritten = true;
    }
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::customMaterial, material.customSlots, 0, sizeof(math::Vec4) * customSlotCount);
    for (std::uint32_t index = 0; index < customTextureCount; ++index)
    {
        GLuint bound = m_white;
        GLuint sampler = m_linearSampler;
        if (index < description.textures.size())
        {
            const MaterialTexture& texture = description.textures[index];
            const GpuTexture* const found = m_textures.find(texture.texture);
            const bool bindable = found != nullptr && found->bindable;
            bound = bindable                                      ? found->texture
                    : texture.fallback == TextureFallback::Normal ? m_flatNormal
                    : texture.fallback == TextureFallback::Black  ? m_black
                                                                  : m_white;
            sampler = bindable && found->nearest ? m_nearestSampler : m_linearSampler;
        }
        bindTexture(static_cast<GLuint>(unit::customTextures) + index, bound, sampler);
    }
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::customInstances, m_customInstanceBuffer, 0,
                        sizeof(math::Vec4) * maxInstanceParameters);
}

void GlRenderer::uploadInstanceParameters()
{
    if (m_customInstanceBuffer == 0)
    {
        gl::GenBuffers(1, &m_customInstanceBuffer);
        gl::BindBuffer(GL_UNIFORM_BUFFER, m_customInstanceBuffer);
        gl::BufferData(GL_UNIFORM_BUFFER, sizeof(math::Vec4) * maxInstanceParameters, nullptr, GL_DYNAMIC_DRAW);
    }
    // OpenGL ES 3.0 holds 16 kilobytes in a uniform block: past 1024 values, objects take those of their material.
    const std::size_t count = std::min(m_world.instanceParameters.size(), maxInstanceParameters);
    if (count > 0)
    {
        gl::BindBuffer(GL_UNIFORM_BUFFER, m_customInstanceBuffer);
        gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(math::Vec4)), m_world.instanceParameters.data());
    }
}

GlRenderer::GpuShader* GlRenderer::skyShader()
{
    return shaderOf(m_world.environment.skyMaterial, asset::ShaderKind::Sky);
}

bool GlRenderer::updateSkyShader()
{
    GpuShader* const shader = skyShader();
    GpuMaterial* const material = shader != nullptr ? m_materials.find(m_world.environment.skyMaterial) : nullptr;
    if (material == nullptr || !m_halfFloatTargets)
    {
        return false;
    }
    // What the sky is drawn from, and the light it shows, as numbers that change when they do, as Vulkan keeps them.
    const auto mix = [](std::uint64_t hash, const void* data, std::size_t size) {
        const auto* const bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash = (hash ^ bytes[index]) * 1099511628211ULL;
        }
        return hash;
    };
    const MaterialHandle handle = m_world.environment.skyMaterial;
    const MaterialDesc& description = material->description;
    std::uint64_t key = 14695981039346656037ULL;
    key = mix(key, &handle, sizeof(handle));
    key = mix(key, &shader->revision, sizeof(shader->revision));
    if (!description.parameters.empty())
    {
        key = mix(key, description.parameters.data(), description.parameters.size() * sizeof(math::Vec4));
    }
    for (const MaterialTexture& texture : description.textures)
    {
        key = mix(key, &texture.texture, sizeof(texture.texture));
    }
    std::uint64_t light = 14695981039346656037ULL;
    light = mix(light, &m_world.sun.direction, sizeof(m_world.sun.direction));
    light = mix(light, &m_world.sun.illuminance, sizeof(m_world.sun.illuminance));
    light = mix(light, &m_world.environment.rotation, sizeof(m_world.environment.rotation));
    const auto now = std::chrono::steady_clock::now();
    if (m_shaderSky.framebuffer != 0 && key == m_shaderSkyKey &&
        (light == m_shaderSkyLight || now - m_shaderSkyBaked < std::chrono::milliseconds(500)))
    {
        return false;
    }
    const GLuint program = customProgram(*shader, customBake);
    if (program == 0)
    {
        return false;
    }
    // The whole sky laid out as an equirectangular image, as a sky texture is, then baked as one.
    constexpr math::Extent2D skySize{512, 256};
    if (core::Result<void> made = ensureTarget(m_shaderSky, skySize, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT); !made)
    {
        DEVEX_LOG_WARNING("{}: the sky of the shader does not light the scene", made.error());
        return false;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, m_shaderSky.framebuffer);
    gl::Viewport(0, 0, static_cast<GLsizei>(skySize.width), static_cast<GLsizei>(skySize.height));
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    gl::UseProgram(program);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::scene, m_sceneBuffer, 0, static_cast<GLsizeiptr>(sceneDataSize()));
    bindCustomMaterial(*material);
    gl::BindVertexArray(m_emptyArray);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    ++m_drawCalls;
    gl::BindVertexArray(0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    bakeEnvironmentFrom(m_shaderSky.texture, skySize.width);
    m_environment.sky = {};
    m_environment.baked = true;
    m_shaderSkyKey = key;
    m_shaderSkyLight = light;
    m_shaderSkyBaked = now;
    return true;
}

} // namespace devex::render::opengl
