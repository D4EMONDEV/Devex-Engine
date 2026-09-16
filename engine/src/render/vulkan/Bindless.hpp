#pragma once

#include "Device.hpp"
#include "Vulkan.hpp"

#include <cstdint>

namespace devex::render::vulkan {

// The descriptor set shared by every draw: all textures in one array indexed by materials, and
// the sampler they are read with. Slots may be rewritten while frames using the set are in flight.
class BindlessSet
{
public:
    static constexpr std::uint32_t textureBinding = 0;
    static constexpr std::uint32_t samplerBinding = 1;

    [[nodiscard]] static core::Result<BindlessSet> create(const Device& device,
                                                          std::uint32_t textureCapacity);

    BindlessSet(BindlessSet&& other) noexcept;
    BindlessSet& operator=(BindlessSet&& other) noexcept;
    ~BindlessSet();

    BindlessSet(const BindlessSet&) = delete;
    BindlessSet& operator=(const BindlessSet&) = delete;

    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept;
    [[nodiscard]] VkDescriptorSet handle() const noexcept;
    [[nodiscard]] std::uint32_t textureCapacity() const noexcept;

    // Points the slot at an image view in the shader read-only layout.
    void setTexture(std::uint32_t slot, VkImageView view) noexcept;

private:
    BindlessSet() = default;
    void destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    std::uint32_t m_textureCapacity = 0;
};

} // namespace devex::render::vulkan
