// What follows the light in the OpenGL renderer, as WebGPU draws it: the temporal antialiasing, the depth of field and
// the motion blur of the lens, and the halo of the bloom, each a triangle over an image of half floats. A system that
// cannot draw half floats keeps its scene at 8 bits, as the display shows it, and draws none of them.
#include "GlRenderer.hpp"

#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <tuple>

namespace devex::render::opengl {
namespace {

using namespace gl;

// The uniform blocks of shaders/gl_taa.slang, gl_bloom.slang and gl_lens.slang, as std140 lays them out.
struct TaaData
{
    float blend = 0.9f;
    float historyValid = 0.0f;
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
    math::Mat4 inverseViewProjection{1.0f};
    math::Mat4 previousViewProjection{1.0f};
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Mat4 previousSkyViewProjection{1.0f};
};
struct BloomData
{
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
    float strength = 1.0f;
    float padding = 0.0f;
};
struct LensData
{
    float targetWidth = 0.0f;
    float targetHeight = 0.0f;
    float imageWidth = 0.0f;
    float imageHeight = 0.0f;
    float aperture = 0.0f;
    float focalLength = 0.0f;
    float focusDistance = 0.0f;
    float amount = 0.0f;
    float maxRadius = 0.0f;
    std::uint32_t autoFocus = 0;
    float shutter = 0.0f;
    float tileSize = 8.0f;
    std::uint32_t fromDepthOfField = 0;
    std::uint32_t frame = 0;
    float padding[2]{};
    math::Mat4 inverseViewProjection{1.0f};
    math::Mat4 previousViewProjection{1.0f};
    math::Mat4 view{1.0f};
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Mat4 previousSkyViewProjection{1.0f};
};
static_assert(sizeof(TaaData) == 272 && sizeof(BloomData) == 16 && sizeof(LensData) == 384);

// What a still pixel keeps of its history, as Vulkan keeps it.
constexpr float taaBlend = 0.9f;

// The images of the chain of the lens, as lens_filter.slang numbers them.
enum LensImage : std::size_t
{
    lensCircleOfConfusion,
    lensHalved,
    lensBokeh,
    lensFiltered,
    lensFocused,
    lensTiles,
    lensNeighbours,
};

// The texture units of the post passes: the image read, the depth of the scene, then the chain of the lens.
constexpr GLint postSource = unit::first;
constexpr GLint postDepth = unit::second;
constexpr GLint postHistory = unit::third;
constexpr GLint postChain = unit::fourth;

} // namespace

core::Result<void> GlRenderer::createPostResources()
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
    if (core::Result<void> linked = link(m_taaProgram, "taaVertex", "taaFragment"); !linked)
    {
        return linked;
    }
    bindProgram(m_taaProgram, {{"TaaData", binding::post}},
                {{"sceneColor", postSource}, {"sceneDepth", postDepth}, {"historyTexture", postHistory}});
    constexpr std::array<std::string_view, 3> bloomFragments{"thresholdFragment", "downsampleFragment", "upsampleFragment"};
    for (std::size_t step = 0; step < bloomFragments.size(); ++step)
    {
        if (core::Result<void> linked = link(m_bloomPrograms[step], "bloomVertex", bloomFragments[step]); !linked)
        {
            return linked;
        }
        bindProgram(m_bloomPrograms[step], {{"BloomData", binding::post}}, {{"sourceTexture", postSource}});
    }
    constexpr std::array<std::string_view, 8> lensFragments{"circleOfConfusionFragment", "prefilterFragment", "bokehFragment",
                                                            "filterFragment",            "combineFragment",   "tilesFragment",
                                                            "neighboursFragment",        "motionFragment"};
    for (std::size_t step = 0; step < lensFragments.size(); ++step)
    {
        if (core::Result<void> linked = link(m_lensPrograms[step], "lensVertex", lensFragments[step]); !linked)
        {
            return linked;
        }
        bindProgram(m_lensPrograms[step], {{"LensData", binding::post}},
                    {{"sourceImage", postSource},
                     {"sceneDepth", postDepth},
                     {"circleOfConfusionImage", postChain + static_cast<GLint>(lensCircleOfConfusion)},
                     {"halvedImage", postChain + static_cast<GLint>(lensHalved)},
                     {"bokehImage", postChain + static_cast<GLint>(lensBokeh)},
                     {"filteredImage", postChain + static_cast<GLint>(lensFiltered)},
                     {"focusedImage", postChain + static_cast<GLint>(lensFocused)},
                     {"tilesImage", postChain + static_cast<GLint>(lensTiles)},
                     {"neighboursImage", postChain + static_cast<GLint>(lensNeighbours)}});
    }
    gl::GenBuffers(1, &m_postBuffer);
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_postBuffer);
    gl::BufferData(GL_UNIFORM_BUFFER, sizeof(LensData), nullptr, GL_DYNAMIC_DRAW);
    return {};
}

