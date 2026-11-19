#include "GlRenderer.hpp"

#include "GlShaders.hpp"

#include "../BlockDecoding.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/asset/ShaderData.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Culling.hpp>
#include <devex/render/Photometry.hpp>
#include <devex/render/PostProcess.hpp>

#include <devex/asset/TextureData.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace devex::render::opengl {
namespace {

using namespace gl;

// The uniform blocks of shaders/gl_*.slang, laid out as std140 lays them out.
struct SpriteFrameData
{
    math::Mat4 viewProjection{1.0f};
    math::Vec4 canvasModulate{1.0f};
};
struct UiDrawData
{
    math::Vec4 rect{0.0f};
    math::Vec2 inverseViewport{0.0f};
    float radius = 0.0f;
    float sharpness = 1.0f;
    std::uint32_t kind = 0;
    std::uint32_t displaySpace = 0;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};
struct TonemapData
{
    std::uint32_t tonemapper = 0;
    float vignette = 0.0f;
    float grain = 0.0f;
    float time = 0.0f;
    float targetWidth = 1.0f;
    float targetHeight = 1.0f;
    std::uint32_t encode = 1;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float bloom = 0.0f;
    float chromatic = 0.0f;
    float colorTableSize = 0.0f;
    math::Vec4 filter{1.0f};
    math::Vec4 balance{1.0f};
};
static_assert(sizeof(SpriteFrameData) == 80 && sizeof(UiDrawData) == 48 && sizeof(TonemapData) == 80);

[[nodiscard]] std::string text(const GLubyte* value)
{
    return value != nullptr ? std::string(reinterpret_cast<const char*>(value)) : std::string{};
}

void DEVEX_GL_CALL forwardDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message,
                                       const void* user)
{
    static_cast<void>(source);
    static_cast<void>(id);
    static_cast<void>(user);
    const std::string_view said(message, length >= 0 ? static_cast<std::size_t>(length) : std::strlen(message));
    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH)
    {
        DEVEX_LOG_ERROR("OpenGL: {}", said);
    }
    else if (type == GL_DEBUG_TYPE_PERFORMANCE)
    {
        // A driver says it compiles a shader again for the state it is drawn with, which happens once.
        DEVEX_LOG_DEBUG("OpenGL: {}", said);
    }
    else if (severity == GL_DEBUG_SEVERITY_MEDIUM)
    {
        DEVEX_LOG_WARNING("OpenGL: {}", said);
    }
}

// How OpenGL stores a format of texture: its internal format, and the format and type of its pixels, or its blocks.
struct FormatLayout
{
    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    std::uint32_t bytesPerPixel = 4;
    bool compressed = false;
};

[[nodiscard]] FormatLayout layoutOf(asset::TextureFormat format) noexcept
{
    switch (format)
    {
    case asset::TextureFormat::Rgba8Unorm:
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
    case asset::TextureFormat::Rgba8Srgb:
        return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
    case asset::TextureFormat::Rgba16Float:
        return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8};
    case asset::TextureFormat::R8Unorm:
        return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
    case asset::TextureFormat::R32Float:
        return {GL_R32F, GL_RED, GL_FLOAT, 4};
    case asset::TextureFormat::Bc5Unorm:
        return {.internalFormat = GL_COMPRESSED_RG_RGTC2, .compressed = true};
    case asset::TextureFormat::Bc7Unorm:
        return {.internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM, .compressed = true};
    case asset::TextureFormat::Bc7Srgb:
        return {.internalFormat = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, .compressed = true};
    case asset::TextureFormat::Astc4x4Unorm:
        return {.internalFormat = GL_COMPRESSED_RGBA_ASTC_4X4, .compressed = true};
    case asset::TextureFormat::Astc4x4Srgb:
        return {.internalFormat = GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4X4, .compressed = true};
    }
    return {};
}

[[nodiscard]] core::Result<GLuint> compileShader(GLenum stage, std::string_view name, std::string_view source)
{
    if (source.empty())
    {
        return core::makeError(core::ErrorCode::NotFound, "no shader {} was translated for OpenGL", name);
    }
    const GLuint shader = gl::CreateShader(stage);
    const GLchar* const sources[] = {source.data()};
    const GLint lengths[] = {static_cast<GLint>(source.size())};
    gl::ShaderSource(shader, 1, sources, lengths);
    gl::CompileShader(shader);
    GLint compiled = 0;
    gl::GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == 0)
    {
        GLint length = 0;
        gl::GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        gl::GetShaderInfoLog(shader, length, nullptr, log.data());
        gl::DeleteShader(shader);
        return core::makeError(core::ErrorCode::Graphics, "OpenGL cannot compile {}: {}", name, log.c_str());
    }
    return shader;
}

// Where a batch of the interface is cut, in pixels from the top left corner, within an image of `size`.
void scissorOf(const UiDraw& draw, math::Extent2D size, std::uint32_t (&scissor)[4])
{
    if (draw.clip.z > draw.clip.x && draw.clip.w > draw.clip.y)
    {
        const float left = std::max(draw.clip.x, 0.0f);
        const float top = std::max(draw.clip.y, 0.0f);
        scissor[0] = static_cast<std::uint32_t>(left);
        scissor[1] = static_cast<std::uint32_t>(top);
        scissor[2] = static_cast<std::uint32_t>(std::clamp(draw.clip.z - left, 0.0f, static_cast<float>(size.width)));
        scissor[3] = static_cast<std::uint32_t>(std::clamp(draw.clip.w - top, 0.0f, static_cast<float>(size.height)));
    }
    else
    {
        scissor[0] = 0;
        scissor[1] = 0;
        scissor[2] = size.width;
        scissor[3] = size.height;
    }
}

[[nodiscard]] math::Extent2D fitted(math::Extent2D size, math::Extent2D limit) noexcept
{
    if (limit.width == 0 || limit.height == 0 || (size.width <= limit.width && size.height <= limit.height))
    {
        return size;
    }
    const float scale = std::min(static_cast<float>(limit.width) / static_cast<float>(size.width),
                                 static_cast<float>(limit.height) / static_cast<float>(size.height));
    return {std::max(1U, static_cast<std::uint32_t>(static_cast<float>(size.width) * scale)),
            std::max(1U, static_cast<std::uint32_t>(static_cast<float>(size.height) * scale))};
}

