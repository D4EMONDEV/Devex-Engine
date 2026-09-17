#pragma once

#include "Device.hpp"
#include "Instance.hpp"
#include "Vulkan.hpp"

#include <devex/math/Math.hpp>

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace devex::render::vulkan {

// Owns the VMA allocator that backs every buffer and image of the device.
class Allocator
{
public:
    [[nodiscard]] static core::Result<Allocator> create(const Instance& instance,
                                                        const Device& device);

    Allocator(Allocator&& other) noexcept;
    Allocator& operator=(Allocator&& other) noexcept;
    ~Allocator();

    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    [[nodiscard]] VmaAllocator handle() const noexcept;

private:
    Allocator() = default;
    void destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

struct BufferConfig
{
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    // Mapped for the whole lifetime of the buffer, for data written by the CPU.
    bool hostVisible = false;
};

class Buffer
{
public:
    [[nodiscard]] static core::Result<Buffer> create(const Allocator& allocator,
                                                     const BufferConfig& config);

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    [[nodiscard]] VkBuffer handle() const noexcept;
    [[nodiscard]] VkDeviceSize size() const noexcept;
    // Empty unless the buffer was created host-visible.
    [[nodiscard]] std::span<std::byte> mappedBytes() const noexcept;
    // Zero unless the buffer was created with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
    [[nodiscard]] VkDeviceAddress deviceAddress() const noexcept;

private:
    Buffer() = default;
    void destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    std::byte* m_mapped = nullptr;
    VkDeviceAddress m_deviceAddress = 0;
};

struct ImageConfig
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    math::Extent2D extent;
    VkImageUsageFlags usage = 0;
    std::uint32_t mipLevels = 1;
    // Six for a cube map.
    std::uint32_t layers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool cube = false;

    bool operator==(const ImageConfig&) const = default;
};

// A device-local image with a view on all its levels and layers: 2D, 2D array or cube.
class Image
{
public:
    [[nodiscard]] static core::Result<Image> create(const Device& device,
                                                    const Allocator& allocator,
                                                    const ImageConfig& config);

    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    [[nodiscard]] VkImage handle() const noexcept;
    [[nodiscard]] VkImageView view() const noexcept;
    [[nodiscard]] const ImageConfig& config() const noexcept;
    [[nodiscard]] VkFormat format() const noexcept;
    [[nodiscard]] math::Extent2D extent() const noexcept;
    [[nodiscard]] std::uint32_t mipLevels() const noexcept;

    // A view on part of the image, created on first use and owned by the image. Returns a null
    // handle when the view cannot be created.
    [[nodiscard]] VkImageView subview(VkImageViewType type, std::uint32_t baseMip,
                                      std::uint32_t mipCount, std::uint32_t baseLayer,
                                      std::uint32_t layerCount) const;

private:
    struct Subview
    {
        VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
        std::uint32_t baseMip = 0;
        std::uint32_t mipCount = 0;
        std::uint32_t baseLayer = 0;
        std::uint32_t layerCount = 0;
        VkImageView view = VK_NULL_HANDLE;
    };

    Image() = default;
    void destroy() noexcept;
    [[nodiscard]] VkImageView createView(VkImageViewType type, std::uint32_t baseMip,
                                         std::uint32_t mipCount, std::uint32_t baseLayer,
                                         std::uint32_t layerCount) const;

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    ImageConfig m_config;
    mutable std::vector<Subview> m_subviews;
};

} // namespace devex::render::vulkan