void GlRenderer::destroyPostResources() noexcept
{
    gl::DeleteProgram(m_taaProgram.program);
    for (const Program& program : m_bloomPrograms)
    {
        gl::DeleteProgram(program.program);
    }
    for (const Program& program : m_lensPrograms)
    {
        gl::DeleteProgram(program.program);
    }
    gl::DeleteBuffers(1, &m_postBuffer);
    for (Target& target : m_resolved)
    {
        destroyTarget(target);
    }
    for (Target& target : m_bloomChain)
    {
        destroyTarget(target);
    }
    for (Target& target : m_lensImages)
    {
        destroyTarget(target);
    }
    destroyTarget(m_lensOutput);
}

bool GlRenderer::drawsPostEffects() const noexcept
{
    return m_sceneFormat == GL_RGBA16F;
}

void GlRenderer::drawPostStep(const Program& program, const Target& into, const void* settings, std::size_t size)
{
    gl::BindFramebuffer(GL_FRAMEBUFFER, into.framebuffer);
    gl::Viewport(0, 0, static_cast<GLsizei>(into.size.width), static_cast<GLsizei>(into.size.height));
    gl::UseProgram(program.program);
    gl::BindBuffer(GL_UNIFORM_BUFFER, m_postBuffer);
    gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(size), settings);
    gl::BindBufferRange(GL_UNIFORM_BUFFER, binding::post, m_postBuffer, 0, static_cast<GLsizeiptr>(size));
    gl::BindVertexArray(m_emptyArray);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    ++m_drawCalls;
}

GLuint GlRenderer::resolve(const ViewMatrices& matrices)
{
    const math::Extent2D size = m_scene.size;
    if (m_world.camera.antialiasing != Antialiasing::Temporal || !drawsPostEffects())
    {
        m_historyValid = false;
        return m_scene.texture;
    }
    for (Target& target : m_resolved)
    {
        if (target.size != size)
        {
            // A history of another size cannot be followed.
            m_historyValid = false;
        }
        if (core::Result<void> made = ensureTarget(target, size, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT); !made)
        {
            DEVEX_LOG_WARNING("{}: the image is not antialiased", made.error());
            return m_scene.texture;
        }
    }
    // An abrupt change of exposure, over a quarter stop, starts afresh: the scene mixes physical light and colours relative
    // to the exposure, which scaling the history would change.
    constexpr float exposureLimit = 1.1892071f;
    const float exposureRatio = m_exposure / m_historyExposure;
    const bool historyValid = m_historyValid && exposureRatio >= 1.0f / exposureLimit && exposureRatio <= exposureLimit;
    const TaaData data{
        .blend = taaBlend,
        .historyValid = historyValid ? 1.0f : 0.0f,
        .texelWidth = 1.0f / static_cast<float>(size.width),
        .texelHeight = 1.0f / static_cast<float>(size.height),
        .inverseViewProjection = matrices.inverseViewProjection,
        .previousViewProjection = m_previousViewProjection,
        .skyInverseViewProjection = math::inverse(matrices.skyViewProjection),
        .previousSkyViewProjection = m_previousSkyViewProjection,
    };
    const std::size_t into = m_frameIndex % 2;
    bindTexture(static_cast<GLuint>(postSource), m_scene.texture, m_clampSampler);
    bindTexture(static_cast<GLuint>(postDepth), m_sceneDepth, m_pointSampler);
    bindTexture(static_cast<GLuint>(postHistory), m_resolved[1 - into].texture, m_clampSampler);
    drawPostStep(m_taaProgram, m_resolved[into], &data, sizeof(data));
    // What the next frame follows its pixels back to.
    m_historyValid = true;
    m_historyExposure = m_exposure;
    return m_resolved[into].texture;
}