// Only one renderer exists at a time, as with Vulkan and WebGPU.
bool rendererAlive = false;

} // namespace

core::Result<std::unique_ptr<GlRenderer>> GlRenderer::create(const platform::Platform& platform, platform::Window& window,
                                                             const RendererConfig& config)
{
    static_cast<void>(platform);
    if (rendererAlive)
    {
        return core::makeError(core::ErrorCode::InvalidState, "a Renderer already exists");
    }
    std::unique_ptr<GlRenderer> renderer(new GlRenderer(window, config));
    if (core::Result<void> initialized = renderer->initialize(); !initialized)
    {
        return std::unexpected(initialized.error());
    }
    return renderer;
}

GlRenderer::GlRenderer(platform::Window& window, const RendererConfig& config) noexcept
    : m_window(window), m_config(config), m_presentMode(config.presentMode)
{
    rendererAlive = true;
}

GlRenderer::~GlRenderer()
{
    if (m_loaded)
    {
        destroySceneResources();
        m_textures.forEach([](TextureHandle, const GpuTexture& texture) { gl::DeleteTextures(1, &texture.texture); });
        m_meshes.forEach([](MeshHandle, const GpuMesh& mesh) {
            const GLuint meshBuffers[] = {mesh.vertices, mesh.indices, mesh.skin};
            gl::DeleteBuffers(3, meshBuffers);
            gl::DeleteVertexArrays(1, &mesh.array);
        });
        m_materials.forEach([](MaterialHandle, const GpuMaterial& material) { gl::DeleteBuffers(1, &material.uniform); });
        for (const Program* program : {&m_skyProgram, &m_spriteProgram, &m_uiProgram, &m_tonemapProgram})
        {
            gl::DeleteProgram(program->program);
        }
        const GLuint arrays[] = {m_emptyArray, m_spriteArray, m_uiArray};
        gl::DeleteVertexArrays(3, arrays);
        const GLuint buffers[] = {m_spriteBuffer, m_uiVertexBuffer, m_uiIndexBuffer, m_spriteFrameBuffer, m_tonemapBuffer, m_uiDrawBuffer};
        gl::DeleteBuffers(6, buffers);
        const GLuint samplers[] = {m_linearSampler, m_nearestSampler, m_clampSampler};
        gl::DeleteSamplers(3, samplers);
        const GLuint textures[] = {m_white, m_black};
        gl::DeleteTextures(2, textures);
        destroyTarget(m_scene);
    }
    m_window.destroyGlContext(m_context);
    rendererAlive = false;
}

