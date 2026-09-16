#pragma once

#include "Device.hpp"
#include "Vulkan.hpp"

#include <functional>

namespace devex::render::vulkan {

// Submits short command buffers and waits for them, to transfer data to the GPU synchronously.
class UploadContext
{
public:
    [[nodiscard]] static core::Result<UploadContext> create(const Device& device);

    UploadContext(UploadContext&& other) noexcept;
    UploadContext& operator=(UploadContext&& other) noexcept;
    ~UploadContext();

    UploadContext(const UploadContext&) = delete;
    UploadContext& operator=(const UploadContext&) = delete;

    // Records the commands, submits them and blocks until the GPU has executed them.
    [[nodiscard]] core::Result<void> submit(const std::function<void(VkCommandBuffer)>& record);

private:
    UploadContext() = default;
    void destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_completed = VK_NULL_HANDLE;
};

} // namespace devex::render::vulkan
