#pragma once

#include "Device.hpp"
#include "Instance.hpp"
#include "Swapchain.hpp"
#include "Vulkan.hpp"

#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>

#include <array>
#include <cstdint>
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

    [[nodiscard]] RenderWorld& beginFrame() noexcept;
    [[nodiscard]] core::Result<void> endFrame();

private:
    // Frames recorded before the oldest one must complete, which bounds the latency added by
    // the CPU running ahead of the GPU.
    static constexpr std::size_t framesInFlight = 2;

    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        // Signaled once the frame's commands have completed.
        VkFence completed = VK_NULL_HANDLE;
        // Signaled when the acquired swapchain image is ready to be rendered to.
        VkSemaphore imageAcquired = VK_NULL_HANDLE;
    };

    VulkanRenderer(platform::Window& window, const RendererConfig& config, Instance instance,
                   Surface surface, Device device) noexcept;

    [[nodiscard]] core::Result<void> createFrameContexts();
    [[nodiscard]] core::Result<void> recreateSwapchain(math::Extent2D windowPixelSize);
    [[nodiscard]] core::Result<void> recordFrame(const FrameContext& frame,
                                                 std::uint32_t imageIndex) const;
    void destroyPresentSemaphores() noexcept;

    platform::Window& m_window;
    PresentMode m_requestedPresentMode;
    Instance m_instance;
    Surface m_surface;
    Device m_device;
    // Absent while the window has no drawable area.
    std::optional<Swapchain> m_swapchain;
    math::Extent2D m_swapchainWindowPixelSize;
    bool m_swapchainOutdated = false;
    std::array<FrameContext, framesInFlight> m_frames{};
    // One per swapchain image: a presented image keeps its semaphore busy until it is replaced.
    std::vector<VkSemaphore> m_presentSemaphores;
    std::uint64_t m_frameIndex = 0;
    RenderWorld m_world;
};

} // namespace devex::render::vulkan