GLuint GlRenderer::drawLens(const ViewMatrices& matrices, GLuint source)
{
    const RenderCamera& camera = m_world.camera;
    const bool focusing = camera.depthOfField > 0.0f && camera.aperture > 0.0f && camera.projection == Projection::Perspective;
    const bool blurring = camera.motionBlur > 0.0f;
    if ((!focusing && !blurring) || !drawsPostEffects())
    {
        return source;
    }
    const math::Extent2D full = m_scene.size;
    const math::Extent2D half{std::max((full.width + 1) / 2, 1U), std::max((full.height + 1) / 2, 1U)};
    // A tile is as wide as the longest blur: a fifty-sixth of the height of the image.
    m_lensTileSize = static_cast<std::uint32_t>(std::max(8.0f, std::round(static_cast<float>(full.height) / 56.0f)));
    const math::Extent2D tiles{(full.width + m_lensTileSize - 1) / m_lensTileSize, (full.height + m_lensTileSize - 1) / m_lensTileSize};
    const std::array<std::tuple<math::Extent2D, GLenum, GLenum>, 7> images{{{full, GL_R16F, GL_RED},
                                                                             {half, GL_RGBA16F, GL_RGBA},
                                                                             {half, GL_RGBA16F, GL_RGBA},
                                                                             {half, GL_RGBA16F, GL_RGBA},
                                                                             {full, GL_RGBA16F, GL_RGBA},
                                                                             {tiles, GL_RG16F, GL_RG},
                                                                             {tiles, GL_RG16F, GL_RG}}};
    for (std::size_t image = 0; image < images.size(); ++image)
    {
        const auto& [size, internalFormat, format] = images[image];
        if (core::Result<void> made = ensureTarget(m_lensImages[image], size, internalFormat, format, GL_HALF_FLOAT); !made)
        {
            DEVEX_LOG_WARNING("{}: the lens draws nothing", made.error());
            return source;
        }
    }
    if (core::Result<void> made = ensureTarget(m_lensOutput, full, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT); !made)
    {
        DEVEX_LOG_WARNING("{}: the lens draws nothing", made.error());
        return source;
    }
    // A full-frame sensor, 24 mm high, behind a lens whose focal length gives the field of view, as Vulkan has it.
    const float focalLength = 0.012f / std::tan(std::max(camera.verticalFov, 1e-3f) * 0.5f);
    const LensData base{
        .imageWidth = static_cast<float>(full.width),
        .imageHeight = static_cast<float>(full.height),
        .aperture = focalLength / std::max(camera.aperture, 0.5f),
        .focalLength = focalLength,
        .focusDistance = std::max(camera.focusDistance, 0.01f),
        .amount = std::clamp(camera.depthOfField, 0.0f, 1.0f),
        .maxRadius = static_cast<float>(full.height) / 60.0f,
        .autoFocus = camera.autoFocus ? 1U : 0U,
        .shutter = std::clamp(camera.motionBlur, 0.0f, 1.0f),
        .tileSize = static_cast<float>(m_lensTileSize),
        .fromDepthOfField = focusing ? 1U : 0U,
        .frame = static_cast<std::uint32_t>(m_frameIndex),
        .inverseViewProjection = matrices.inverseViewProjection,
        .previousViewProjection = m_previousViewProjection,
        .view = camera.view,
        .skyInverseViewProjection = math::inverse(matrices.skyViewProjection),
        .previousSkyViewProjection = m_previousSkyViewProjection,
    };
    // A step into one image, every image of the chain bound but the one it writes.
    const auto pass = [&](std::size_t program, const Target& into, std::optional<std::size_t> written) {
        LensData data = base;
        data.targetWidth = static_cast<float>(into.size.width);
        data.targetHeight = static_cast<float>(into.size.height);
        bindTexture(static_cast<GLuint>(postSource), source, m_clampSampler);
        bindTexture(static_cast<GLuint>(postDepth), m_sceneDepth, m_pointSampler);
        for (std::size_t image = 0; image < m_lensImages.size(); ++image)
        {
            bindTexture(static_cast<GLuint>(postChain + static_cast<GLint>(image)), written == image ? m_black : m_lensImages[image].texture,
                        m_clampSampler);
        }
        drawPostStep(m_lensPrograms[program], into, &data, sizeof(data));
    };
    if (focusing)
    {
        pass(0, m_lensImages[lensCircleOfConfusion], lensCircleOfConfusion);
        pass(1, m_lensImages[lensHalved], lensHalved);
        pass(2, m_lensImages[lensBokeh], lensBokeh);
        pass(3, m_lensImages[lensFiltered], lensFiltered);
        pass(4, blurring ? m_lensImages[lensFocused] : m_lensOutput, blurring ? std::optional<std::size_t>(lensFocused) : std::nullopt);
    }
    if (blurring)
    {
        pass(5, m_lensImages[lensTiles], lensTiles);
        pass(6, m_lensImages[lensNeighbours], lensNeighbours);
        pass(7, m_lensOutput, std::nullopt);
    }
    return m_lensOutput.texture;
}

