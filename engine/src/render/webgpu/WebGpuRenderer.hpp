#pragma once

// The renderer of WebGPU, with Dawn: in a browser, and on a computer built with DEVEX_WEBGPU, where the tests compare
// its images with Vulkan's. It draws what the Compatibility method shows, as Vulkan does and from code shared with it
// where it can: sprites, tilemaps, text and the interface, the 2D lights; meshes, skinned or not, lit by the sun, the
// local lights and the environment, with their shadows, decals, terrains and grass, particles and trails, over the
// sky and in the fog; then bloom, temporal antialiasing, the effects of the lens and the colour table. Spatial shaders
// of projects draw from the WGSL their import compiles. The air of the atmosphere comes after the 1.0.
#include "../FramePreparation.hpp"
#include "../GpuTypes.hpp"
#include "../RenderBackend.hpp"

#include <devex/asset/ShaderData.hpp>
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
        // The same for a shader of the project: the slots of its uniforms, and its textures.
        wgpu::Buffer slots;
        wgpu::BindGroup customGroup;
        std::uint64_t customEpoch = 0;
    };
    // A shader of the project: what WebGPU draws with it, its module of WGSL, and its pipelines, made when first drawn:
    // rigid and skinned meshes, and their shadows into the cascades and into the atlas.
    struct GpuShader
    {
        asset::ShaderKind kind = asset::ShaderKind::Spatial;
        asset::ShaderCull cull = asset::ShaderCull::Back;
        asset::ShaderBlend blend = asset::ShaderBlend::Mix;
        bool transparent = false;
        bool castsShadows = true;
        wgpu::ShaderModule module;
        std::array<wgpu::RenderPipeline, 2> meshes;
        std::array<wgpu::RenderPipeline, 4> shadows;
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
    // The shaders of the project, in WebGpuScene.cpp: the spatial one a material draws with, if WebGPU can; its group,
    // its pipelines.
    [[nodiscard]] GpuShader* spatialShaderOf(const MaterialDesc& material);
    [[nodiscard]] wgpu::BindGroup customMaterialGroup(GpuMaterial& material);
    [[nodiscard]] const wgpu::RenderPipeline& customMeshPipeline(GpuShader& shader, bool skinned);
    [[nodiscard]] const wgpu::RenderPipeline& customShadowPipeline(GpuShader& shader, bool skinned, bool atlas);
    [[nodiscard]] GpuShader gpuShaderOf(const asset::ShaderData& shader) const;
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
    // The temporal antialiasing and the bloom, in WebGpuScene.cpp: what the tonemapping reads, resolved and haloed.
    [[nodiscard]] core::Result<void> createPostPipelines();
    void ensurePostTargets(math::Extent2D size);
    // Mixes the frame with the ones before when the camera asks for it; the image the bloom and the tonemapping read.
    [[nodiscard]] wgpu::TextureView resolve(const wgpu::CommandEncoder& encoder, const ViewMatrices& matrices);
    // An atlas of the textures of the decals: where it put every texture, and what it was drawn from.
    struct DecalAtlas
    {
        wgpu::Texture texture;
        wgpu::TextureView view;
        std::vector<std::pair<TextureHandle, math::Vec4>> rects;
        std::vector<std::uint64_t> key;
    };
    // The terrains, in WebGpuScene.cpp: the textures of the layers of each drawn into arrays, kept while they stay the
    // same, and what a frame draws of each: its group, its blocks and its grass.
    struct TerrainLayers
    {
        wgpu::Texture colors;
        wgpu::Texture surfaces;
        wgpu::Texture normals;
        std::vector<std::uint64_t> key;
    };
    struct TerrainDrawing
    {
        wgpu::BindGroup group;
        bool castShadows = true;
        std::array<std::uint32_t, 5> firstNode{};
        std::array<std::uint32_t, 5> nodeCount{};
        std::array<std::uint32_t, 3> tileCount{};
        std::array<std::uint32_t, 3> blades{};
        // Its three draws of grass among the draw data.
        std::uint32_t firstDraw = 0;
    };
    void createTerrainPipelines();
    void fillTerrainLayers(TerrainLayers& layers, const RenderTerrain& terrain);
    void prepareTerrains(const ViewMatrices& matrices);
    // The blocks, into the scene or, with its view, into a shadow map; the grass, into the scene.
    void drawTerrains(const wgpu::RenderPassEncoder& pass, std::optional<std::uint32_t> shadowView, bool atlas);
    void drawGrass(const wgpu::RenderPassEncoder& pass);
    // The decals of the frame, in WebGpuScene.cpp: their atlases drawn when their textures change, and what they read.
    void prepareDecals();
    void fillDecalAtlas(DecalAtlas& atlas, std::vector<TextureHandle> textures, wgpu::TextureFormat format, const wgpu::RenderPipeline& blit);
    // The halo of what is brighter than the threshold, in the first level of the chain; null without bloom.
    [[nodiscard]] wgpu::TextureView drawBloom(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& finished);
    // The depth of field and the motion blur of the camera over the resolved image, as Vulkan draws them; the image
    // itself without them.
    void ensureLensTargets(math::Extent2D size);
    [[nodiscard]] wgpu::TextureView drawLens(const wgpu::CommandEncoder& encoder, const ViewMatrices& matrices,
                                             const wgpu::TextureView& source);
    // The table of colours of the camera, when it is a texture that can be read.
    [[nodiscard]] const GpuTexture* colorTableOf(TextureHandle handle) const;
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
    // Grows with every texture made, changed or destroyed: the atlases of the decals are drawn again then.
    std::uint64_t m_textureRevision = 0;
    // The decals: the atlases their textures are drawn into, their colours (sRGB) and their data, each with where it
    // put every texture and what it was drawn from; the blit that fills them; and what the decals of the frame read.
    DecalAtlas m_decalColors;
    DecalAtlas m_decalData;
    wgpu::BindGroupLayout m_blitLayout;
    wgpu::RenderPipeline m_blitColors;
    wgpu::RenderPipeline m_blitData;
    wgpu::Buffer m_decalBuffer;
    std::uint64_t m_decalCapacity = 0;
    std::uint32_t m_decalCount = 0;
    wgpu::BindGroupLayout m_customMaterialLayout;
    wgpu::BindGroupLayout m_customShadowLayout;
    wgpu::BindGroup m_customShadowGroup;
    wgpu::Buffer m_instanceParameterBuffer;
    std::uint64_t m_instanceParameterCapacity = 0;
    std::vector<TerrainLayers> m_terrainLayers;
    std::vector<TerrainDrawing> m_terrainDrawings;
    wgpu::BindGroupLayout m_terrainLayout;
    wgpu::BindGroupLayout m_terrainDrawLayout;
    wgpu::BindGroupLayout m_emptyLayout;
    wgpu::BindGroup m_emptyGroup;
    wgpu::BindGroup m_terrainDrawGroup;
    wgpu::RenderPipeline m_terrainPipeline;
    wgpu::RenderPipeline m_grassPipeline;
    // Into the cascades, then into the atlas.
    std::array<wgpu::RenderPipeline, 2> m_terrainShadowPipelines;
    wgpu::Buffer m_terrainGrid;
    wgpu::Buffer m_grassBlade;
    wgpu::Buffer m_terrainBuffer;
    std::uint64_t m_terrainCapacity = 0;
    wgpu::Buffer m_terrainDrawBuffer;
    std::uint64_t m_terrainDrawCapacity = 0;
    wgpu::Buffer m_terrainNodeBuffer;
    std::uint64_t m_terrainNodeCapacity = 0;
    wgpu::Buffer m_grassTileBuffer;
    std::uint64_t m_grassTileCapacity = 0;

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

    // The temporal antialiasing: the frames it counts for its jitter, the two images it resolves into in turn, the
    // one before being its history, and what the frame before saw.
    std::uint64_t m_frameIndex = 0;
    std::array<wgpu::Texture, 2> m_resolved;
    std::array<wgpu::TextureView, 2> m_resolvedViews;
    bool m_historyValid = false;
    float m_historyExposure = 1.0f;
    math::Mat4 m_previousViewProjection{1.0f};
    math::Mat4 m_previousSkyViewProjection{1.0f};
    wgpu::BindGroupLayout m_taaLayout;
    wgpu::RenderPipeline m_taaPipeline;
    wgpu::Buffer m_taaBuffer;
    // The bloom: five levels, each half the one before, and its steps.
    std::array<wgpu::Texture, 5> m_bloomChain;
    std::array<wgpu::TextureView, 5> m_bloomViews;
    std::array<math::Extent2D, 5> m_bloomSizes;
    wgpu::BindGroupLayout m_bloomLayout;
    wgpu::RenderPipeline m_bloomThreshold;
    wgpu::RenderPipeline m_bloomDownsample;
    wgpu::RenderPipeline m_bloomUpsample;
    wgpu::Buffer m_bloomBuffer;
    // The lens: its chain (the circles of confusion, the halved image, its blur and its smoothing, the image in focus,
    // the tiles of motion and their neighbours), the image it gives, and a pipeline for each of its steps.
    std::array<wgpu::Texture, 7> m_lensChain;
    std::array<wgpu::TextureView, 7> m_lensViews;
    wgpu::Texture m_lensOutput;
    wgpu::TextureView m_lensOutputView;
    math::Extent2D m_lensSize{};
    std::uint32_t m_lensTileSize = 8;
    wgpu::BindGroupLayout m_lensLayout;
    std::array<wgpu::RenderPipeline, 8> m_lensPipelines;
    wgpu::Buffer m_lensBuffer;
    wgpu::Texture m_black;
    core::SlotMap<GpuShader, ShaderTag> m_shaders;
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
