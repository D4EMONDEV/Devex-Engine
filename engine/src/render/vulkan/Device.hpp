#pragma once

#include "Vulkan.hpp"

#include <devex/render/Gpu.hpp>

#include <cstdint>
#include <string_view>

namespace devex::render::vulkan {

// Owns the logical device and its single queue, which both draws and presents to the surface.
class Device
{
public:
    // Selects a GPU able to present to the surface and creates the logical device on it.
    [[nodiscard]] static core::Result<Device> create(VkInstance instance, VkSurfaceKHR surface,
                                                     std::string_view preferredGpu);

    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept;
    [[nodiscard]] VkDevice handle() const noexcept;
    [[nodiscard]] VkQueue queue() const noexcept;
    [[nodiscard]] std::uint32_t queueFamily() const noexcept;
    [[nodiscard]] const GpuInfo& gpu() const noexcept;
    // 1 when anisotropic filtering is unavailable.
    [[nodiscard]] float maxSamplerAnisotropy() const noexcept;
    // Sampled images a fragment shader may index in a descriptor set updated after binding.
    [[nodiscard]] std::uint32_t maxBindlessTextures() const noexcept;
    // The largest sample count up to `requested` that color and depth attachments both support.
    [[nodiscard]] VkSampleCountFlagBits sampleCount(std::uint32_t requested) const noexcept;
    [[nodiscard]] bool supportsDepthClamp() const noexcept;

private:
    Device() = default;
    void destroy() noexcept;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::uint32_t m_queueFamily = 0;
    GpuInfo m_gpu;
    float m_maxSamplerAnisotropy = 1.0f;
    std::uint32_t m_maxBindlessTextures = 0;
    VkSampleCountFlags m_sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    bool m_depthClamp = false;
};

} // namespace devex::render::vulkan
