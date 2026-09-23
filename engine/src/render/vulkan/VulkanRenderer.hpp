#pragma once

#include "../Lighting.hpp"
#include "DescriptorSets.hpp"
#include "Device.hpp"
#include "EnvironmentBaker.hpp"
#include "GpuData.hpp"
#include "Instance.hpp"
#include "Memory.hpp"
#include "Pipeline.hpp"
#include "RenderGraph.hpp"
#include "Swapchain.hpp"
#include "Upload.hpp"
#include "Vulkan.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/core/SlotMap.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Culling.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace devex::render::vulkan {

// Vulkan implementation behind render::Renderer.
//
// Each frame is recorded as render graph passes: the sun's shadow cascades, the
// high dynamic range scene (forward+ lighting, then the sky), the luminance measure for automatic
// exposure, picking and the selection mask when the tools ask for them, tonemapping to the
// swapchain or to the viewport image of the tools, the tools' overlay, and ImGui.
class VulkanRenderer
{
public:
    // Stands for the viewport image in ImGui draw data. It is replaced while drawing by the ImGui
    // descriptor set of the frame's image, and cannot collide with a real set, which is a pointer.
    static constexpr std::uint64_t viewportTextureId = 0xFFFF'FFFF'DE7E'0001ull;

    [[nodiscard]] static core::Result<std::unique_ptr<VulkanRenderer>> create(
        const platform::Platform& platform, platform::Window& window, const RendererConfig& config);

    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    [[nodiscard]] const GpuInfo& gpu() const noexcept;
    [[nodiscard]] PresentMode presentMode() const noexcept;

    [[nodiscard]] core::Result<MeshHandle> createMesh(const asset::MeshData& mesh);
    void destroyMesh(MeshHandle mesh);
    [[nodiscard]] std::uint32_t submeshCount(MeshHandle mesh) const noexcept;

    [[nodiscard]] core::Result<TextureHandle> createTexture(const asset::TextureData& texture);
    void destroyTexture(TextureHandle texture);

    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc& material);
    void updateMaterial(MaterialHandle handle, const MaterialDesc& material);
    void destroyMaterial(MaterialHandle material);

    [[nodiscard]] RenderWorld& beginFrame() noexcept;
    [[nodiscard]] core::Result<void> endFrame();
    [[nodiscard]] std::vector<PickResult> takePickResults();

    [[nodiscard]] RendererStats stats() const noexcept;

    [[nodiscard]] core::Result<void> initializeImGui();
    void shutdownImGui() noexcept;
    void beginImGuiFrame();
    void queueImGuiDrawData() noexcept;
    [[nodiscard]] bool imGuiNeedsLinearColors() const noexcept;