core::Result<void> GlRenderer::initialize()
{
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    m_es = true;
#else
    m_es = m_config.openGlEs;
#endif
    core::Result<void*> context = m_window.createGlContext(m_es, m_config.validation);
    if (!context)
    {
        return std::unexpected(context.error());
    }
    m_context = *context;
    if (const char* const missing = gl::loadFunctions(&platform::Window::glFunction))
    {
        return core::makeError(core::ErrorCode::Graphics, "the OpenGL of this system lacks {}", missing);
    }
    m_loaded = true;
    if (m_config.validation && gl::DebugMessageCallback != nullptr)
    {
        gl::Enable(GL_DEBUG_OUTPUT);
        gl::Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        gl::DebugMessageCallback(&forwardDebugMessage, nullptr);
        if (gl::DebugMessageControl != nullptr)
        {
            gl::DebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        }
    }

    m_gpu.name = text(gl::GetString(GL_RENDERER));
    m_gpu.type = GpuType::Other;
    m_gpu.apiVersion = text(gl::GetString(GL_VERSION));
    m_gpu.driverName = text(gl::GetString(GL_VENDOR));
    m_gpu.driverVersion = text(gl::GetString(GL_SHADING_LANGUAGE_VERSION));
    GLint extensions = 0;
    gl::GetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    bool anisotropic = false;
    bool halfFloatTargets = !m_es;
    for (GLint index = 0; index < extensions; ++index)
    {
        const std::string extension = text(gl::GetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
        m_bptc = m_bptc || extension == "GL_ARB_texture_compression_bptc" || extension == "GL_EXT_texture_compression_bptc";
        m_astc = m_astc || extension == "GL_KHR_texture_compression_astc_ldr";
        anisotropic = anisotropic || extension == "GL_EXT_texture_filter_anisotropic" || extension == "GL_ARB_texture_filter_anisotropic";
        halfFloatTargets = halfFloatTargets || extension == "GL_EXT_color_buffer_half_float" || extension == "GL_EXT_color_buffer_float";
    }
    // OpenGL 4.2 reads BC7 without saying so.
    GLint major = 0;
    GLint minor = 0;
    gl::GetIntegerv(GL_MAJOR_VERSION, &major);
    gl::GetIntegerv(GL_MINOR_VERSION, &minor);
    m_bptc = m_bptc || (!m_es && (major > 4 || (major == 4 && minor >= 2)));
    if (anisotropic)
    {
        gl::GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m_maxAnisotropy);
    }
    gl::GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &m_uniformAlignment);
    m_uniformAlignment = std::max(m_uniformAlignment, 16);
    // The scene keeps its light in half floats where the system can draw into them; a phone without leaves the light at 8
    // bits, as the display shows it.
    m_sceneFormat = halfFloatTargets ? GL_RGBA16F : GL_RGBA8;
    m_halfFloatTargets = halfFloatTargets;
    GLint encoding = 0;
    gl::GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, m_es ? GL_BACK : GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,
                                            &encoding);
    m_windowSrgb = static_cast<GLenum>(encoding) == GL_SRGB;

    const auto link = [this](Program& program, std::string_view vertex, std::string_view fragment) -> core::Result<void> {
        core::Result<Program> linked = linkProgram(vertex, fragment);
        if (!linked)
        {
            return std::unexpected(linked.error());
        }
        program = *linked;
        return {};
    };
    if (core::Result<void> linked = link(m_skyProgram, "skyVertex", "skyFragment"); !linked)
    {
        return linked;
    }
    if (core::Result<void> linked = link(m_spriteProgram, "spriteVertex", "spriteFragment"); !linked)
    {
        return linked;
    }
    if (core::Result<void> linked = link(m_uiProgram, "uiVertex", "uiFragment"); !linked)
    {
        return linked;
    }
    if (core::Result<void> linked = link(m_tonemapProgram, "tonemapVertex", "tonemapFragment"); !linked)
    {
        return linked;
    }
    bindProgram(m_skyProgram, {{"SceneData", binding::scene}},
                {{"irradianceEnvironment", unit::irradiance}, {"specularEnvironment", unit::specular}, {"skyTexture", unit::first}});
    bindProgram(m_spriteProgram, {{"SpriteFrame", binding::spriteFrame}}, {{"spriteTexture", unit::first}});
    bindProgram(m_uiProgram, {{"DrawData", binding::uiDraw}}, {{"uiTexture", unit::first}});
    bindProgram(m_tonemapProgram, {{"TonemapData", binding::tonemap}},
                {{"sceneTexture", unit::first}, {"bloomTexture", unit::second}, {"colorTableTexture", unit::third}});

    gl::GenVertexArrays(1, &m_emptyArray);
    gl::GenVertexArrays(1, &m_spriteArray);
    gl::GenVertexArrays(1, &m_uiArray);
    const std::array buffers{&m_spriteBuffer, &m_uiVertexBuffer, &m_uiIndexBuffer, &m_spriteFrameBuffer, &m_tonemapBuffer, &m_uiDrawBuffer};
    for (GLuint* buffer : buffers)
    {
        gl::GenBuffers(1, buffer);
    }
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_spriteFrameBuffer);
    gl::BufferData(GL_UNIFORM_BUFFER, sizeof(SpriteFrameData), nullptr, GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_tonemapBuffer);
    gl::BufferData(GL_UNIFORM_BUFFER, sizeof(TonemapData), nullptr, GL_DYNAMIC_DRAW);

    // The interface reads positions, texture coordinates and colours from its vertices.
    gl::BindVertexArray(m_uiArray);
    gl::BindBuffer(GL_ARRAY_BUFFER, m_uiVertexBuffer);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_uiIndexBuffer);
    for (GLuint location = 0; location < 3; ++location)
    {
        gl::EnableVertexAttribArray(location);
    }
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GpuUiVertex), reinterpret_cast<const void*>(offsetof(GpuUiVertex, position)));
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GpuUiVertex), reinterpret_cast<const void*>(offsetof(GpuUiVertex, uv)));
    gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GpuUiVertex), reinterpret_cast<const void*>(offsetof(GpuUiVertex, color)));
    // Each sprite is an instance: its attributes move on once per sprite, from where its run starts.
    gl::BindVertexArray(m_spriteArray);
    for (GLuint location = 0; location < 10; ++location)
    {
        gl::EnableVertexAttribArray(location);
        gl::VertexAttribDivisor(location, 1);
    }
    gl::BindVertexArray(0);

    const auto sampler = [](GLuint& made, GLenum minify, GLenum magnify, GLenum wrap, float anisotropy) {
        gl::GenSamplers(1, &made);
        gl::SamplerParameteri(made, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minify));
        gl::SamplerParameteri(made, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magnify));
        gl::SamplerParameteri(made, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
        gl::SamplerParameteri(made, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
        gl::SamplerParameteri(made, GL_TEXTURE_WRAP_R, static_cast<GLint>(wrap));
        if (anisotropy > 1.0f)
        {
            gl::SamplerParameterf(made, GL_TEXTURE_MAX_ANISOTROPY, anisotropy);
        }
    };
    sampler(m_linearSampler, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, std::min(m_maxAnisotropy, 16.0f));
    sampler(m_nearestSampler, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST, GL_REPEAT, 1.0f);
    sampler(m_clampSampler, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, 1.0f);

    const auto pixel = [](GLuint& made, std::array<std::uint8_t, 4> color) {
        gl::GenTextures(1, &made);
        gl::BindTexture(GL_TEXTURE_2D, made);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
    };
    pixel(m_white, {255, 255, 255, 255});
    pixel(m_black, {0, 0, 0, 255});
    // A normal map that leaves the normal as it is.
    pixel(m_flatNormal, {128, 128, 255, 255});
    gl::BindTexture(GL_TEXTURE_2D, 0);
    if (core::Result<void> scene = createSceneResources(); !scene)
    {
        return scene;
    }
    applyPresentMode();
    if (const GLenum error = gl::GetError(); error != GL_NO_ERROR)
    {
        return core::makeError(core::ErrorCode::Graphics, "OpenGL failed while the renderer started: error {:#x}", error);
    }
    // OpenGL ES says its name in its version; OpenGL does not.
    DEVEX_LOG_INFO("Drawing with {}{} on {}", m_gpu.apiVersion.starts_with("OpenGL") ? "" : "OpenGL ", m_gpu.apiVersion, m_gpu.name);
    return {};
}

core::Result<GlRenderer::Program> GlRenderer::linkProgram(std::string_view vertex, std::string_view fragment) const
{
    core::Result<GLuint> vertexShader = compileShader(GL_VERTEX_SHADER, vertex, shaderSource(vertex, m_es));
    if (!vertexShader)
    {
        return std::unexpected(vertexShader.error());
    }
    core::Result<GLuint> fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragment, shaderSource(fragment, m_es));
    if (!fragmentShader)
    {
        gl::DeleteShader(*vertexShader);
        return std::unexpected(fragmentShader.error());
    }
    const GLuint program = gl::CreateProgram();
    gl::AttachShader(program, *vertexShader);
    gl::AttachShader(program, *fragmentShader);
    gl::LinkProgram(program);
    gl::DeleteShader(*vertexShader);
    gl::DeleteShader(*fragmentShader);
    GLint linked = 0;
    gl::GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == 0)
    {
        GLint length = 0;
        gl::GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        gl::GetProgramInfoLog(program, length, nullptr, log.data());
        gl::DeleteProgram(program);
        return core::makeError(core::ErrorCode::Graphics, "OpenGL cannot link {} with {}: {}", vertex, fragment, log.c_str());
    }
    return Program{.program = program};
}

void GlRenderer::bindProgram(const Program& program, std::initializer_list<std::pair<const char*, GLuint>> blocks,
                             std::initializer_list<std::pair<const char*, GLint>> samplers) const
{
    for (const auto& [name, binding] : blocks)
    {
        // spirv-cross names a block after its type, with what std140 adds to it.
        GLuint block = gl::GetUniformBlockIndex(program.program, std::format("{}_std140", name).c_str());
        if (block == 0xFFFFFFFFU)
        {
            block = gl::GetUniformBlockIndex(program.program, name);
        }
        if (block != 0xFFFFFFFFU)
        {
            gl::UniformBlockBinding(program.program, block, binding);
        }
    }
    gl::UseProgram(program.program);
    for (const auto& [name, textureUnit] : samplers)
    {
        if (const GLint location = gl::GetUniformLocation(program.program, name); location >= 0)
        {
            gl::Uniform1i(location, textureUnit);
        }
    }
    gl::UseProgram(0);
}

