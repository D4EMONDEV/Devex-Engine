#include "VulkanRenderer.hpp"

#include "Commands.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>

#include <atomic>
#include <bit>
#include <limits>
#include <utility>

namespace devex::render::vulkan {
namespace {

// volk loads device functions globally, so a second device would replace those of the first.
std::atomic<bool> rendererExists{false};

constexpr std::uint64_t noTimeout = std::numeric_limits<std::uint64_t>::max();

} // namespace

core::Result<std::unique_ptr<VulkanRenderer>> VulkanRenderer::create(
    const platform::Platform& platform, platform::Window& window, const RendererConfig& config)
{
    if (rendererExists.load())
    {
        return core::makeError(core::ErrorCode::InvalidState, "a Renderer already exists");
    }

    const core::Result<std::span<const char* const>> extensions =
        platform.vulkanInstanceExtensions();
    if (!extensions)
    {
        return std::unexpected(extensions.error());
    }

    core::Result<Instance> instance = Instance::create({
        .applicationName = config.applicationName,
        .requiredExtensions = *extensions,
        .validation = config.validation,
    });
    if (!instance)
    {
        return std::unexpected(instance.error());
    }

    const core::Result<std::uint64_t> surfaceHandle =
        window.createVulkanSurface(instance->handle());
    if (!surfaceHandle)
    {
        return std::unexpected(surfaceHandle.error());
    }
    Surface surface(instance->handle(), std::bit_cast<VkSurfaceKHR>(*surfaceHandle));

    core::Result<Device> device =
        Device::create(instance->handle(), surface.handle(), config.preferredGpu);
    if (!device)
    {
        return std::unexpected(device.error());
    }

    std::unique_ptr<VulkanRenderer> renderer(new VulkanRenderer(
        window, config, std::move(*instance), std::move(surface), std::move(*device)));

    const GpuInfo& gpu = renderer->gpu();
    DEVEX_LOG_INFO("GPU: {} ({}, Vulkan {}, {} {})", gpu.name, toString(gpu.type), gpu.apiVersion,
                   gpu.driverName, gpu.driverVersion);

    if (core::Result<void> contexts = renderer->createFrameContexts(); !contexts)
    {
        return std::unexpected(contexts.error());
    }
    if (const math::Extent2D pixelSize = window.pixelSize();
        pixelSize.width > 0 && pixelSize.height > 0)
    {
        if (core::Result<void> swapchain = renderer->recreateSwapchain(pixelSize); !swapchain)
        {
            return std::unexpected(swapchain.error());
        }
    }
    return renderer;
}

VulkanRenderer::VulkanRenderer(platform::Window& window, const RendererConfig& config,
                               Instance instance, Surface surface, Device device) noexcept
    : m_window(window)
    , m_requestedPresentMode(config.presentMode)
    , m_instance(std::move(instance))
    , m_surface(std::move(surface))
    , m_device(std::move(device))
{
    rendererExists.store(true);
}

VulkanRenderer::~VulkanRenderer()
{
    const VkDevice device = m_device.handle();
    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
        destroyPresentSemaphores();
        for (FrameContext& frame : m_frames)
        {
            vkDestroySemaphore(device, frame.imageAcquired, nullptr);
            vkDestroyFence(device, frame.completed, nullptr);
            vkDestroyCommandPool(device, frame.commandPool, nullptr);
        }
        m_swapchain.reset();
    }
    rendererExists.store(false);
}

const GpuInfo& VulkanRenderer::gpu() const noexcept
{
    return m_device.gpu();
}

PresentMode VulkanRenderer::presentMode() const noexcept
{
    return m_swapchain ? m_swapchain->presentMode() : m_requestedPresentMode;
}

RenderWorld& VulkanRenderer::beginFrame() noexcept
{
    m_world = RenderWorld{};
    return m_world;
}

core::Result<void> VulkanRenderer::endFrame()
{
    const math::Extent2D windowPixelSize = m_window.pixelSize();
    if (windowPixelSize.width == 0 || windowPixelSize.height == 0)
    {
        return {};
    }

    if (!m_swapchain || m_swapchainOutdated || windowPixelSize != m_swapchainWindowPixelSize)
    {
        if (core::Result<void> recreated = recreateSwapchain(windowPixelSize); !recreated)
        {
            return recreated;
        }
        if (!m_swapchain || m_swapchainOutdated)
        {
            return {};
        }
    }

    const VkDevice device = m_device.handle();
    const FrameContext& frame = m_frames[m_frameIndex % framesInFlight];
    DEVEX_VK_TRY(vkWaitForFences, device, 1, &frame.completed, VK_TRUE, noTimeout);

    std::uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device, m_swapchain->handle(), noTimeout,
                                                    frame.imageAcquired, VK_NULL_HANDLE,
                                                    &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_swapchainOutdated = true;
        return {};
    }
    if (acquired < VK_SUCCESS)
    {
        return vulkanError("vkAcquireNextImageKHR", acquired);
    }
    if (acquired == VK_SUBOPTIMAL_KHR)
    {
        m_swapchainOutdated = true;
    }

    DEVEX_VK_TRY(vkResetFences, device, 1, &frame.completed);
    if (core::Result<void> recorded = recordFrame(frame, imageIndex); !recorded)
    {
        return recorded;
    }

    const VkSemaphore presentSemaphore = m_presentSemaphores[imageIndex];
    const VkSemaphoreSubmitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAcquired,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };
    const VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = presentSemaphore,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo,
    };
    DEVEX_VK_TRY(vkQueueSubmit2, m_device.queue(), 1, &submitInfo, frame.completed);

    const VkSwapchainKHR swapchain = m_swapchain->handle();
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    };
    const VkResult presented = vkQueuePresentKHR(m_device.queue(), &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
    {
        m_swapchainOutdated = true;
    }
    else if (presented < VK_SUCCESS)
    {
        return vulkanError("vkQueuePresentKHR", presented);
    }

    ++m_frameIndex;
    return {};
}

