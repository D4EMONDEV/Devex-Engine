#pragma once

// The renderer of OpenGL: OpenGL 3.3 on a computer, OpenGL ES 3.0 on a phone and WebGL2 in a browser, the light renderer
// that will take the place of WebGPU (see "Les API graphiques" in docs/decisions.md). It draws, as the first step of its
// plan, the 2D and the interface: the environment behind the scene, sprites and tilemaps, the tonemapping, the interface
// and the captures. The meshes, the lights, the particles and the shaders of projects are kept, not drawn yet. Its shaders
// are those of shaders/gl_*.slang, translated into GLSL at the build.
#include "GlApi.hpp"

#include "../FramePreparation.hpp"
#include "../GpuTypes.hpp"
#include "../RenderBackend.hpp"

#include <devex/core/SlotMap.hpp>
#include <devex/platform/Platform.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace devex::render::opengl {

class GlRenderer final : public RenderBackend
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<GlRenderer>> create(const platform::Platform& platform, platform::Window& window,
                                                                          const RendererConfig& config);
    ~GlRenderer() override;

    [[nodiscard]] const GpuInfo& gpu() const noexcept override;
    [[nodiscard]] PresentMode presentMode() const noexcept override;
    void setPresentMode(PresentMode mode) override;
    [[nodiscard]] core::Result<std::uint32_t> addWindowTarget(platform::Window& window) override;
    void removeWindowTarget(std::uint32_t target) override;
    [[nodiscard]] core::Result<std::string> describeGraphics() const override;

    [[nodiscard]] core::Result<MeshHandle> createMesh(const asset::MeshData& mesh) override;
    [[nodiscard]] bool isReady(MeshHandle mesh) const noexcept override;
    [[nodiscard]] bool isReady(TextureHandle texture) const noexcept override;
    void destroyMesh(MeshHandle mesh) override;
    [[nodiscard]] std::uint32_t submeshCount(MeshHandle mesh) const noexcept override;

    [[nodiscard]] core::Result<TextureHandle> createTexture(const asset::TextureData& texture) override;
    void destroyTexture(TextureHandle texture) override;
    [[nodiscard]] core::Result<void> updateTexture(TextureHandle texture, std::uint32_t top, std::uint32_t height,
                                                   std::span<const std::byte> rows) override;

    [[nodiscard]] core::Result<ReflectionProbeHandle> createReflectionProbe(const asset::ReflectionProbeData& probe) override;
    void destroyReflectionProbe(ReflectionProbeHandle probe) override;
    [[nodiscard]] core::Result<IrradianceVolumeHandle> createIrradianceVolume(const asset::IrradianceVolumeData& volume) override;
    void destroyIrradianceVolume(IrradianceVolumeHandle volume) override;

    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc& material) override;
    void updateMaterial(MaterialHandle handle, const MaterialDesc& material) override;
    void destroyMaterial(MaterialHandle material) override;

    [[nodiscard]] core::Result<ShaderHandle> createShader(const asset::ShaderData& shader) override;
    [[nodiscard]] core::Result<void> updateShader(ShaderHandle handle, const asset::ShaderData& shader) override;
    void destroyShader(ShaderHandle shader) override;

    [[nodiscard]] RenderWorld& beginFrame() noexcept override;
    void resetSceneHistory() noexcept override;
    [[nodiscard]] core::Result<void> endFrame() override;
    [[nodiscard]] std::vector<PickResult> takePickResults() override;
    [[nodiscard]] std::uint64_t requestCapture(std::uint32_t maxWidth, std::uint32_t maxHeight,
                                               std::optional<std::uint32_t> view) override;
    [[nodiscard]] std::uint64_t requestWindowCapture() override;
    [[nodiscard]] std::vector<CapturedImage> takeCaptures() override;

    [[nodiscard]] RendererStats stats() const noexcept override;