void GlRenderer::growBuffer(GLuint buffer, GLenum target, std::size_t& capacity, std::size_t size) const
{
    gl::BindBuffer(target, buffer);
    if (capacity < size)
    {
        capacity = std::max<std::size_t>(std::bit_ceil(std::max<std::size_t>(size, 256)), capacity);
        gl::BufferData(target, static_cast<GLsizeiptr>(capacity), nullptr, GL_STREAM_DRAW);
    }
}

core::Result<void> GlRenderer::ensureTarget(Target& target, math::Extent2D size, GLenum internalFormat, GLenum format, GLenum type) const
{
    if (target.framebuffer != 0 && target.size == size)
    {
        return {};
    }
    destroyTarget(target);
    gl::GenTextures(1, &target.texture);
    gl::BindTexture(GL_TEXTURE_2D, target.texture);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), static_cast<GLsizei>(size.width),
                   static_cast<GLsizei>(size.height), 0, format, type, nullptr);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    gl::GenFramebuffers(1, &target.framebuffer);
    gl::BindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    target.size = size;
    if (const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE)
    {
        destroyTarget(target);
        return core::makeError(core::ErrorCode::Graphics, "OpenGL cannot draw into an image of format {:#x}: {:#x}", internalFormat, status);
    }
    return {};
}

void GlRenderer::destroyTarget(Target& target) const noexcept
{
    if (target.framebuffer != 0)
    {
        gl::DeleteFramebuffers(1, &target.framebuffer);
    }
    if (target.texture != 0)
    {
        gl::DeleteTextures(1, &target.texture);
    }
    target = {};
}

const GlRenderer::GpuTexture* GlRenderer::textureAtSlot(std::uint32_t slot) const noexcept
{
    return slot < m_slotHandles.size() ? m_textures.find(m_slotHandles[slot]) : nullptr;
}

void GlRenderer::bindTexture(GLuint unit, GLuint texture, GLuint sampler) const
{
    gl::ActiveTexture(GL_TEXTURE0 + unit);
    gl::BindTexture(GL_TEXTURE_2D, texture);
    gl::BindSampler(unit, sampler);
}

void GlRenderer::applyPresentMode()
{
    // Immediate never waits; mailbox, which OpenGL lacks, does not either; FIFO waits for the display, and relaxed only
    // when on time, where the system can.
    const int interval = m_presentMode == PresentMode::Immediate || m_presentMode == PresentMode::Mailbox ? 0
                         : m_presentMode == PresentMode::FifoRelaxed                                     ? -1
                                                                                                         : 1;
    if (!m_window.setGlSwapInterval(interval) && interval == -1)
    {
        static_cast<void>(m_window.setGlSwapInterval(1));
    }
    m_presentModeApplied = true;
}

const GpuInfo& GlRenderer::gpu() const noexcept
{
    return m_gpu;
}

PresentMode GlRenderer::presentMode() const noexcept
{
    return m_presentMode;
}

void GlRenderer::setPresentMode(PresentMode mode)
{
    if (mode != m_presentMode)
    {
        m_presentMode = mode;
        m_presentModeApplied = false;
    }
}

core::Result<std::uint32_t> GlRenderer::addWindowTarget(platform::Window& window)
{
    static_cast<void>(window);
    return core::makeError(core::ErrorCode::Unsupported, "OpenGL draws into one window");
}

void GlRenderer::removeWindowTarget(std::uint32_t target)
{
    static_cast<void>(target);
}

core::Result<std::string> GlRenderer::describeGraphics() const
{
    return std::format("OpenGL{} {} on {} ({}), GLSL {}", m_es ? " ES" : "", m_gpu.apiVersion, m_gpu.name, m_gpu.driverName,
                       m_gpu.driverVersion);
}

core::Result<MeshHandle> GlRenderer::createMesh(const asset::MeshData& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a mesh needs vertices and indices");
    }
    GpuMesh gpu;
    const auto upload = [](GLuint& buffer, GLenum target, const void* data, std::size_t size) {
        gl::GenBuffers(1, &buffer);
        gl::BindBuffer(target, buffer);
        gl::BufferData(target, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    };
    gl::GenVertexArrays(1, &gpu.array);
    gl::BindVertexArray(gpu.array);
    upload(gpu.vertices, GL_ARRAY_BUFFER, mesh.vertices.data(), mesh.vertices.size() * sizeof(asset::Vertex));
    const auto at = [](std::size_t offset) { return reinterpret_cast<const void*>(offset); };
    constexpr GLsizei stride = sizeof(asset::Vertex);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, at(offsetof(asset::Vertex, position)));
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, at(offsetof(asset::Vertex, normal)));
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, at(offsetof(asset::Vertex, uv)));
    gl::VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(asset::Vertex, tangent)));
    for (GLuint location = 0; location < 4; ++location)
    {
        gl::EnableVertexAttribArray(location);
    }
    if (!mesh.skin.empty())
    {
        upload(gpu.skin, GL_ARRAY_BUFFER, mesh.skin.data(), mesh.skin.size() * sizeof(asset::VertexSkin));
        constexpr GLsizei skinStride = sizeof(asset::VertexSkin);
        gl::VertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, skinStride, at(offsetof(asset::VertexSkin, joints)));
        gl::VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, skinStride, at(offsetof(asset::VertexSkin, weights)));
        gl::EnableVertexAttribArray(4);
        gl::EnableVertexAttribArray(5);
    }
    // The attributes of its instances, which each batch points at where its own start: once an instance.
    for (GLuint location = 6; location < 13; ++location)
    {
        gl::EnableVertexAttribArray(location);
        gl::VertexAttribDivisor(location, 1);
    }
    upload(gpu.indices, GL_ELEMENT_ARRAY_BUFFER, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
    gl::BindVertexArray(0);
    gpu.submeshes = mesh.submeshes;
    if (gpu.submeshes.empty())
    {
        gpu.submeshes.push_back({.firstIndex = 0, .indexCount = static_cast<std::uint32_t>(mesh.indices.size())});
    }
    gpu.bounds = mesh.bounds;
    return m_meshes.insert(std::move(gpu));
}

