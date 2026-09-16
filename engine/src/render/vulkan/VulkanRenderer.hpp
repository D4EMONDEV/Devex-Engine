#pragma once

#include "Device.hpp"
#include "Instance.hpp"
#include "Memory.hpp"
#include "Pipeline.hpp"
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
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace devex::render::vulkan {

// Vulkan implementation behind render::Renderer.
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

    [[nodiscard]] RenderWorld& beginFrame() noexcept;
    [[nodiscard]] core::Result<void> endFrame();

private:
    // Frames recorded before the oldest one must complete, which bounds the latency added by
    // the CPU running ahead of the GPU.
    static constexpr std::uint64_t framesInFlight = 2;
    static constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        // Signaled once the frame's commands have completed.
        VkFence completed = VK_NULL_HANDLE;
        // Signaled when the acquired swapchain image is ready to be rendered to.
        VkSemaphore imageAcquired = VK_NULL_HANDLE;
        // Camera and lighting of the frame, read by shaders through its device address.
        std::optional<Buffer> sceneData;
    };

    struct GpuMesh
    {
        Buffer vertices;
        Buffer indices;
        std::uint32_t indexCount = 0;
    };

    // A destroyed mesh waiting for the frames that may still reference it.
    struct RetiredMesh
    {
        GpuMesh mesh;
        std::uint64_t retiredAtFrame = 0;
    };

    VulkanRenderer(platform::Window& window, const RendererConfig& config,
                   std::filesystem::path shaderDirectory, Instance instance, Surface surface,
                   Device device, Allocator allocator, UploadContext upload) noexcept;

    [[nodiscard]] core::Result<void> createFrameContexts();
    [[nodiscard]] core::Result<void> recreateSwapchain(math::Extent2D windowPixelSize);
    [[nodiscard]] core::Result<void> recordFrame(const FrameContext& frame,
                                                 std::uint32_t imageIndex) const;
    void writeSceneData(const FrameContext& frame) const noexcept;
    void releaseRetiredMeshes() noexcept;
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
    // Absent while the window has no drawable area.
    std::optional<Swapchain> m_swapchain;
    std::optional<Image> m_depthImage;
    std::optional<GraphicsPipeline> m_meshPipeline;
    math::Extent2D m_swapchainWindowPixelSize;
    bool m_swapchainOutdated = false;
    std::array<FrameContext, framesInFlight> m_frames{};
    // One per swapchain image: a presented image keeps its semaphore busy until it is replaced.
    std::vector<VkSemaphore> m_presentSemaphores;
    core::SlotMap<GpuMesh, MeshTag> m_meshes;
    std::vector<RetiredMesh> m_retiredMeshes;
    std::uint64_t m_frameIndex = 0;
    RenderWorld m_world;
};

} // namespace devex::render::vulkan
