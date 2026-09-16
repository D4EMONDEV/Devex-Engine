#include "Swapchain.hpp"

#include "../Selection.hpp"

#include <devex/core/Assert.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace devex::render::vulkan {
namespace {

[[nodiscard]] std::optional<PresentMode> fromVulkan(VkPresentModeKHR mode) noexcept
{
    switch (mode)
    {
    case VK_PRESENT_MODE_FIFO_KHR:
        return PresentMode::Fifo;
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return PresentMode::Mailbox;
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return PresentMode::Immediate;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] VkPresentModeKHR toVulkan(PresentMode mode) noexcept
{
    switch (mode)
    {
    case PresentMode::Fifo:
        return VK_PRESENT_MODE_FIFO_KHR;
    case PresentMode::Mailbox:
        return VK_PRESENT_MODE_MAILBOX_KHR;
    case PresentMode::Immediate:
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// An sRGB format lets the hardware encode the linear colors written by the renderer.
[[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    DEVEX_ASSERT(!formats.empty());
    for (const VkFormat preferred : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB})
    {
        const auto found = std::ranges::find_if(formats, [preferred](const VkSurfaceFormatKHR& format) {
            return format.format == preferred &&
                   format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (found != formats.end())
        {
            return *found;
        }
    }
    return formats.front();
}

} // namespace

core::Result<Swapchain> Swapchain::create(const Device& device, VkSurfaceKHR surface,
                                          const SwapchainConfig& config)
{
    const VkPhysicalDevice physicalDevice = device.physicalDevice();

    VkSurfaceCapabilitiesKHR capabilities{};
    DEVEX_VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, physicalDevice, surface, &capabilities);

    auto formats = enumerate<VkSurfaceFormatKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR",
                                                 vkGetPhysicalDeviceSurfaceFormatsKHR,
                                                 physicalDevice, surface);
    if (!formats)
    {
        return std::unexpected(formats.error());
    }
    if (formats->empty())
    {
        return core::makeError(core::ErrorCode::Unsupported, "the surface exposes no format");
    }

    auto vulkanPresentModes = enumerate<VkPresentModeKHR>(
        "vkGetPhysicalDeviceSurfacePresentModesKHR", vkGetPhysicalDeviceSurfacePresentModesKHR,
        physicalDevice, surface);
    if (!vulkanPresentModes)
    {
        return std::unexpected(vulkanPresentModes.error());
    }
    std::vector<PresentMode> presentModes;
    for (const VkPresentModeKHR mode : *vulkanPresentModes)
    {
        if (const std::optional<PresentMode> known = fromVulkan(mode))
        {
            presentModes.push_back(*known);
        }
    }

    const std::optional<math::Extent2D> surfaceExtent =
        capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max()
            ? std::nullopt
            : std::optional<math::Extent2D>(math::Extent2D{capabilities.currentExtent.width,
                                                           capabilities.currentExtent.height});
    const math::Extent2D extent = chooseSwapchainExtent(
        surfaceExtent, {capabilities.minImageExtent.width, capabilities.minImageExtent.height},
        {capabilities.maxImageExtent.width, capabilities.maxImageExtent.height},
        config.windowPixelSize);
    if (extent.width == 0 || extent.height == 0)
    {
        return core::makeError(core::ErrorCode::InvalidState, "the window has no drawable area");
    }

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(*formats);
    const PresentMode presentMode = choosePresentMode(config.presentMode, presentModes);

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = chooseImageCount(capabilities.minImageCount, capabilities.maxImageCount),
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = {extent.width, extent.height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = toVulkan(presentMode),
        .clipped = VK_TRUE,
        .oldSwapchain = config.previous,
    };

    Swapchain swapchain;
    swapchain.m_device = device.handle();
    swapchain.m_format = surfaceFormat.format;
    swapchain.m_extent = extent;
    swapchain.m_presentMode = presentMode;
    DEVEX_VK_TRY(vkCreateSwapchainKHR, device.handle(), &createInfo, nullptr, &swapchain.m_swapchain);

    auto images = enumerate<VkImage>("vkGetSwapchainImagesKHR", vkGetSwapchainImagesKHR,
                                     device.handle(), swapchain.m_swapchain);
    if (!images)
    {
        return std::unexpected(images.error());
    }
    swapchain.m_images = std::move(*images);

    for (const VkImage image : swapchain.m_images)
    {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surfaceFormat.format,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        };
        VkImageView view = VK_NULL_HANDLE;
        DEVEX_VK_TRY(vkCreateImageView, device.handle(), &viewInfo, nullptr, &view);
        swapchain.m_imageViews.push_back(view);
    }
    return swapchain;
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_swapchain(std::exchange(other.m_swapchain, VK_NULL_HANDLE))
    , m_format(other.m_format)
    , m_extent(other.m_extent)
    , m_presentMode(other.m_presentMode)
    , m_images(std::move(other.m_images))
    , m_imageViews(std::move(other.m_imageViews))
{
    other.m_imageViews.clear();
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_swapchain = std::exchange(other.m_swapchain, VK_NULL_HANDLE);
        m_format = other.m_format;
        m_extent = other.m_extent;
        m_presentMode = other.m_presentMode;
        m_images = std::move(other.m_images);
        m_imageViews = std::move(other.m_imageViews);
        other.m_imageViews.clear();
    }
    return *this;
}

Swapchain::~Swapchain()
{
    destroy();
}

void Swapchain::destroy() noexcept
{
    for (const VkImageView view : m_imageViews)
    {
        vkDestroyImageView(m_device, view, nullptr);
    }
    m_imageViews.clear();
    m_images.clear();
    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

VkSwapchainKHR Swapchain::handle() const noexcept
{
    return m_swapchain;
}

VkFormat Swapchain::format() const noexcept
{
    return m_format;
}

math::Extent2D Swapchain::extent() const noexcept
{
    return m_extent;
}

PresentMode Swapchain::presentMode() const noexcept
{
    return m_presentMode;
}

std::uint32_t Swapchain::imageCount() const noexcept
{
    return static_cast<std::uint32_t>(m_images.size());
}

VkImage Swapchain::image(std::uint32_t index) const noexcept
{
    DEVEX_ASSERT(index < m_images.size());
    return m_images[index];
}

VkImageView Swapchain::imageView(std::uint32_t index) const noexcept
{
    DEVEX_ASSERT(index < m_imageViews.size());
    return m_imageViews[index];
}

} // namespace devex::render::vulkan