bool GlRenderer::isReady(MeshHandle mesh) const noexcept
{
    return m_meshes.contains(mesh);
}

bool GlRenderer::isReady(TextureHandle texture) const noexcept
{
    return m_textures.contains(texture);
}

void GlRenderer::destroyMesh(MeshHandle mesh)
{
    if (const std::optional<GpuMesh> removed = m_meshes.remove(mesh))
    {
        const GLuint buffers[] = {removed->vertices, removed->indices, removed->skin};
        gl::DeleteBuffers(3, buffers);
        gl::DeleteVertexArrays(1, &removed->array);
    }
}

std::uint32_t GlRenderer::submeshCount(MeshHandle mesh) const noexcept
{
    const GpuMesh* const record = m_meshes.find(mesh);
    return record != nullptr ? static_cast<std::uint32_t>(record->submeshes.size()) : 0;
}

core::Result<TextureHandle> GlRenderer::createTexture(const asset::TextureData& texture)
{
    if (core::Result<void> valid = asset::validate(texture); !valid)
    {
        return std::unexpected(valid.error());
    }
    FormatLayout layout = layoutOf(texture.format);
    const bool bptc = texture.format == asset::TextureFormat::Bc7Unorm || texture.format == asset::TextureFormat::Bc7Srgb;
    const bool astc = texture.format == asset::TextureFormat::Astc4x4Unorm || texture.format == asset::TextureFormat::Astc4x4Srgb;
    if (astc && !m_astc)
    {
        return core::makeError(core::ErrorCode::Unsupported, "this OpenGL cannot read {} textures", asset::toString(texture.format));
    }
    // The blocks of BC5 OpenGL 3.0 reads, and BC7 the systems that say so; OpenGL ES reads no BC. The others get the pixels,
    // as WebGPU in the browser of a phone.
    const bool decode = (bptc && !m_bptc) || (m_es && texture.format == asset::TextureFormat::Bc5Unorm);
    if (decode)
    {
        layout = {texture.format == asset::TextureFormat::Bc7Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
    }
    GpuTexture gpu;
    gpu.format = texture.format;
    gpu.size = {texture.mips.front().width, texture.mips.front().height};
    gpu.bindable = texture.format != asset::TextureFormat::R32Float;
    gpu.nearest = texture.filter == asset::TextureFilter::Nearest;
    gpu.mips = static_cast<std::uint32_t>(texture.mips.size());
    ++m_textureRevision;
    gl::GenTextures(1, &gpu.texture);
    gl::BindTexture(GL_TEXTURE_2D, gpu.texture);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.mips.size() - 1));
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t level = 0; level < texture.mips.size(); ++level)
    {
        const asset::TextureMip& mip = texture.mips[level];
        if (decode)
        {
            const std::vector<std::byte> pixels = decodeBlocks(texture.format, mip.bytes, mip.width, mip.height);
            gl::TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), static_cast<GLint>(layout.internalFormat),
                           static_cast<GLsizei>(mip.width), static_cast<GLsizei>(mip.height), 0, layout.format, layout.type, pixels.data());
        }
        else if (layout.compressed)
        {
            gl::CompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), layout.internalFormat, static_cast<GLsizei>(mip.width),
                                     static_cast<GLsizei>(mip.height), 0, static_cast<GLsizei>(mip.bytes.size()), mip.bytes.data());
        }
        else
        {
            gl::TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), static_cast<GLint>(layout.internalFormat),
                           static_cast<GLsizei>(mip.width), static_cast<GLsizei>(mip.height), 0, layout.format, layout.type,
                           mip.bytes.data());
        }
    }
    gl::BindTexture(GL_TEXTURE_2D, 0);
    const TextureHandle handle = m_textures.insert(gpu);
    if (m_slotHandles.size() <= handle.index)
    {
        m_slotHandles.resize(handle.index + 1);
    }
    m_slotHandles[handle.index] = handle;
    return handle;
}

void GlRenderer::destroyTexture(TextureHandle texture)
{
    if (const std::optional<GpuTexture> removed = m_textures.remove(texture))
    {
        gl::DeleteTextures(1, &removed->texture);
        ++m_textureRevision;
    }
}

core::Result<void> GlRenderer::updateTexture(TextureHandle texture, std::uint32_t top, std::uint32_t height, std::span<const std::byte> rows)
{
    const GpuTexture* const gpu = m_textures.find(texture);
    if (gpu == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "no such texture");
    }
    const FormatLayout layout = layoutOf(gpu->format);
    if (layout.compressed)
    {
        return core::makeError(core::ErrorCode::Unsupported, "rows of a texture of blocks cannot be replaced");
    }
    if (top + height > gpu->size.height || rows.size() < std::size_t{gpu->size.width} * height * layout.bytesPerPixel)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the rows go past the texture");
    }
    ++m_textureRevision;
    gl::BindTexture(GL_TEXTURE_2D, gpu->texture);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(top), static_cast<GLsizei>(gpu->size.width), static_cast<GLsizei>(height),
                      layout.format, layout.type, rows.data());
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return {};
}

core::Result<ReflectionProbeHandle> GlRenderer::createReflectionProbe(const asset::ReflectionProbeData& probe)
{
    static_cast<void>(probe);
    return m_probes.insert({});
}

void GlRenderer::destroyReflectionProbe(ReflectionProbeHandle probe)
{
    static_cast<void>(m_probes.remove(probe));
}

core::Result<IrradianceVolumeHandle> GlRenderer::createIrradianceVolume(const asset::IrradianceVolumeData& volume)
{
    static_cast<void>(volume);
    return m_volumes.insert({});
}

void GlRenderer::destroyIrradianceVolume(IrradianceVolumeHandle volume)
{
    static_cast<void>(m_volumes.remove(volume));
}

MaterialHandle GlRenderer::createMaterial(const MaterialDesc& material)
{
    return m_materials.insert({.description = material});
}

void GlRenderer::updateMaterial(MaterialHandle handle, const MaterialDesc& material)
{
    if (GpuMaterial* const stored = m_materials.find(handle))
    {
        stored->description = material;
        stored->written = false;
    }
}

void GlRenderer::destroyMaterial(MaterialHandle material)
{
    if (const std::optional<GpuMaterial> removed = m_materials.remove(material))
    {
        gl::DeleteBuffers(1, &removed->uniform);
    }
}