private:
    // Frames recorded before the oldest one must complete, which bounds the latency added by
    // the CPU running ahead of the GPU.
    static constexpr std::uint64_t framesInFlight = 2;
    // How many jittered frames cover a pixel before the pattern starts over.
    static constexpr std::uint64_t taaSampleCount = 8;
    // How much of the history a still pixel keeps: higher is smoother and slower to follow.
    static constexpr float taaBlend = 0.9f;
    static constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat sceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    // More textures than any scene needs for now, within what drivers commonly allow.
    static constexpr std::uint32_t maxTextures = 8192;
    // Bindless slots of the textures that stand in for missing ones.
    static constexpr std::uint32_t whiteTextureSlot = 0;
    static constexpr std::uint32_t flatNormalTextureSlot = 1;
    static constexpr std::uint32_t shadowMapSize = 2048;
    static constexpr std::uint32_t luminanceGridWidth = 64;
    static constexpr std::uint32_t luminanceGridHeight = 36;
    static constexpr VkFormat pickFormat = VK_FORMAT_R32_UINT;
    static constexpr VkFormat selectionMaskFormat = VK_FORMAT_R8_UNORM;
    // Where a pixel was on the previous frame, and the normal of its surface.
    static constexpr VkFormat velocityFormat = VK_FORMAT_R16G16_SFLOAT;
    static constexpr VkFormat normalFormat = VK_FORMAT_R16G16_SFLOAT;
    static constexpr VkFormat occlusionFormat = VK_FORMAT_R8_UNORM;
    // The shadows of the local lights share one image, cut into square tiles.
    static constexpr std::uint32_t shadowAtlasSize = 4096;
    static constexpr std::uint32_t shadowAtlasCell = 512;
    static constexpr std::uint32_t shadowAtlasCells = shadowAtlasSize / shadowAtlasCell;
    // How many lights are given tiles twice as wide, from the most important down.
    static constexpr std::size_t largeShadowLights = 2;
    // Larger viewports are clamped, since images this large would exhaust memory.
    static constexpr std::uint32_t maxViewportSize = 8192;

    enum class MeshPass : std::uint8_t
    {
        // Depth, motion and normals of everything that hides what is behind it.
        Prepass,
        // Everything that hides what is behind it, including the cut-out materials.
        Scene,
        // The blended materials, drawn after the scene from the farthest to the nearest.
        Transparent,
        Shadow,
        // Into one tile of the shadow atlas, for a local light.
        LocalShadow,
        Pick,
        // Only the outlined instances.
        SelectionMask,
    };

    // Where each list of the overlay starts in the frame's overlay vertex buffer.
    struct OverlayRanges
    {
        std::uint32_t sceneLines = 0;
        std::uint32_t overlayLines = 0;
        std::uint32_t overlayTriangles = 0;
    };

    // Timestamps a frame can write: one per pass of the render graph, and one more.
    static constexpr std::uint32_t maxTimestamps = 128;

    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        // Signaled once the frame's commands have completed.
        VkFence completed = VK_NULL_HANDLE;
        // Signaled when the acquired swapchain image is ready to be rendered to.
        VkSemaphore imageAcquired = VK_NULL_HANDLE;
        // One timestamp before each pass of the graph and one after the last, read once the GPU
        // has finished the frame; the names are those of the passes that follow each timestamp.
        VkQueryPool timestamps = VK_NULL_HANDLE;
        std::uint32_t timestampCount = 0;
        std::vector<const char*> timedPasses;
        // The frame of the profiler the timestamps belong to.
        std::uint64_t profiledFrame = 0;
        // Data read by shaders through device addresses. Each frame keeps its own copies, so
        // writing them never touches memory a frame in flight reads.
        std::optional<Buffer> sceneData;
        std::optional<Buffer> materials;
        std::uint64_t materialVersion = 0;
        std::optional<Buffer> lights;
        // The bone matrices of every skinned instance of the frame.
        std::optional<Buffer> bones;
        std::optional<Buffer> clusters;
        std::optional<Buffer> clusterLights;
        // Exposed luminance measured on a grid by the last frame recorded with this context.
        std::optional<Buffer> luminance;
        bool luminanceMeasured = false;
        // The exposure that frame used, to turn measured values back into nits.
        float exposure = 1.0f;
        TransientImagePool images;
        DescriptorSets::FrameImages boundImages;
        // Where every skinned instance stood on the previous frame, for the motion of its pixels.
        std::optional<Buffer> previousBones;
        // The views the local lights were given in the shadow atlas.
        std::optional<Buffer> shadowViews;
        std::optional<Buffer> overlayVertices;
        // The triangles of the interface of the frame.
        std::optional<Buffer> uiVertices;
        std::optional<Buffer> uiIndices;
        // The object identifier the frame read under the requested pixel.
        std::optional<Buffer> pickReadback;
        std::optional<std::uint64_t> pickRequest;
        // ImGui's descriptor set for the viewport image of this frame context.
        VkDescriptorSet imguiViewport = VK_NULL_HANDLE;
        VkImageView imguiViewportView = VK_NULL_HANDLE;
    };

    struct SubmeshRange
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    struct GpuMesh
    {
        // The box holding the mesh, which the culling tests against the views.
        math::Aabb bounds;
        Buffer vertices;
        Buffer indices;
        std::vector<SubmeshRange> submeshes;
        // Only for a skinned mesh: the joints and weights of its vertices.
        std::optional<Buffer> skin;
    };

    // A destroyed mesh waiting for the frames that may still reference it.
    struct RetiredMesh
    {
        GpuMesh mesh;
        std::uint64_t retiredAtFrame = 0;
    };

    struct GpuTexture
    {
        Image image;
        std::uint32_t slot = 0;
    };

    struct RetiredTexture
    {
        GpuTexture texture;
        std::uint64_t retiredAtFrame = 0;
    };

    struct RetiredEnvironment
    {
        EnvironmentMaps maps;
        std::uint64_t retiredAtFrame = 0;
    };

    VulkanRenderer(platform::Window& window, const RendererConfig& config,
                   std::filesystem::path shaderDirectory, Instance instance, Surface surface,
                   Device device, Allocator allocator, UploadContext upload) noexcept;

    [[nodiscard]] core::Result<void> createFrameContexts();
    // Hands the GPU times of the passes of a finished frame to the profiler.
    void reportPassTimes(FrameContext& frame);
    [[nodiscard]] core::Result<void> createDefaultResources();
    [[nodiscard]] core::Result<void> createScenePipelines();
    // Pipelines drawing into the swapchain or the viewport image, which share its format.
    [[nodiscard]] core::Result<void> createTargetPipelines(VkFormat format);
    [[nodiscard]] core::Result<void> recreateSwapchain(math::Extent2D windowPixelSize);
    [[nodiscard]] core::Result<GpuTexture> uploadTexture(const asset::TextureData& texture,
                                                         std::uint32_t slot);
    [[nodiscard]] GpuMaterial toGpuMaterial(const MaterialDesc& material) const noexcept;
    // Refreshes the frame's copy of the materials when it is older than the current ones.
    [[nodiscard]] core::Result<void> updateFrameMaterials(FrameContext& frame);
    // Bakes image based lighting when the sky texture changed.
    [[nodiscard]] core::Result<void> updateEnvironment();
    void updateExposure(FrameContext& frame);
    [[nodiscard]] core::Result<void> uploadLights(FrameContext& frame, float aspectRatio);
    void writeSceneData(FrameContext& frame, const std::optional<ShadowCascades>& cascades) const noexcept;
    [[nodiscard]] core::Result<OverlayRanges> uploadOverlay(FrameContext& frame);
    [[nodiscard]] core::Result<void> uploadUi(FrameContext& frame);
    [[nodiscard]] core::Result<void> uploadPreviousBones(FrameContext& frame) const;
    // Gives the local lights that cast shadows a tile of the atlas, the most important first, and
    // builds the view each tile is drawn through. Lights that do not fit keep their light alone.
    void assignShadowViews();
    [[nodiscard]] core::Result<void> uploadShadowViews(FrameContext& frame) const;
    // Creates the two images the antialiasing carries from frame to frame, when the scene changed
    // size or they do not exist yet.
    [[nodiscard]] core::Result<void> ensureHistory();
    // Remembers where the instances of this frame stood, for the motion of the next one.
    void rememberFrame();
    // Keeps the answer of the pick request the frame recorded, now that it completed.
    void readPickResult(FrameContext& frame);
    // Returns the number of draw calls recorded.
    [[nodiscard]] core::Result<std::uint32_t> recordFrame(FrameContext& frame, std::uint32_t frameSlot,
                                                          std::uint32_t imageIndex, bool drawImGui,
                                                          bool drawShadows);
    // Whether the instance blends with what is behind it, and so belongs to the transparent pass.
    [[nodiscard]] bool isBlended(const MeshInstance& instance) const noexcept;
    // The instances a pass draws, in the order it draws them: those its view can see.
    [[nodiscard]] std::span<const std::uint32_t> instancesOf(MeshPass pass,
                                                             std::uint32_t cascade) const;
    // The box an instance takes in the world, grown for a skinned mesh whose pose has left its
    // bind pose behind.
    [[nodiscard]] math::Aabb worldBounds(const MeshInstance& instance) const noexcept;
    // The key an instance keeps between frames: its entity and the submesh it draws.
    [[nodiscard]] static std::uint64_t instanceKey(const MeshInstance& instance) noexcept
    {
        return (static_cast<std::uint64_t>(instance.objectId) << 32) | instance.submesh;
    }
    std::uint32_t drawMeshes(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData,
                             VkDeviceAddress boneMatrices, MeshPass pass, std::uint32_t cascade,
                             std::uint32_t frameSlot, VkDeviceAddress previousBones = 0) const;
    // Copies the bone matrices of the frame's skinned instances into its buffer.
    [[nodiscard]] core::Result<void> uploadBones(FrameContext& frame) const;
    [[nodiscard]] core::Result<void> ensureHostBuffer(std::optional<Buffer>& buffer, VkDeviceSize bytes) const;
    // Replaces the viewport placeholder of the ImGui draw data with the frame's viewport image.
    void bindViewportTexture(FrameContext& frame, VkImageView viewport);
    void releaseRetiredResources() noexcept;
    void destroyPresentSemaphores() noexcept;

    platform::Window& m_window;
    PresentMode m_requestedPresentMode;
    std::filesystem::path m_shaderDirectory;
    Instance m_instance;
    Surface m_surface;
    Device m_device;
    // Everything allocated through VMA is declared below, so it is destroyed before the allocator.
    Allocator m_allocator;
    UploadContext m_upload;
    std::optional<DescriptorSets> m_descriptors;
    std::unique_ptr<EnvironmentBaker> m_baker;
    std::optional<Image> m_brdfTable;
    // Absent while the window has no drawable area.
    std::optional<Swapchain> m_swapchain;
    std::optional<Pipeline> m_meshPipeline;
    std::optional<Pipeline> m_doubleSidedPipeline;
    std::optional<Pipeline> m_transparentPipeline;
    std::optional<Pipeline> m_transparentDoubleSidedPipeline;
    std::optional<Pipeline> m_prepassPipeline;
    std::optional<Pipeline> m_prepassDoubleSidedPipeline;
    // Blended instances of the frame, farthest first; kept between frames to avoid allocating.
    mutable std::vector<std::pair<float, std::uint32_t>> m_transparentOrder;
    mutable std::vector<std::uint32_t> m_passInstances;
    // Where the camera of the frame is, which sorts the blended instances.
    mutable math::Vec3 m_cameraPosition{0.0f};
    // What the camera and each shadow cascade can see, rebuilt every frame.
    mutable Frustum m_cameraFrustum;
    mutable std::array<Frustum, cascadeCount> m_cascadeFrustums{};
    // Instances kept and dropped by the culling of the frame, for the statistics.
    mutable std::uint32_t m_culledInstances = 0;
    // The views the local lights were given this frame, and what each one draws.
    std::vector<GpuShadowView> m_shadowViews;
    std::vector<Frustum> m_shadowViewFrustums;
    std::uint32_t m_lastCulledInstances = 0;
    // Where each instance stood on the previous frame, by entity and submesh, and where its bones
    // were. An instance that was not there yet simply does not move.
    std::unordered_map<std::uint64_t, math::Mat4> m_previousTransforms;
    std::unordered_map<std::uint64_t, std::uint32_t> m_previousFirstBone;
    std::vector<math::Mat4> m_previousBoneMatrices;
    // The view and projection of the previous frame, without its jitter.
    math::Mat4 m_previousViewProjection{1.0f};
    mutable math::Mat4 m_unjitteredViewProjection{1.0f};
    // How far the projection of this frame is moved, in clip space.
    math::Vec2 m_jitter{0.0f};
    std::optional<Pipeline> m_shadowPipeline;
    std::optional<Pipeline> m_skyPipeline;
    std::optional<Pipeline> m_luminancePipeline;
    std::optional<Pipeline> m_pickPipeline;
    std::optional<Pipeline> m_selectionMaskPipeline;
    std::optional<Pipeline> m_tonemapPipeline;
    std::optional<Pipeline> m_sceneLinePipeline;
    std::optional<Pipeline> m_outlinePipeline;
    std::optional<Pipeline> m_overlayLinePipeline;
    std::optional<Pipeline> m_overlayTrianglePipeline;
    std::optional<Pipeline> m_uiPipeline;
    std::optional<Pipeline> m_taaPipeline;
    std::optional<Pipeline> m_aoPipeline;
    std::optional<Pipeline> m_localShadowPipeline;
    std::optional<Pipeline> m_bloomThresholdPipeline;
    std::optional<Pipeline> m_bloomDownsamplePipeline;
    std::optional<Pipeline> m_bloomUpsamplePipeline;
    // Bound as the selection mask of frames that outline nothing.
    std::optional<Image> m_emptySelectionMask;
    std::optional<Image> m_whiteImage;
    // The same, left in the general layout, for the bloom chain a frame did not build.
    std::optional<Image> m_whiteGeneralImage;
    // Stands in for the shadow atlas when no local light casts a shadow.
    std::optional<Image> m_emptyShadowAtlas;
    // What the antialiasing resolved, kept from one frame to the next: one image is read while the
    // other is written, and they swap every frame.
    std::array<std::optional<Image>, 2> m_history;
    std::array<ImageState, 2> m_historyStates{ImageState::Undefined, ImageState::Undefined};
    math::Extent2D m_historyExtent;
    // False until a frame has resolved into the history, and whenever it was just recreated.
    bool m_historyValid = false;
    math::Extent2D m_swapchainWindowPixelSize;
    // The size of the scene image of the frame being recorded.
    math::Extent2D m_sceneExtent;
    bool m_swapchainOutdated = false;
    std::array<FrameContext, framesInFlight> m_frames{};
    // One per swapchain image: a presented image keeps its semaphore busy until it is replaced.
    std::vector<VkSemaphore> m_presentSemaphores;

    core::SlotMap<GpuMesh, MeshTag> m_meshes;
    std::vector<RetiredMesh> m_retiredMeshes;
    std::optional<GpuTexture> m_whiteTexture;
    std::optional<GpuTexture> m_flatNormalTexture;
    core::SlotMap<GpuTexture, TextureTag> m_textures;
    std::vector<RetiredTexture> m_retiredTextures;
    // Bindless slots released by destroyed textures, reused before new ones.
    std::vector<std::uint32_t> m_freeTextureSlots;
    std::uint32_t m_nextTextureSlot = flatNormalTextureSlot + 1;
    core::SlotMap<MaterialDesc, MaterialTag> m_materials;
    MaterialHandle m_defaultMaterial;
    // GPU form of every material slot, rebuilt when a material or a texture changes.
    std::vector<GpuMaterial> m_gpuMaterials;
    std::uint64_t m_materialVersion = 1;
    bool m_materialsChanged = true;

    // Image based lighting of a uniform white sky, and of the current sky texture.
    std::optional<EnvironmentMaps> m_uniformEnvironment;
    std::optional<EnvironmentMaps> m_textureEnvironment;
    TextureHandle m_environmentSource;
    TextureHandle m_failedEnvironment;
    bool m_environmentBound = false;
    std::vector<RetiredEnvironment> m_retiredEnvironments;

    std::uint64_t m_frameIndex = 0;
    // Nanoseconds per timestamp tick, 0 when the queue writes none; and the bits it writes.
    double m_timestampPeriod = 0.0;
    std::uint64_t m_timestampMask = ~std::uint64_t{0};
    RenderWorld m_world;
    LightClusters m_clusters;
    std::vector<GpuLight> m_gpuLights;
    std::optional<ShadowCascades> m_cascades;
    std::vector<float> m_luminanceSamples;
    float m_ev100 = 14.0f;
    float m_targetEv100 = 14.0f;
    bool m_exposureInitialized = false;
    std::chrono::steady_clock::time_point m_lastExposureUpdate;
    std::vector<PickResult> m_pickResults;
    std::uint32_t m_lastDrawCalls = 0;
    std::uint32_t m_lastLightCount = 0;
    bool m_imguiInitialized = false;
    bool m_imguiDrawQueued = false;
    // ImGui's pipeline keeps a pointer to this format.
    VkFormat m_imguiColorFormat = VK_FORMAT_UNDEFINED;
};

} // namespace devex::render::vulkan
