#include "Upload.hpp"

#include <limits>
#include <utility>

namespace devex::render::vulkan {

core::Result<UploadContext> UploadContext::create(const Device& device)
{
    UploadContext context;
    context.m_device = device.handle();
    context.m_queue = device.queue();

    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = device.queueFamily(),
    };
    DEVEX_VK_TRY(vkCreateCommandPool, context.m_device, &poolInfo, nullptr,
                 &context.m_commandPool);

    const VkCommandBufferAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = context.m_commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    DEVEX_VK_TRY(vkAllocateCommandBuffers, context.m_device, &allocateInfo,
                 &context.m_commandBuffer);

    const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    DEVEX_VK_TRY(vkCreateFence, context.m_device, &fenceInfo, nullptr, &context.m_completed);
    return context;
}

UploadContext::UploadContext(UploadContext&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_queue(std::exchange(other.m_queue, VK_NULL_HANDLE))
    , m_commandPool(std::exchange(other.m_commandPool, VK_NULL_HANDLE))
    , m_commandBuffer(std::exchange(other.m_commandBuffer, VK_NULL_HANDLE))
    , m_completed(std::exchange(other.m_completed, VK_NULL_HANDLE))
{
}

UploadContext& UploadContext::operator=(UploadContext&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_queue = std::exchange(other.m_queue, VK_NULL_HANDLE);
        m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
        m_commandBuffer = std::exchange(other.m_commandBuffer, VK_NULL_HANDLE);
        m_completed = std::exchange(other.m_completed, VK_NULL_HANDLE);
    }
    return *this;
}

UploadContext::~UploadContext()
{
    destroy();
}

void UploadContext::destroy() noexcept
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyFence(m_device, m_completed, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_completed = VK_NULL_HANDLE;
        m_commandPool = VK_NULL_HANDLE;
        m_commandBuffer = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
    }
}

core::Result<void> UploadContext::submit(const std::function<void(VkCommandBuffer)>& record)
{
    DEVEX_VK_TRY(vkResetFences, m_device, 1, &m_completed);
    DEVEX_VK_TRY(vkResetCommandPool, m_device, m_commandPool, 0);

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    DEVEX_VK_TRY(vkBeginCommandBuffer, m_commandBuffer, &beginInfo);
    record(m_commandBuffer);
    DEVEX_VK_TRY(vkEndCommandBuffer, m_commandBuffer);

    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = m_commandBuffer,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
    };
    DEVEX_VK_TRY(vkQueueSubmit2, m_queue, 1, &submitInfo, m_completed);
    DEVEX_VK_TRY(vkWaitForFences, m_device, 1, &m_completed, VK_TRUE,
                 std::numeric_limits<std::uint64_t>::max());
    return {};
}

} // namespace devex::render::vulkan
