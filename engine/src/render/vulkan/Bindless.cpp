#include "Bindless.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace devex::render::vulkan {

core::Result<BindlessSet> BindlessSet::create(const Device& device, std::uint32_t textureCapacity)
{
    BindlessSet set;
    set.m_device = device.handle();
    set.m_textureCapacity = textureCapacity;

    const float anisotropy = std::min(device.maxSamplerAnisotropy(), 16.0f);
    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = anisotropy,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    DEVEX_VK_TRY(vkCreateSampler, set.m_device, &samplerInfo, nullptr, &set.m_sampler);

    const std::array bindings{
        VkDescriptorSetLayoutBinding{
            .binding = textureBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = textureCapacity,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        VkDescriptorSetLayoutBinding{
            .binding = samplerBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = &set.m_sampler,
        },
    };
    // Texture slots change while frames using the set are in flight, and unused slots stay empty.
    const std::array<VkDescriptorBindingFlags, 2> bindingFlags{
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        0,
    };
    const VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(bindingFlags.size()),
        .pBindingFlags = bindingFlags.data(),
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorSetLayout, set.m_device, &layoutInfo, nullptr, &set.m_layout);

    const std::array poolSizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = textureCapacity},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1},
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorPool, set.m_device, &poolInfo, nullptr, &set.m_pool);

    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = set.m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &set.m_layout,
    };
    DEVEX_VK_TRY(vkAllocateDescriptorSets, set.m_device, &allocateInfo, &set.m_set);
    return set;
}

BindlessSet::BindlessSet(BindlessSet&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_layout(std::exchange(other.m_layout, VK_NULL_HANDLE))
    , m_pool(std::exchange(other.m_pool, VK_NULL_HANDLE))
    , m_set(std::exchange(other.m_set, VK_NULL_HANDLE))
    , m_sampler(std::exchange(other.m_sampler, VK_NULL_HANDLE))
    , m_textureCapacity(std::exchange(other.m_textureCapacity, 0))
{
}

BindlessSet& BindlessSet::operator=(BindlessSet&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_pool = std::exchange(other.m_pool, VK_NULL_HANDLE);
        m_set = std::exchange(other.m_set, VK_NULL_HANDLE);
        m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
        m_textureCapacity = std::exchange(other.m_textureCapacity, 0);
    }
    return *this;
}

BindlessSet::~BindlessSet()
{
    destroy();
}

void BindlessSet::destroy() noexcept
{
    if (m_device != VK_NULL_HANDLE)
    {
        // Destroying the pool frees its set.
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_device = VK_NULL_HANDLE;
        m_pool = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_sampler = VK_NULL_HANDLE;
        m_set = VK_NULL_HANDLE;
    }
}

VkDescriptorSetLayout BindlessSet::layout() const noexcept
{
    return m_layout;
}

VkDescriptorSet BindlessSet::handle() const noexcept
{
    return m_set;
}

std::uint32_t BindlessSet::textureCapacity() const noexcept
{
    return m_textureCapacity;
}

void BindlessSet::setTexture(std::uint32_t slot, VkImageView view) noexcept
{
    const VkDescriptorImageInfo imageInfo{
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_set,
        .dstBinding = textureBinding,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

} // namespace devex::render::vulkan