core::Result<void> VulkanRenderer::createFrameContexts()
{
    const VkDevice device = m_device.handle();
    for (FrameContext& frame : m_frames)
    {
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_device.queueFamily(),
        };
        DEVEX_VK_TRY(vkCreateCommandPool, device, &poolInfo, nullptr, &frame.commandPool);

        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        DEVEX_VK_TRY(vkAllocateCommandBuffers, device, &allocateInfo, &frame.commandBuffer);

        const VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        DEVEX_VK_TRY(vkCreateFence, device, &fenceInfo, nullptr, &frame.completed);

        const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        DEVEX_VK_TRY(vkCreateSemaphore, device, &semaphoreInfo, nullptr, &frame.imageAcquired);
    }
    return {};
}

core::Result<void> VulkanRenderer::recreateSwapchain(math::Extent2D windowPixelSize)
{
    const VkDevice device = m_device.handle();
    DEVEX_VK_TRY(vkDeviceWaitIdle, device);

    core::Result<Swapchain> swapchain =
        Swapchain::create(m_device, m_surface.handle(),
                          {
                              .windowPixelSize = windowPixelSize,
                              .presentMode = m_requestedPresentMode,
                              .previous = m_swapchain ? m_swapchain->handle() : VK_NULL_HANDLE,
                          });
    if (!swapchain)
    {
        // The window can lose its drawable area between the size check and this point: retry on
        // a later frame.
        if (swapchain.error().code == core::ErrorCode::InvalidState)
        {
            m_swapchainOutdated = true;
            return {};
        }
        return std::unexpected(swapchain.error());
    }

    const bool isFirstSwapchain = !m_swapchain.has_value();
    m_swapchain = std::move(*swapchain);

    destroyPresentSemaphores();
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::uint32_t image = 0; image < m_swapchain->imageCount(); ++image)
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        DEVEX_VK_TRY(vkCreateSemaphore, device, &semaphoreInfo, nullptr, &semaphore);
        m_presentSemaphores.push_back(semaphore);
    }

    m_swapchainWindowPixelSize = windowPixelSize;
    m_swapchainOutdated = false;

    const math::Extent2D extent = m_swapchain->extent();
    if (isFirstSwapchain)
    {
        DEVEX_LOG_INFO("Swapchain {}x{} with {} images, {} presentation", extent.width,
                       extent.height, m_swapchain->imageCount(),
                       toString(m_swapchain->presentMode()));
        if (m_swapchain->presentMode() != m_requestedPresentMode)
        {
            DEVEX_LOG_WARNING("The display does not support {} presentation, using FIFO",
                              toString(m_requestedPresentMode));
        }
    }
    else
    {
        DEVEX_LOG_TRACE("Swapchain recreated at {}x{}", extent.width, extent.height);
    }
    return {};
}

core::Result<void> VulkanRenderer::recordFrame(const FrameContext& frame,
                                               std::uint32_t imageIndex) const
{
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    DEVEX_VK_TRY(vkResetCommandPool, m_device.handle(), frame.commandPool, 0);

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    DEVEX_VK_TRY(vkBeginCommandBuffer, commandBuffer, &beginInfo);

    const VkImage image = m_swapchain->image(imageIndex);
    transitionBackbuffer(commandBuffer, image, BackbufferState::Acquired,
                         BackbufferState::ColorAttachment);

    const math::Vec4& clearColor = m_world.clearColor;
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain->imageView(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {clearColor.r, clearColor.g, clearColor.b,
                                             clearColor.a}}},
    };
    const math::Extent2D extent = m_swapchain->extent();
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = {extent.width, extent.height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    vkCmdEndRendering(commandBuffer);

    transitionBackbuffer(commandBuffer, image, BackbufferState::ColorAttachment,
                         BackbufferState::Present);
    DEVEX_VK_TRY(vkEndCommandBuffer, commandBuffer);
    return {};
}

void VulkanRenderer::destroyPresentSemaphores() noexcept
{
    for (const VkSemaphore semaphore : m_presentSemaphores)
    {
        vkDestroySemaphore(m_device.handle(), semaphore, nullptr);
    }
    m_presentSemaphores.clear();
}

} // namespace devex::render::vulkan
