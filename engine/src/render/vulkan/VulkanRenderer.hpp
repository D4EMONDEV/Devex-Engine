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
// Each frame is recorded as render graph passes: the sun's shadow cascades, the multisampled
// high dynamic range scene (forward+ lighting, then the sky), the luminance measure for automatic
// exposure, and tonemapping to the swapchain with the tools drawn over it.
class VulkanRenderer
{
public:
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

    [[nodiscard]] RendererStats stats() const noexcept;

    [[nodiscard]] core::Result<void> initializeImGui();
    void shutdownImGui() noexcept;
    void beginImGuiFrame();
    void queueImGuiDrawData() noexcept;

private:
    // Frames recorded before the oldest one must complete, which bounds the latency added by
    // the CPU running ahead of the GPU.
    static constexpr std::uint64_t framesInFlight = 2;
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

    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        // Signaled once the frame's commands have completed.
        VkFence completed = VK_NULL_HANDLE;
        // Signaled when the acquired swapchain image is ready to be rendered to.
        VkSemaphore imageAcquired = VK_NULL_HANDLE;
        // Data read by shaders through device addresses. Each frame keeps its own copies, so
        // writing them never touches memory a frame in flight reads.
        std::optional<Buffer> sceneData;
        std::optional<Buffer> materials;
        std::uint64_t materialVersion = 0;
        std::optional<Buffer> lights;
        std::optional<Buffer> clusters;
        std::optional<Buffer> clusterLights;
        // Exposed luminance measured on a grid by the last frame recorded with this context.
        std::optional<Buffer> luminance;
        bool luminanceMeasured = false;
        // The exposure that frame used, to turn measured values back into nits.
        float exposure = 1.0f;
        TransientImagePool images;
        VkImageView boundShadowMap = VK_NULL_HANDLE;
        VkImageView boundSceneColor = VK_NULL_HANDLE;
    };

    struct SubmeshRange
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    struct GpuMesh
    {
        Buffer vertices;
        Buffer indices;
        std::vector<SubmeshRange> submeshes;
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
    [[nodiscard]] core::Result<void> createDefaultResources();
    [[nodiscard]] core::Result<void> createScenePipelines();
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
    // Returns the number of draw calls recorded.
    [[nodiscard]] core::Result<std::uint32_t> recordFrame(FrameContext& frame, std::uint32_t frameSlot,
                                                          std::uint32_t imageIndex, bool drawImGui,
                                                          bool drawShadows);
    std::uint32_t drawMeshes(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData,
                             const Pipeline* shadowPipeline, std::uint32_t cascade,
                             std::uint32_t frameSlot) const;
    [[nodiscard]] core::Result<void> ensureHostBuffer(std::optional<Buffer>& buffer, VkDeviceSize bytes) const;
    void releaseRetiredResources() noexcept;
    void destroyPresentSemaphores() noexcept;

    platform::Window& m_window;
    PresentMode m_requestedPresentMode;
    std::uint32_t m_requestedSamples;
    std::filesystem::path m_shaderDirectory;
    Instance m_instance;
    Surface m_surface;
    Device m_device;
    // Everything allocated through VMA is declared below, so it is destroyed before the allocator.
    Allocator m_allocator;
    UploadContext m_upload;
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;
    std::optional<DescriptorSets> m_descriptors;
    std::unique_ptr<EnvironmentBaker> m_baker;
    std::optional<Image> m_brdfTable;
    // Absent while the window has no drawable area.
    std::optional<Swapchain> m_swapchain;
    std::optional<Pipeline> m_meshPipeline;
    std::optional<Pipeline> m_doubleSidedPipeline;
    std::optional<Pipeline> m_shadowPipeline;
    std::optional<Pipeline> m_skyPipeline;
    std::optional<Pipeline> m_luminancePipeline;
    std::optional<Pipeline> m_tonemapPipeline;
    math::Extent2D m_swapchainWindowPixelSize;
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
    RenderWorld m_world;
    LightClusters m_clusters;
    std::vector<GpuLight> m_gpuLights;
    std::optional<ShadowCascades> m_cascades;
    std::vector<float> m_luminanceSamples;
    float m_ev100 = 14.0f;
    float m_targetEv100 = 14.0f;
    bool m_exposureInitialized = false;
    std::chrono::steady_clock::time_point m_lastExposureUpdate;
    std::uint32_t m_lastDrawCalls = 0;
    std::uint32_t m_lastLightCount = 0;
    bool m_imguiInitialized = false;
    bool m_imguiDrawQueued = false;
    // ImGui's pipeline keeps a pointer to this format.
    VkFormat m_imguiColorFormat = VK_FORMAT_UNDEFINED;
};

} // namespace devex::render::vulkan
