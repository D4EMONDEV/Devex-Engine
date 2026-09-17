#include "Vulkan.hpp"

// volk must be included first so that VMA exposes vmaImportVulkanFunctionsFromVolk.
#define VMA_IMPLEMENTATION
#include "Memory.hpp"

#include "Commands.hpp"

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
        .flags = config.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : VkImageCreateFlags{0},
        .imageType = VK_IMAGE_TYPE_2D,
        .format = config.format,
        .extent = {config.extent.width, config.extent.height, 1},
        .mipLevels = config.mipLevels,
        .arrayLayers = config.layers,
        .samples = config.samples,
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
    image.m_config = config;
    DEVEX_VK_TRY(vmaCreateImage, allocator.handle(), &imageInfo, &allocationInfo, &image.m_image,
                 &image.m_allocation, nullptr);

    const VkImageViewType type = config.cube          ? VK_IMAGE_VIEW_TYPE_CUBE
                                 : config.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                     : VK_IMAGE_VIEW_TYPE_2D;
    image.m_view = image.createView(type, 0, config.mipLevels, 0, config.layers);
    if (image.m_view == VK_NULL_HANDLE)
    {
        return core::makeError(core::ErrorCode::Graphics, "cannot create the image view");
    }
    return image;
}

Image::Image(Image&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_view(std::exchange(other.m_view, VK_NULL_HANDLE))
    , m_config(other.m_config)
    , m_subviews(std::move(other.m_subviews))
{
    other.m_subviews.clear();
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
        m_config = other.m_config;
        m_subviews = std::move(other.m_subviews);
        other.m_subviews.clear();
    }
    return *this;
}

Image::~Image()
{
    destroy();
}

void Image::destroy() noexcept
{
    for (const Subview& subview : m_subviews)
    {
        vkDestroyImageView(m_device, subview.view, nullptr);
    }
    m_subviews.clear();
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

VkImageView Image::createView(VkImageViewType type, std::uint32_t baseMip, std::uint32_t mipCount,
                              std::uint32_t baseLayer, std::uint32_t layerCount) const
{
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType = type,
        .format = m_config.format,
        .subresourceRange =
            {
                .aspectMask = aspectOf(m_config.format),
                .baseMipLevel = baseMip,
                .levelCount = mipCount,
                .baseArrayLayer = baseLayer,
                .layerCount = layerCount,
            },
    };
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &view) < VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return view;
}

VkImageView Image::subview(VkImageViewType type, std::uint32_t baseMip, std::uint32_t mipCount,
                           std::uint32_t baseLayer, std::uint32_t layerCount) const
{
    for (const Subview& subview : m_subviews)
    {
        if (subview.type == type && subview.baseMip == baseMip && subview.mipCount == mipCount &&
            subview.baseLayer == baseLayer && subview.layerCount == layerCount)
        {
            return subview.view;
        }
    }
    const VkImageView view = createView(type, baseMip, mipCount, baseLayer, layerCount);
    if (view != VK_NULL_HANDLE)
    {
        m_subviews.push_back({type, baseMip, mipCount, baseLayer, layerCount, view});
    }
    return view;
}

VkImage Image::handle() const noexcept
{
    return m_image;
}

VkImageView Image::view() const noexcept
{
    return m_view;
}

const ImageConfig& Image::config() const noexcept
{
    return m_config;
}

VkFormat Image::format() const noexcept
{
    return m_config.format;
}

math::Extent2D Image::extent() const noexcept
{
    return m_config.extent;
}

std::uint32_t Image::mipLevels() const noexcept
{
    return m_config.mipLevels;
}

} // namespace devex::render::vulkan