GLuint GlRenderer::drawBloom(GLuint finished)
{
    if (m_world.camera.bloom <= 0.0f || !drawsPostEffects())
    {
        return 0;
    }
    const math::Extent2D size = m_scene.size;
    for (std::size_t level = 0; level < m_bloomChain.size(); ++level)
    {
        // Every level halves the one before it, down to a few pixels.
        const math::Extent2D levelSize{std::max(size.width >> (level + 1), 1U), std::max(size.height >> (level + 1), 1U)};
        if (core::Result<void> made = ensureTarget(m_bloomChain[level], levelSize, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT); !made)
        {
            DEVEX_LOG_WARNING("{}: the image has no bloom", made.error());
            return 0;
        }
    }
    const auto step = [this](std::size_t program, GLuint read, math::Extent2D readSize, const Target& into, float strength) {
        const BloomData data{.texelWidth = 1.0f / static_cast<float>(readSize.width),
                             .texelHeight = 1.0f / static_cast<float>(readSize.height),
                             .strength = strength};
        bindTexture(static_cast<GLuint>(postSource), read, m_clampSampler);
        drawPostStep(m_bloomPrograms[program], into, &data, sizeof(data));
    };
    // The first step reads the finished image; each halving reads the level above; the last steps add themselves back, each
    // level over the one above.
    step(0, finished, size, m_bloomChain[0], m_world.camera.bloomThreshold);
    for (std::size_t level = 1; level < m_bloomChain.size(); ++level)
    {
        step(1, m_bloomChain[level - 1].texture, m_bloomChain[level - 1].size, m_bloomChain[level], 1.0f);
    }
    gl::Enable(GL_BLEND);
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
    for (std::size_t level = m_bloomChain.size() - 1; level > 0; --level)
    {
        step(2, m_bloomChain[level].texture, m_bloomChain[level].size, m_bloomChain[level - 1], 1.0f);
    }
    gl::Disable(GL_BLEND);
    return m_bloomChain[0].texture;
}

} // namespace devex::render::opengl