core::Result<ShaderHandle> GlRenderer::createShader(const asset::ShaderData& shader)
{
    return m_shaders.insert({.kind = shader.kind});
}

core::Result<void> GlRenderer::updateShader(ShaderHandle handle, const asset::ShaderData& shader)
{
    GpuShader* const stored = m_shaders.find(handle);
    if (stored == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "no such shader");
    }
    stored->kind = shader.kind;
    return {};
}

void GlRenderer::destroyShader(ShaderHandle shader)
{
    static_cast<void>(m_shaders.remove(shader));
}

RenderWorld& GlRenderer::beginFrame() noexcept
{
    m_world.reset();
    return m_world;
}

void GlRenderer::resetSceneHistory() noexcept
{
    m_snapExposure = true;
}

core::Result<void> GlRenderer::endFrame()
{
    const math::Extent2D size = m_window.pixelSize();
    if (size.width == 0 || size.height == 0)
    {
        return {};
    }
    m_windowSize = size;
    if (!m_presentModeApplied)
    {
        applyPresentMode();
    }
    m_world.camera = blendPostVolumes(m_world.camera, m_world.postVolumes);
    m_drawCalls = 0;
    m_time = std::fmod(m_time + 1.0f, 1024.0f);
    m_shaderTime = static_cast<float>(
        std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count(), 3600.0));
    updateExposure();
    m_exposure = exposureFromEv100(m_ev100);

    // The scene in fewer pixels, stretched over the window by the tonemapping; the interface keeps every pixel.
    const math::Extent2D sceneSize = scaledExtent(size, m_world.renderScale);
    if (m_scene.size != sceneSize || m_scene.framebuffer == 0)
    {
        if (core::Result<void> target = ensureTarget(m_scene, sceneSize, m_sceneFormat, GL_RGBA,
                                                     m_sceneFormat == GL_RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE);
            !target)
        {
            if (m_sceneFormat == GL_RGBA8)
            {
                return target;
            }
            m_sceneFormat = GL_RGBA8;
            if (core::Result<void> plain = ensureTarget(m_scene, sceneSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE); !plain)
            {
                return plain;
            }
        }
        // Its depth, reversed as Vulkan keeps it: 0 is infinitely far.
        if (m_sceneDepth != 0)
        {
            gl::DeleteTextures(1, &m_sceneDepth);
        }
        gl::GenTextures(1, &m_sceneDepth);
        gl::BindTexture(GL_TEXTURE_2D, m_sceneDepth);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl::TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT32F), static_cast<GLsizei>(sceneSize.width),
                       static_cast<GLsizei>(sceneSize.height), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        gl::BindTexture(GL_TEXTURE_2D, 0);
        gl::BindFramebuffer(GL_FRAMEBUFFER, m_scene.framebuffer);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_sceneDepth, 0);
        if (const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE)
        {
            return core::makeError(core::ErrorCode::Graphics, "OpenGL cannot draw the scene with its depth: {:#x}", status);
        }
    }
    // The clip space of OpenGL runs from -1 to 1 in depth: the reversed depth of Vulkan, from 0 to 1, is stretched over it,
    // which keeps what the depth buffer holds.
    math::Mat4 clipCorrection{1.0f};
    clipCorrection[2][2] = 2.0f;
    clipCorrection[3][2] = -1.0f;
    const ViewMatrices matrices = prepareView(m_world.camera, sceneSize, math::Vec2{0.0f}, clipCorrection);
    // The frustum of the depth from 0 to 1, which the culling expects.
    const Frustum frustum = frustumOf(math::inverse(clipCorrection) * matrices.unjitteredViewProjection);
    prepareMeshes(matrices, frustum, sceneSize);

    // The sprites in drawing order, by runs that read one texture.
    const SpriteResources resources{
        .textureSlot = [this](TextureHandle handle) {
            const GpuTexture* const texture = m_textures.find(handle);
            return texture != nullptr && texture->bindable ? handle.index : noParticleTexture;
        },
        .materialIndex = [](MaterialHandle) { return 0U; },
        .canvasShader = [](MaterialHandle) { return ShaderHandle{}; },
        .sortDistance = [this, camera = matrices.cameraPosition](math::Vec3 point) { return sortDistance(point, camera); },
    };
    gatherSprites(m_world, frustum, resources, m_spriteFrame);
    if (!m_spriteFrame.sprites.empty())
    {
        growBuffer(m_spriteBuffer, GL_ARRAY_BUFFER, m_spriteCapacity, m_spriteFrame.sprites.size() * sizeof(GpuSprite));
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_spriteFrame.sprites.size() * sizeof(GpuSprite)),
                          m_spriteFrame.sprites.data());
    }
    const SpriteFrameData frame{.viewProjection = matrices.viewProjection, .canvasModulate = math::Vec4(m_world.canvasModulate, 1.0f)};
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_spriteFrameBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(frame), &frame);
    prepareParticles(matrices);
    prepareTerrains(matrices, frustum);
    orderTransparent(matrices);

    // The shadows, then the scene: the environment, the opaque meshes, then what is blended, meshes and sprites, in their
    // order, testing the depth of the opaque surfaces without writing it.
    drawShadows();
    gl::BindFramebuffer(GL_FRAMEBUFFER, m_scene.framebuffer);
    gl::Viewport(0, 0, static_cast<GLsizei>(sceneSize.width), static_cast<GLsizei>(sceneSize.height));
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_BLEND);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::DepthMask(GL_TRUE);
    if (gl::ClearDepthf != nullptr)
    {
        gl::ClearDepthf(0.0f);
    }
    else
    {
        gl::ClearDepth(0.0);
    }
    gl::Clear(GL_DEPTH_BUFFER_BIT);
    gl::Disable(GL_DEPTH_TEST);
    gl::DepthMask(GL_FALSE);
    drawSky();
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_GREATER);
    gl::DepthMask(GL_TRUE);
    drawMeshes(false);
    drawTerrains(std::nullopt);
    drawGrass();
    gl::DepthMask(GL_FALSE);
    copySceneDepth(sceneSize);
    drawTransparent();
    gl::Disable(GL_DEPTH_TEST);
    gl::DepthMask(GL_TRUE);
    gl::Disable(GL_CULL_FACE);
    measureLuminance();

    // The interface, in batches of one texture and one shape, cut to their rectangles.
    packUi(m_world, false, m_ui);
    m_uiRuns.clear();
    m_uiDraws.clear();
    const std::size_t stride = (sizeof(UiDrawData) + static_cast<std::size_t>(m_uniformAlignment) - 1) /
                               static_cast<std::size_t>(m_uniformAlignment) * static_cast<std::size_t>(m_uniformAlignment);
    for (const UiDraw& draw : m_world.uiDraws)
    {
        if (draw.source != UiSource::Texture || draw.indexCount == 0)
        {
            // The images of the tools are not drawn by OpenGL.
            continue;
        }
        const GpuTexture* const texture = m_textures.find(draw.texture);
        UiRun run{.firstIndex = draw.firstIndex,
                  .indexCount = draw.indexCount,
                  .texture = texture != nullptr && texture->bindable ? texture->texture : m_white,
                  .nearest = texture != nullptr && texture->nearest};
        scissorOf(draw, size, run.scissor);
        const UiDrawData data{.rect = draw.rect,
                              .radius = draw.radius,
                              .sharpness = draw.sharpness,
                              .kind = static_cast<std::uint32_t>(draw.kind)};
        const std::size_t at = m_uiDraws.size();
        m_uiDraws.resize(at + stride);
        std::memcpy(m_uiDraws.data() + at, &data, sizeof(data));
        m_uiRuns.push_back(run);
    }
    if (!m_uiRuns.empty() && !m_ui.indices.empty())
    {
        gl::BindVertexArray(m_uiArray);
        growBuffer(m_uiVertexBuffer, GL_ARRAY_BUFFER, m_uiVertexCapacity, m_ui.vertices.size() * sizeof(GpuUiVertex));
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_ui.vertices.size() * sizeof(GpuUiVertex)), m_ui.vertices.data());
        growBuffer(m_uiIndexBuffer, GL_ELEMENT_ARRAY_BUFFER, m_uiIndexCapacity, m_ui.indices.size() * sizeof(std::uint32_t));
        gl::BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_ui.indices.size() * sizeof(std::uint32_t)),
                          m_ui.indices.data());
        gl::BindVertexArray(0);
    }
    else
    {
        m_uiRuns.clear();
    }

    // The captures asked for: the scene at a small size, or the whole window with its interface.
    for (const CaptureRequest& request : std::exchange(m_captureRequests, {}))
    {
        const math::Extent2D captured = request.window ? size : fitted(size, request.maxSize);
        // In sRGB, as the window of Vulkan keeps it.
        Target target;
        if (core::Result<void> made = ensureTarget(target, captured, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE); !made)
        {
            return made;
        }
        drawImage(captured, request.window, true);
        m_captured.push_back(readPixels(request.id, captured));
        destroyTarget(target);
    }

    // The window: the scene tonemapped, then the interface over it.
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    drawImage(size, true, m_windowSrgb);
    m_window.swapGl();
    if (const GLenum error = gl::GetError(); error != GL_NO_ERROR)
    {
        DEVEX_LOG_ERROR("OpenGL failed while drawing a frame: error {:#x}", error);
    }
    return {};
}

