#pragma once

// The renderer of WebGPU, with Dawn: in a browser, and on a computer built with DEVEX_WEBGPU, where the tests compare
// its images with Vulkan's. It draws what a 2D game shows (web step 2): sprites, tilemaps, text and the interface,
// over the colour of the environment, exposed and tonemapped as Vulkan does. Meshes, lights, particles and the
// shaders of projects come with step 3: until then they are kept and not drawn.
#include "../FramePreparation.hpp"
#include "../RenderBackend.hpp"

#include <devex/core/SlotMap.hpp>

#include <webgpu/webgpu_cpp.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace devex::render::webgpu {

class WebGpuRenderer final : public RenderBackend
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<WebGpuRenderer>> create(const platform::Platform& platform,
                                                                              platform::Window& window,
                                                                              const RendererConfig& config);
    ~WebGpuRenderer() override;

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
        wgpu::Texture texture;
        // The texture with its sampler, as the sprites and the interface bind it.
        wgpu::BindGroup group;
        asset::TextureFormat format = asset::TextureFormat::Rgba8Srgb;
        math::Extent2D size;
        // Stored decoded, for a device that cannot read its blocks.
        bool decoded = false;
        // A texture that cannot be filtered, such as heights in floats, is never bound.
        bool bindable = true;
    };
    struct MeshRecord
    {
        std::uint32_t submeshes = 0;
    };
    struct Empty
    {
    };
    // An image the GPU copies into a buffer, which the CPU reads once it is mapped.
    struct Readback
    {
        std::uint64_t request = 0;
        wgpu::Buffer buffer;
        math::Extent2D size;
        std::uint32_t bytesPerRow = 0;
        // Set by the callback of the mapping: 1 mapped, -1 failed.
        std::shared_ptr<int> state = std::make_shared<int>(0);
    };
    // A capture asked for, drawn by the next frame.
    struct CaptureRequest
    {
        std::uint64_t id = 0;
        math::Extent2D maxSize;
        bool window = false;
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
        wgpu::BindGroup texture;
        // Pixels: left, top, width, height.
        std::uint32_t scissor[4]{};
    };

    WebGpuRenderer(platform::Window& window, const RendererConfig& config);
    [[nodiscard]] core::Result<void> initialize();
    [[nodiscard]] core::Result<void> createPipelines();
    [[nodiscard]] wgpu::ShaderModule shaderModule(std::string_view name) const;
    [[nodiscard]] core::Result<void> configureSurface(math::Extent2D size);
    void ensureSceneTarget(math::Extent2D size);
    void updateExposure();
    [[nodiscard]] wgpu::BindGroup textureGroup(const wgpu::Texture& texture, bool nearest) const;
    [[nodiscard]] const GpuTexture* textureAtSlot(std::uint32_t slot) const noexcept;
    void growBuffer(wgpu::Buffer& buffer, std::uint64_t& capacity, std::uint64_t size,
                    wgpu::BufferUsage usage, const char* label) const;
    // The image of the scene tonemapped into `target`, then the interface over it when asked.
    void drawImage(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, wgpu::TextureFormat format,
                   math::Extent2D size, bool interface);
    [[nodiscard]] std::uint32_t uiPipelineIndex(wgpu::TextureFormat format);
    void queueReadback(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture, math::Extent2D size,
                       std::uint64_t request);
    void collectReadbacks();

    platform::Window& m_window;
    RendererConfig m_config;
    GpuInfo m_gpu;
    PresentMode m_presentMode = PresentMode::Fifo;
    bool m_surfaceOutdated = true;

    wgpu::Instance m_instance;
    wgpu::Adapter m_adapter;
    wgpu::Device m_device;
    wgpu::Queue m_queue;
    wgpu::Surface m_surface;
    wgpu::TextureFormat m_surfaceFormat = wgpu::TextureFormat::Undefined;
    // The format the scene and the interface are drawn into the surface with: its sRGB view.
    wgpu::TextureFormat m_surfaceViewFormat = wgpu::TextureFormat::Undefined;
    std::vector<wgpu::PresentMode> m_presentModes;
    math::Extent2D m_surfaceSize;
    bool m_blockCompression = false;

    wgpu::Sampler m_linearSampler;
    wgpu::Sampler m_nearestSampler;
    wgpu::Sampler m_clampSampler;
    wgpu::BindGroupLayout m_textureLayout;
    wgpu::BindGroupLayout m_spriteLayout;
    wgpu::BindGroupLayout m_uiLayout;
    wgpu::BindGroupLayout m_tonemapLayout;
    wgpu::BindGroupLayout m_luminanceLayout;
    wgpu::RenderPipeline m_spritePipeline;
    wgpu::ComputePipeline m_luminancePipeline;
    wgpu::PipelineLayout m_uiPipelineLayout;
    wgpu::PipelineLayout m_tonemapPipelineLayout;
    wgpu::ShaderModule m_uiModule;
    wgpu::ShaderModule m_tonemapModule;
    // The interface and the tonemapping by the format they draw into.
    std::vector<std::pair<wgpu::TextureFormat, wgpu::RenderPipeline>> m_uiPipelines;
    std::vector<std::pair<wgpu::TextureFormat, wgpu::RenderPipeline>> m_tonemapPipelines;
    wgpu::BindGroup m_whiteGroup;
    wgpu::Texture m_white;

    wgpu::Texture m_sceneTarget;
    wgpu::TextureView m_sceneView;
    math::Extent2D m_sceneSize;

    // Buffers of the frame, grown as needed.
    wgpu::Buffer m_frameBuffer;
    wgpu::Buffer m_spriteBuffer;
    std::uint64_t m_spriteCapacity = 0;
    wgpu::Buffer m_drawBuffer;
    std::uint64_t m_drawCapacity = 0;
    wgpu::Buffer m_uiVertexBuffer;
    std::uint64_t m_uiVertexCapacity = 0;
    wgpu::Buffer m_uiIndexBuffer;
    std::uint64_t m_uiIndexCapacity = 0;
    wgpu::Buffer m_uiDrawBuffer;
    std::uint64_t m_uiDrawCapacity = 0;
    wgpu::Buffer m_tonemapBuffer;
    std::uint64_t m_tonemapCapacity = 0;
    // How many images this frame tonemapped: each reads its settings at its own offset.
    std::uint32_t m_tonemapPass = 0;
    wgpu::BindGroup m_tonemapGroup;
    wgpu::BindGroup m_uiGroup;
    wgpu::Buffer m_gridBuffer;
    wgpu::Buffer m_luminanceBuffer;

    core::SlotMap<GpuTexture, TextureTag> m_textures;
    core::SlotMap<MeshRecord, MeshTag> m_meshes;
    core::SlotMap<MaterialDesc, MaterialTag> m_materials;
    core::SlotMap<Empty, ShaderTag> m_shaders;
    core::SlotMap<Empty, ReflectionProbeTag> m_probes;
    core::SlotMap<Empty, IrradianceVolumeTag> m_volumes;

    RenderWorld m_world;
    SpriteFrame m_spriteFrame;
    PackedUi m_ui;
    // The handle of the texture at each slot the sprites name.
    std::vector<TextureHandle> m_slotHandles;
    std::vector<SpriteRun> m_spriteRuns;
    std::vector<UiRun> m_uiRuns;

    // The exposure, adapted from the luminance the frames measured, as Vulkan does.
    float m_ev100 = 0.0f;
    float m_targetEv100 = 0.0f;
    bool m_exposureInitialized = false;
    bool m_snapExposure = true;
    std::chrono::steady_clock::time_point m_lastExposureUpdate;
    std::optional<Readback> m_luminance;
    float m_measuredExposure = 0.0f;
    std::vector<float> m_luminanceSamples;
    float m_exposure = 1.0f;

    std::uint64_t m_nextCapture = 1;
    std::vector<CaptureRequest> m_captureRequests;
    std::vector<Readback> m_readbacks;
    std::vector<CapturedImage> m_captured;

    std::uint32_t m_drawCalls = 0;
    float m_time = 0.0f;
};

} // namespace devex::render::webgpu
