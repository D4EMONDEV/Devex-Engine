#include "Vulkan.hpp"

// volk must be included first so that VMA exposes vmaImportVulkanFunctionsFromVolk.
#define VMA_IMPLEMENTATION
#include "Memory.hpp"

#include <utility>

namespace devex::render::vulkan {

core::Result<Allocator> Allocator::create(const Instance& instance, const Device& device)
{
    VmaAllocatorCreateInfo createInfo{};
    createInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    createInfo.physicalDevice = device.physicalDevice();
    createInfo.device = device.handle();
    createInfo.instance = instance.handle();
    createInfo.vulkanApiVersion = requiredApiVersion;

    VmaVulkanFunctions functions{};
    DEVEX_VK_TRY(vmaImportVulkanFunctionsFromVolk, &createInfo, &functions);
    createInfo.pVulkanFunctions = &functions;

    Allocator allocator;
    DEVEX_VK_TRY(vmaCreateAllocator, &createInfo, &allocator.m_allocator);
    return allocator;
}

Allocator::Allocator(Allocator&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
{
}

Allocator& Allocator::operator=(Allocator&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
    }
    return *this;
}

Allocator::~Allocator()
{
    destroy();
}

void Allocator::destroy() noexcept
{
    if (m_allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

VmaAllocator Allocator::handle() const noexcept
{
    return m_allocator;
}

core::Result<Buffer> Buffer::create(const Allocator& allocator, const BufferConfig& config)
{
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = config.size,
        .usage = config.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (config.hostVisible)
    {
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    Buffer buffer;
    buffer.m_allocator = allocator.handle();
    buffer.m_size = config.size;
    VmaAllocationInfo allocated{};
    DEVEX_VK_TRY(vmaCreateBuffer, allocator.handle(), &bufferInfo, &allocationInfo,
                 &buffer.m_buffer, &buffer.m_allocation, &allocated);

    buffer.m_mapped = static_cast<std::byte*>(allocated.pMappedData);
    if ((config.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
    {
        const VkBufferDeviceAddressInfo addressInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer.m_buffer,
        };
        VmaAllocatorInfo allocatorInfo{};
        vmaGetAllocatorInfo(allocator.handle(), &allocatorInfo);
        buffer.m_deviceAddress = vkGetBufferDeviceAddress(allocatorInfo.device, &addressInfo);
    }
    return buffer;
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_size(std::exchange(other.m_size, 0))
    , m_mapped(std::exchange(other.m_mapped, nullptr))
    , m_deviceAddress(std::exchange(other.m_deviceAddress, 0))
{
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_size = std::exchange(other.m_size, 0);
        m_mapped = std::exchange(other.m_mapped, nullptr);
        m_deviceAddress = std::exchange(other.m_deviceAddress, 0);
    }
    return *this;
}

Buffer::~Buffer()
{
    destroy();
}

void Buffer::destroy() noexcept
{
    if (m_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mapped = nullptr;
    }
}

VkBuffer Buffer::handle() const noexcept
{
    return m_buffer;
}

VkDeviceSize Buffer::size() const noexcept
{
    return m_size;
}

std::span<std::byte> Buffer::mappedBytes() const noexcept
{
    return m_mapped != nullptr ? std::span<std::byte>(m_mapped, static_cast<std::size_t>(m_size))
                               : std::span<std::byte>();
}

VkDeviceAddress Buffer::deviceAddress() const noexcept
{
    return m_deviceAddress;
}

core::Result<Image> Image::create(const Device& device, const Allocator& allocator,
                                  const ImageConfig& config)
{
    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = config.format,
        .extent = {config.extent.width, config.extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = config.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    Image image;
    image.m_device = device.handle();
    image.m_allocator = allocator.handle();
    image.m_format = config.format;
    image.m_extent = config.extent;
    DEVEX_VK_TRY(vmaCreateImage, allocator.handle(), &imageInfo, &allocationInfo, &image.m_image,
                 &image.m_allocation, nullptr);

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.m_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = config.format,
        .subresourceRange =
            {
                .aspectMask = config.aspect,
                .levelCount = 1,
                .layerCount = 1,
            },
    };
    DEVEX_VK_TRY(vkCreateImageView, device.handle(), &viewInfo, nullptr, &image.m_view);
    return image;
}

Image::Image(Image&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_view(std::exchange(other.m_view, VK_NULL_HANDLE))
    , m_format(other.m_format)
    , m_extent(other.m_extent)
{
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
        m_format = other.m_format;
        m_extent = other.m_extent;
    }
    return *this;
}

Image::~Image()
{
    destroy();
}

void Image::destroy() noexcept
{
    if (m_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

VkImage Image::handle() const noexcept
{
    return m_image;
}

VkImageView Image::view() const noexcept
{
    return m_view;
}

VkFormat Image::format() const noexcept
{
    return m_format;
}

math::Extent2D Image::extent() const noexcept
{
    return m_extent;
}

} // namespace devex::render::vulkan