void GlRenderer::drawSky()
{
    const GpuTexture* const sky = m_textures.find(m_world.environment.sky);
    gl::UseProgram(m_skyProgram.program);
    bindSceneTextures();
    bindTexture(unit::first, sky != nullptr && sky->bindable ? sky->texture : m_white, m_skySampler);
    gl::BindVertexArray(m_emptyArray);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    ++m_drawCalls;
}

void GlRenderer::drawSprites(std::uint32_t firstRun, std::uint32_t runCount)
{
    if (runCount == 0)
    {
        return;
    }
    // Premultiplied: what is drawn adds to what it lets through. Behind what the meshes drew nearer, without hiding the
    // others.
    gl::Enable(GL_BLEND);
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl::Disable(GL_CULL_FACE);
    gl::DepthFunc(GL_GEQUAL);
    gl::UseProgram(m_spriteProgram.program);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::spriteFrame, m_spriteFrameBuffer, 0, sizeof(SpriteFrameData));
    gl::BindVertexArray(m_spriteArray);
    gl::BindBuffer(GL_ARRAY_BUFFER, m_spriteBuffer);
    constexpr GLsizei stride = sizeof(GpuSprite);
    for (std::uint32_t index = firstRun; index < firstRun + runCount; ++index)
    {
        const SpriteRun& run = m_spriteRuns[index];
        // OpenGL ES 3.0 has no first instance: the attributes start at the run.
        const std::size_t base = std::size_t{run.first} * sizeof(GpuSprite);
        const auto at = [base](std::size_t offset) { return reinterpret_cast<const void*>(base + offset); };
        for (GLuint column = 0; column < 4; ++column)
        {
            gl::VertexAttribPointer(column, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(GpuSprite, world) + column * sizeof(math::Vec4)));
        }
        gl::VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(GpuSprite, uvRect)));
        gl::VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(GpuSprite, color)));
        gl::VertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(GpuSprite, border)));
        gl::VertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(GpuSprite, size)));
        gl::VertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, stride, at(offsetof(GpuSprite, naturalSize)));
        gl::VertexAttribIPointer(9, 2, GL_UNSIGNED_INT, stride, at(offsetof(GpuSprite, texture)));
        const GpuTexture* const texture = textureAtSlot(run.texture);
        bindTexture(unit::first, texture != nullptr ? texture->texture : m_white,
                    texture != nullptr && texture->nearest ? m_nearestSampler : m_linearSampler);
        gl::DrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(run.count));
        ++m_drawCalls;
    }
    gl::BindVertexArray(0);
    gl::Disable(GL_BLEND);
}

