#pragma once

#include "Device.hpp"
#include "Vulkan.hpp"

#include <devex/math/Math.hpp>
#include <devex/render/Renderer.hpp>

#include <cstdint>
#include <vector>

namespace devex::render::vulkan {

struct SwapchainConfig
{
    math::Extent2D windowPixelSize;
    PresentMode presentMode = PresentMode::Fifo;
    // Swapchain being replaced, which lets the driver reuse its resources.
    VkSwapchainKHR previous = VK_NULL_HANDLE;
};

// Owns a VkSwapchainKHR and a view on each of its images.
class Swapchain
{
public:
    [[nodiscard]] static core::Result<Swapchain> create(const Device& device, VkSurfaceKHR surface,
                                                        const SwapchainConfig& config);

    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    [[nodiscard]] VkSwapchainKHR handle() const noexcept;
    [[nodiscard]] VkFormat format() const noexcept;
    [[nodiscard]] math::Extent2D extent() const noexcept;
    [[nodiscard]] PresentMode presentMode() const noexcept;
    [[nodiscard]] std::uint32_t imageCount() const noexcept;
    [[nodiscard]] VkImage image(std::uint32_t index) const noexcept;
    [[nodiscard]] VkImageView imageView(std::uint32_t index) const noexcept;
    // The format and views the tools draw through: the UNORM counterpart of an sRGB format when the
    // device can view swapchain images in both, so that ImGui blends in display space as it
    // expects. Otherwise the swapchain's own format and views.
    [[nodiscard]] VkFormat toolsFormat() const noexcept;
    [[nodiscard]] VkImageView toolsImageView(std::uint32_t index) const noexcept;

private:
    Swapchain() = default;
    void destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    math::Extent2D m_extent;
    PresentMode m_presentMode = PresentMode::Fifo;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat m_toolsFormat = VK_FORMAT_UNDEFINED;
    // Empty when the tools draw through the image views.
    std::vector<VkImageView> m_toolsImageViews;
};

} // namespace devex::render::vulkan