private:
    struct GpuTexture
    {
        gl::GLuint texture = 0;
        asset::TextureFormat format = asset::TextureFormat::Rgba8Srgb;
        math::Extent2D size;
        // A texture that cannot be filtered, such as heights in floats, is never sampled.
        bool bindable = true;
        // Read at its nearest pixel, for pixel art.
        bool nearest = false;
    };
    // What a mesh is until the 3D comes: its submeshes, which the scene asks for.
    struct GpuMesh
    {
        std::uint32_t submeshes = 0;
    };
    struct GpuMaterial
    {
        MaterialDesc description;
    };
    struct GpuShader
    {
        asset::ShaderKind kind = asset::ShaderKind::Spatial;
    };
    struct Empty
    {
    };
    // A program, linked from the vertex and the fragment shaders of a file of shaders/gl_*.slang.
    struct Program
    {
        gl::GLuint program = 0;
    };
    // A run of sprites that read the same texture, drawn at once.
    struct SpriteRun
    {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        std::uint32_t texture = 0;
    };
    // A batch of the interface as it is drawn.
    struct UiRun
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        gl::GLuint texture = 0;
        bool nearest = false;
        // Pixels from the top left corner: left, top, width, height.
        std::uint32_t scissor[4]{};
    };
    struct CaptureRequest
    {
        std::uint64_t id = 0;
        math::Extent2D maxSize;
        bool window = false;
    };
    // An image the frame draws into, and the framebuffer that draws into it.
    struct Target
    {
        gl::GLuint texture = 0;
        gl::GLuint framebuffer = 0;
        math::Extent2D size;
    };

    GlRenderer(platform::Window& window, const RendererConfig& config) noexcept;
    [[nodiscard]] core::Result<void> initialize();
    [[nodiscard]] core::Result<Program> linkProgram(std::string_view vertex, std::string_view fragment) const;
    // Binds the uniform block of `name` to `binding`, and the samplers of `samplers` to the texture units of their order.
    void bindProgram(const Program& program, std::initializer_list<std::pair<const char*, gl::GLuint>> blocks,
                     std::initializer_list<const char*> samplers) const;
    // Gives a buffer `size` bytes at least, kept when it already has them.
    void growBuffer(gl::GLuint buffer, gl::GLenum target, std::size_t& capacity, std::size_t size) const;
    [[nodiscard]] core::Result<void> ensureTarget(Target& target, math::Extent2D size, gl::GLenum internalFormat, gl::GLenum format,
                                                  gl::GLenum type) const;
    void destroyTarget(Target& target) const noexcept;
    [[nodiscard]] const GpuTexture* textureAtSlot(std::uint32_t slot) const noexcept;
    void bindTexture(gl::GLuint unit, gl::GLuint texture, gl::GLuint sampler) const;
    void applyPresentMode();

    void drawSky(const ViewMatrices& matrices);
    void drawSprites();
    // The scene tonemapped into the framebuffer bound, of `size`, and the interface over it. A framebuffer that keeps sRGB
    // encodes the colours itself, and blends them as light; another takes them encoded by the shaders.
    void drawImage(math::Extent2D size, bool interface, bool srgb);
    // The pixels of the framebuffer bound, from the top row down.
    [[nodiscard]] CapturedImage readPixels(std::uint64_t request, math::Extent2D size) const;

    platform::Window& m_window;
    RendererConfig m_config;
    void* m_context = nullptr;
    // Whether the functions of OpenGL were found, which the destructor calls.
    bool m_loaded = false;
    bool m_es = false;
    GpuInfo m_gpu;
    PresentMode m_presentMode = PresentMode::Fifo;
    bool m_presentModeApplied = false;
    // Whether the window keeps its colours in sRGB, as Vulkan's swapchain: a browser's does not.
    bool m_windowSrgb = false;
    // Whether the driver reads the blocks of BC7 and of ASTC.
    bool m_bptc = false;
    bool m_astc = false;
    float m_maxAnisotropy = 1.0f;
    gl::GLint m_uniformAlignment = 256;

    core::SlotMap<GpuTexture, TextureTag> m_textures;
    core::SlotMap<GpuMesh, MeshTag> m_meshes;
    core::SlotMap<GpuMaterial, MaterialTag> m_materials;
    core::SlotMap<GpuShader, ShaderTag> m_shaders;
    core::SlotMap<Empty, ReflectionProbeTag> m_probes;
    core::SlotMap<Empty, IrradianceVolumeTag> m_volumes;
    // The textures by the slot the sprites name them with: the index of their handle.
    std::vector<TextureHandle> m_slotHandles;

    Program m_skyProgram;
    Program m_spriteProgram;
    Program m_uiProgram;
    Program m_tonemapProgram;
    // The vertex arrays of what reads no vertices, of the sprites and of the interface.
    gl::GLuint m_emptyArray = 0;
    gl::GLuint m_spriteArray = 0;
    gl::GLuint m_uiArray = 0;
    gl::GLuint m_spriteBuffer = 0;
    std::size_t m_spriteCapacity = 0;
    gl::GLuint m_uiVertexBuffer = 0;
    std::size_t m_uiVertexCapacity = 0;
    gl::GLuint m_uiIndexBuffer = 0;
    std::size_t m_uiIndexCapacity = 0;
    // The uniform blocks: those of one draw, then those of each batch of the interface at an aligned offset of their own.
    gl::GLuint m_skyBuffer = 0;
    gl::GLuint m_spriteFrameBuffer = 0;
    gl::GLuint m_tonemapBuffer = 0;
    gl::GLuint m_uiDrawBuffer = 0;
    std::size_t m_uiDrawCapacity = 0;
    gl::GLuint m_linearSampler = 0;
    gl::GLuint m_nearestSampler = 0;
    gl::GLuint m_clampSampler = 0;
    gl::GLuint m_white = 0;
    gl::GLuint m_black = 0;
    // The scene in light, which the tonemapping maps to the display.
    Target m_scene;
    gl::GLenum m_sceneFormat = 0;

    RenderWorld m_world;
    SpriteFrame m_spriteFrame;
    std::vector<SpriteRun> m_spriteRuns;
    PackedUi m_ui;
    std::vector<UiRun> m_uiRuns;
    std::vector<std::byte> m_uiDraws;

    float m_ev100 = 0.0f;
    float m_exposure = 1.0f;
    float m_time = 0.0f;
    std::uint32_t m_drawCalls = 0;
    math::Extent2D m_windowSize;
    std::uint64_t m_nextCapture = 1;
    std::vector<CaptureRequest> m_captureRequests;
    std::vector<CapturedImage> m_captured;
};

} // namespace devex::render::opengl