void GlRenderer::drawImage(math::Extent2D size, bool interface, bool srgb)
{
    gl::Viewport(0, 0, static_cast<GLsizei>(size.width), static_cast<GLsizei>(size.height));
    // OpenGL ES encodes into what keeps sRGB by itself; OpenGL when told to.
    if (!m_es)
    {
        srgb ? gl::Enable(GL_FRAMEBUFFER_SRGB) : gl::Disable(GL_FRAMEBUFFER_SRGB);
    }
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    const RenderCamera& camera = m_world.camera;
    const math::Vec3 balance = whiteBalanceFactors(camera.temperature, camera.tint);
    const TonemapData settings{
        .tonemapper = static_cast<std::uint32_t>(camera.tonemapper),
        .vignette = std::clamp(camera.vignette, 0.0f, 1.0f),
        .grain = std::max(camera.grain, 0.0f),
        .time = m_time,
        .targetWidth = static_cast<float>(size.width),
        .targetHeight = static_cast<float>(size.height),
        .encode = srgb ? 0U : 1U,
        .saturation = std::max(camera.saturation, 0.0f),
        .contrast = std::max(camera.contrast, 0.01f),
        .chromatic = std::max(camera.chromaticAberration, 0.0f),
        .filter = math::Vec4(math::max(camera.colorFilter, math::Vec3{0.0f}), 1.0f),
        .balance = math::Vec4(balance, 1.0f),
    };
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_tonemapBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(settings), &settings);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::tonemap, m_tonemapBuffer, 0, sizeof(settings));
    gl::UseProgram(m_tonemapProgram.program);
    bindTexture(unit::first, m_scene.texture, m_clampSampler);
    bindTexture(unit::second, m_black, m_clampSampler);
    bindTexture(unit::third, m_black, m_clampSampler);
    gl::BindVertexArray(m_emptyArray);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    ++m_drawCalls;

    if (!interface || m_uiRuns.empty())
    {
        if (!m_es)
        {
            gl::Disable(GL_FRAMEBUFFER_SRGB);
        }
        return;
    }
    // The interface blends straight alpha over the image.
    gl::Enable(GL_BLEND);
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl::Enable(GL_SCISSOR_TEST);
    gl::UseProgram(m_uiProgram.program);
    gl::BindVertexArray(m_uiArray);
    const std::size_t stride = m_uiDraws.size() / m_uiRuns.size();
    // Each draw tells the shader how pixels map to the clip space of this image, and whether it encodes the colours.
    const math::Vec2 inverseViewport{2.0f / static_cast<float>(size.width), 2.0f / static_cast<float>(size.height)};
    const std::uint32_t displaySpace = srgb ? 0U : 1U;
    for (std::size_t run = 0; run < m_uiRuns.size(); ++run)
    {
        std::byte* const data = m_uiDraws.data() + run * stride;
        std::memcpy(data + offsetof(UiDrawData, inverseViewport), &inverseViewport, sizeof(inverseViewport));
        std::memcpy(data + offsetof(UiDrawData, displaySpace), &displaySpace, sizeof(displaySpace));
    }
    growBuffer(m_uiDrawBuffer, GL_UNIFORM_BUFFER, m_uiDrawCapacity, m_uiDraws.size());
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(m_uiDraws.size()), m_uiDraws.data());
    for (std::size_t index = 0; index < m_uiRuns.size(); ++index)
    {
        const UiRun& drawn = m_uiRuns[index];
        const std::uint32_t right = std::min(drawn.scissor[0] + drawn.scissor[2], size.width);
        const std::uint32_t bottom = std::min(drawn.scissor[1] + drawn.scissor[3], size.height);
        if (drawn.scissor[0] >= right || drawn.scissor[1] >= bottom)
        {
            continue;
        }
        // The scissor of OpenGL counts from the bottom row.
        gl::Scissor(static_cast<GLint>(drawn.scissor[0]), static_cast<GLint>(size.height - bottom),
                    static_cast<GLsizei>(right - drawn.scissor[0]), static_cast<GLsizei>(bottom - drawn.scissor[1]));
        gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::uiDraw, m_uiDrawBuffer, static_cast<GLintptr>(index * stride), sizeof(UiDrawData));
        bindTexture(unit::first, drawn.texture, drawn.nearest ? m_nearestSampler : m_linearSampler);
        gl::DrawElements(GL_TRIANGLES, static_cast<GLsizei>(drawn.indexCount), GL_UNSIGNED_INT,
                         reinterpret_cast<const void*>(std::size_t{drawn.firstIndex} * sizeof(std::uint32_t)));
        ++m_drawCalls;
    }
    gl::BindVertexArray(0);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_BLEND);
    if (!m_es)
    {
        gl::Disable(GL_FRAMEBUFFER_SRGB);
    }
}

CapturedImage GlRenderer::readPixels(std::uint64_t request, math::Extent2D size) const
{
    CapturedImage image{.request = request, .width = size.width, .height = size.height};
    image.rgba.resize(std::size_t{size.width} * size.height * 4);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, static_cast<GLsizei>(size.width), static_cast<GLsizei>(size.height), GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    // OpenGL reads from the bottom row up.
    const std::size_t row = std::size_t{size.width} * 4;
    for (std::uint32_t y = 0; y < size.height / 2; ++y)
    {
        std::swap_ranges(image.rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                         image.rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                         image.rgba.begin() + static_cast<std::ptrdiff_t>((size.height - 1 - y) * row));
    }
    // Opaque, as the captures of Vulkan are.
    for (std::size_t alpha = 3; alpha < image.rgba.size(); alpha += 4)
    {
        image.rgba[alpha] = 255;
    }
    return image;
}

std::vector<PickResult> GlRenderer::takePickResults()
{
    // Picking comes with the tools, after the 3D.
    return {};
}

std::uint64_t GlRenderer::requestCapture(std::uint32_t maxWidth, std::uint32_t maxHeight, std::optional<std::uint32_t> view)
{
    static_cast<void>(view);
    const std::uint64_t id = m_nextCapture++;
    m_captureRequests.push_back({.id = id, .maxSize = {maxWidth, maxHeight}});
    return id;
}

std::uint64_t GlRenderer::requestWindowCapture()
{
    const std::uint64_t id = m_nextCapture++;
    m_captureRequests.push_back({.id = id, .window = true});
    return id;
}

std::vector<CapturedImage> GlRenderer::takeCaptures()
{
    return std::exchange(m_captured, {});
}

RendererStats GlRenderer::stats() const noexcept
{
    RendererStats stats;
    stats.drawCalls = m_drawCalls;
    stats.meshCount = m_meshes.size();
    stats.textureCount = m_textures.size();
    stats.materialCount = m_materials.size();
    stats.shaderCount = m_shaders.size();
    stats.ev100 = m_ev100;
    stats.lightCount = static_cast<std::uint32_t>(m_lights.size());
    stats.swapchainExtent = m_windowSize;
    stats.sceneExtent = m_scene.size;
    return stats;
}

} // namespace devex::render::opengl
