#pragma once

// The renderer of WebGPU, with Dawn: in a browser, and on a computer built with DEVEX_WEBGPU, where the tests compare
// its images with Vulkan's. It draws what the Compatibility method shows, step by step: sprites, tilemaps, text and the
// interface (web step 2), then meshes lit by the sun, the local lights and the environment, skinned or not (step
// 3a), over the sky, exposed and tonemapped as Vulkan does. Shadows, particles, fog and the shaders of projects come
// with the next steps: until then they are kept and not drawn.
#include "../FramePreparation.hpp"
#include "../GpuTypes.hpp"
#include "../RenderBackend.hpp"

#include <devex/core/SlotMap.hpp>
#include <devex/render/Culling.hpp>

#include <webgpu/webgpu_cpp.h>

#include <array>
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
        // Read at its nearest pixel, for pixel art.
        bool nearest = false;
    };
    struct GpuMesh
    {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        // The joints and weights of a skinned mesh; null for a rigid one.
        wgpu::Buffer skin;
        std::vector<asset::Submesh> submeshes;
        math::Aabb bounds;
    };
    struct GpuMaterial
    {
        MaterialDesc description;
        wgpu::Buffer uniform;
        // Its uniforms and textures; made again when a texture it reads goes.
        wgpu::BindGroup group;
        std::uint64_t textureEpoch = 0;
    };
    // The meshes of the frame that share a mesh, a submesh, a material and a way to be drawn, at once; a blended one
    // alone, in its order from the farthest.
    struct MeshBatch
    {
        const GpuMesh* mesh = nullptr;
        std::uint32_t submesh = 0;
        MaterialHandle material;
        std::uint32_t pipeline = 0;
        std::uint32_t firstInstance = 0;
        std::uint32_t instanceCount = 0;
        float distance = 0.0f;
    };
    // What the pass of blended things draws, in Vulkan's order: by layer, then order, then from the farthest. A blended
    // batch of meshes, a draw of particles, or runs of sprites that follow each other.
    struct TransparentDraw
    {
        enum class Kind : std::uint8_t
        {
            Mesh,
            Particles,
            Sprites,
        };
        Kind kind = Kind::Mesh;
        // The batch of meshes, or the draw of particles of the world.
        std::uint32_t index = 0;
        // The runs of sprites, among m_spriteRuns.
        std::uint32_t firstRun = 0;
        std::uint32_t runCount = 0;
    };
    // A pass that draws the depth of the casters of one view of the shadows.
    struct ShadowPass
    {
        // Its batches among m_shadowBatches.
        std::uint32_t firstBatch = 0;
        std::uint32_t batchCount = 0;
        // Its matrix, at its offset among those of the views.
        std::uint32_t view = 0;
        // A cascade draws into its layer of the map, a light into its tile of the atlas, in pixels.
        bool atlas = false;
        std::uint32_t layer = 0;
        std::array<float, 4> viewport{};
    };
    // The light of the environment: its maps, baked from the sky texture as Vulkan bakes them, or white for a sky of
    // one colour, which the colour then tints.
    struct Environment
    {
        TextureHandle sky;
        wgpu::Texture irradiance;
        wgpu::Texture specular;
        std::uint32_t specularMips = 1;
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
    // A run of sprites that read the same texture and normal map, drawn at once.
    struct SpriteRun
    {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        std::uint32_t texture = 0;
        std::uint32_t normalTexture = 0;
        wgpu::BindGroup group;
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

    // The 3D, in WebGpuScene.cpp.
    [[nodiscard]] core::Result<void> createScenePipelines();
    [[nodiscard]] wgpu::BindGroup materialGroup(GpuMaterial& material);
    [[nodiscard]] wgpu::Texture whiteCube() const;
    void bakeBrdfTable();
    void bakeEnvironment(TextureHandle sky);
    // The meshes of the frame by batches, and the data the scene pass reads of them.
    void prepareMeshes(const ViewMatrices& matrices, const Frustum& frustum);
    void drawSky(const wgpu::RenderPassEncoder& pass);
    // The depth of the shadow casters, into the cascades of the sun and the atlas of the local lights.
    void drawShadows(const wgpu::CommandEncoder& encoder);
    void drawMeshes(const wgpu::RenderPassEncoder& pass, bool blended);
    void drawMeshBatch(const wgpu::RenderPassEncoder& pass, std::uint32_t batch, std::uint32_t& bound);
    // The particles of the frame, and the order of what is blended, in WebGpuScene.cpp.
    [[nodiscard]] core::Result<void> createParticlePipelines();
    void prepareParticles(const ViewMatrices& matrices);
    void orderTransparent(const ViewMatrices& matrices);
    void drawTransparent(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& sprites);
    // How far a point is from the camera for sorting, as Vulkan measures it: along the view for an orthographic camera,
    // squared otherwise.
    [[nodiscard]] float sortDistance(math::Vec3 point, math::Vec3 cameraPosition) const noexcept;

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
    // The texture and the normal map of a run of sprites, with their samplers.
    wgpu::BindGroupLayout m_spriteTexturesLayout;
    wgpu::Buffer m_lights2DBuffer;
    std::uint64_t m_lights2DCapacity = 0;
    wgpu::Buffer m_shadows2DBuffer;
    std::uint64_t m_shadows2DCapacity = 0;
    std::vector<GpuLight2D> m_lights2D;
    std::vector<float> m_shadows2D;
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
    core::SlotMap<GpuMesh, MeshTag> m_meshes;
    core::SlotMap<GpuMaterial, MaterialTag> m_materials;
    // Counts the textures destroyed: a material made before reads one that went.
    std::uint64_t m_textureEpoch = 1;

    wgpu::Texture m_depthTarget;
    wgpu::TextureView m_depthView;
    wgpu::Texture m_flatNormal;
    wgpu::BindGroupLayout m_sceneLayout;
    wgpu::BindGroupLayout m_materialLayout;
    wgpu::BindGroupLayout m_meshDrawLayout;
    // Rigid or skinned, one side or both, opaque or blended: the index is 1 skinned + 2 both sides + 4 blended.
    std::array<wgpu::RenderPipeline, 8> m_meshPipelines;
    wgpu::RenderPipeline m_skyPipeline;
    wgpu::Buffer m_sceneBuffer;
    wgpu::Buffer m_lightBuffer;
    std::uint64_t m_lightCapacity = 0;
    wgpu::Buffer m_instanceBuffer;
    std::uint64_t m_instanceCapacity = 0;
    wgpu::Buffer m_boneBuffer;
    std::uint64_t m_boneCapacity = 0;
    wgpu::Buffer m_meshDrawBuffer;
    std::uint64_t m_meshDrawCapacity = 0;
    wgpu::BindGroup m_sceneGroup;
    wgpu::BindGroup m_meshDrawGroup;
    wgpu::Texture m_brdfTable;
    Environment m_environment;
    // The default material, for an instance whose material is gone.
    MaterialHandle m_defaultMaterial;
    std::vector<MeshBatch> m_meshBatches;
    std::vector<GpuLight> m_lights;

    // The shadows: the cascades of the sun in the layers of one map, those of the local lights in tiles of an atlas.
    wgpu::Texture m_cascadeMap;
    std::array<wgpu::TextureView, 4> m_cascadeLayers;
    wgpu::Texture m_atlasMap;
    wgpu::Sampler m_shadowSampler;
    wgpu::BindGroupLayout m_shadowLayout;
    // 1 skinned + 2 the atlas, whose slope bias is larger.
    std::array<wgpu::RenderPipeline, 4> m_shadowPipelines;
    // Depth clamped rather than clipped, as Vulkan draws shadows, where the device can.
    bool m_unclippedDepth = false;
    wgpu::Buffer m_shadowViewBuffer;
    std::uint64_t m_shadowViewCapacity = 0;
    wgpu::Buffer m_shadowMatrixBuffer;
    std::uint64_t m_shadowMatrixCapacity = 0;
    wgpu::BindGroup m_shadowGroup;
    std::vector<MeshBatch> m_shadowBatches;
    std::vector<ShadowPass> m_shadowPasses;

    // The particles and the ribbons of the frame.
    wgpu::BindGroupLayout m_particleLayout;
    wgpu::BindGroupLayout m_particleDrawLayout;
    // Particles, then ribbons.
    std::array<wgpu::RenderPipeline, 2> m_particlePipelines;
    std::vector<GpuParticle> m_particles;
    std::vector<GpuTrailPoint> m_points;
    wgpu::Buffer m_particleBuffer;
    std::uint64_t m_particleCapacity = 0;
    wgpu::Buffer m_pointBuffer;
    std::uint64_t m_pointCapacity = 0;
    wgpu::Buffer m_segmentBuffer;
    std::uint64_t m_segmentCapacity = 0;
    wgpu::Buffer m_particleDrawBuffer;
    std::uint64_t m_particleDrawCapacity = 0;
    // The data and texture of each draw of particles.
    std::vector<wgpu::BindGroup> m_particleGroups;
    wgpu::BindGroup m_particleDrawGroup;
    std::vector<TransparentDraw> m_transparentDraws;
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
