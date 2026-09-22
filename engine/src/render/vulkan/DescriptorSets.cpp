#include "DescriptorSets.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace devex::render::vulkan {
namespace {

constexpr VkShaderStageFlags shaderStages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
constexpr VkDescriptorBindingFlags updatedInFlight =
    VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

} // namespace

core::Result<DescriptorSets> DescriptorSets::create(const Device& device,
                                                    std::uint32_t textureCapacity,
                                                    std::uint32_t frameCount)
{
    DescriptorSets sets;
    sets.m_device = device.handle();
    sets.m_textureCapacity = textureCapacity;

    const float anisotropy = std::min(device.maxSamplerAnisotropy(), 16.0f);
    const VkSamplerCreateInfo materialSamplerInfo{
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
    DEVEX_VK_TRY(vkCreateSampler, sets.m_device, &materialSamplerInfo, nullptr, &sets.m_materialSampler);

    VkSamplerCreateInfo clampSamplerInfo = materialSamplerInfo;
    clampSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    clampSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    clampSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    clampSamplerInfo.anisotropyEnable = VK_FALSE;
    clampSamplerInfo.maxAnisotropy = 1.0f;
    DEVEX_VK_TRY(vkCreateSampler, sets.m_device, &clampSamplerInfo, nullptr, &sets.m_clampSampler);

    // Equirectangular maps wrap around horizontally but not over the poles.
    VkSamplerCreateInfo skySamplerInfo = clampSamplerInfo;
    skySamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    DEVEX_VK_TRY(vkCreateSampler, sets.m_device, &skySamplerInfo, nullptr, &sets.m_skySampler);

    // Bilinear comparisons filter shadow edges; outside the map, nothing is shadowed.
    VkSamplerCreateInfo shadowSamplerInfo = clampSamplerInfo;
    shadowSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadowSamplerInfo.compareEnable = VK_TRUE;
    shadowSamplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    shadowSamplerInfo.maxLod = 0.0f;
    DEVEX_VK_TRY(vkCreateSampler, sets.m_device, &shadowSamplerInfo, nullptr, &sets.m_shadowSampler);

    const auto image = [](std::uint32_t binding, std::uint32_t count = 1) {
        return VkDescriptorSetLayoutBinding{
            .binding = binding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = count,
            .stageFlags = shaderStages,
        };
    };
    const auto sampler = [](std::uint32_t binding, const VkSampler* immutable) {
        return VkDescriptorSetLayoutBinding{
            .binding = binding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = shaderStages,
            .pImmutableSamplers = immutable,
        };
    };

    const std::array globalBindings{
        image(texturesBinding, textureCapacity),
        sampler(materialSamplerBinding, &sets.m_materialSampler),
        image(specularBinding),
        image(irradianceBinding),
        image(brdfBinding),
        sampler(clampSamplerBinding, &sets.m_clampSampler),
        image(skyBinding),
        sampler(skySamplerBinding, &sets.m_skySampler),
    };
    const std::array<VkDescriptorBindingFlags, 8> globalFlags{
        updatedInFlight, 0, updatedInFlight, updatedInFlight, updatedInFlight, 0, updatedInFlight, 0,
    };
    const VkDescriptorSetLayoutBindingFlagsCreateInfo globalFlagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(globalFlags.size()),
        .pBindingFlags = globalFlags.data(),
    };
    const VkDescriptorSetLayoutCreateInfo globalLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &globalFlagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<std::uint32_t>(globalBindings.size()),
        .pBindings = globalBindings.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorSetLayout, sets.m_device, &globalLayoutInfo, nullptr,
                 &sets.m_globalLayout);

    const std::array frameBindings{
        image(shadowMapBinding),
        sampler(shadowSamplerBinding, &sets.m_shadowSampler),
        image(sceneColorBinding),
        image(selectionMaskBinding),
        image(velocityBinding),
        image(normalBinding),
        image(ambientOcclusionBinding),
        image(historyBinding),
        image(depthBinding),
        image(resolvedBinding),
        VkDescriptorSetLayoutBinding{
            .binding = bloomBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = bloomLevels,
            .stageFlags = VK_SHADER_STAGE_ALL,
        },
    };
    const VkDescriptorSetLayoutCreateInfo frameLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(frameBindings.size()),
        .pBindings = frameBindings.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorSetLayout, sets.m_device, &frameLayoutInfo, nullptr,
                 &sets.m_frameLayout);

    const std::array globalPoolSizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = textureCapacity + 4},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 3},
    };
    const VkDescriptorPoolCreateInfo globalPoolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<std::uint32_t>(globalPoolSizes.size()),
        .pPoolSizes = globalPoolSizes.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorPool, sets.m_device, &globalPoolInfo, nullptr, &sets.m_globalPool);

    const std::array framePoolSizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = (9 + bloomLevels) * frameCount},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = frameCount},
    };
    const VkDescriptorPoolCreateInfo framePoolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = frameCount,
        .poolSizeCount = static_cast<std::uint32_t>(framePoolSizes.size()),
        .pPoolSizes = framePoolSizes.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorPool, sets.m_device, &framePoolInfo, nullptr, &sets.m_framePool);

    const VkDescriptorSetAllocateInfo globalAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = sets.m_globalPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &sets.m_globalLayout,
    };
    DEVEX_VK_TRY(vkAllocateDescriptorSets, sets.m_device, &globalAllocateInfo, &sets.m_global);

    const std::vector<VkDescriptorSetLayout> frameLayouts(frameCount, sets.m_frameLayout);
    sets.m_frames.resize(frameCount);
    const VkDescriptorSetAllocateInfo frameAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = sets.m_framePool,
        .descriptorSetCount = frameCount,
        .pSetLayouts = frameLayouts.data(),
    };
    DEVEX_VK_TRY(vkAllocateDescriptorSets, sets.m_device, &frameAllocateInfo, sets.m_frames.data());
    return sets;
}

DescriptorSets::DescriptorSets(DescriptorSets&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_materialSampler(std::exchange(other.m_materialSampler, VK_NULL_HANDLE))
    , m_clampSampler(std::exchange(other.m_clampSampler, VK_NULL_HANDLE))
    , m_skySampler(std::exchange(other.m_skySampler, VK_NULL_HANDLE))
    , m_shadowSampler(std::exchange(other.m_shadowSampler, VK_NULL_HANDLE))
    , m_globalLayout(std::exchange(other.m_globalLayout, VK_NULL_HANDLE))
    , m_frameLayout(std::exchange(other.m_frameLayout, VK_NULL_HANDLE))
    , m_globalPool(std::exchange(other.m_globalPool, VK_NULL_HANDLE))
    , m_framePool(std::exchange(other.m_framePool, VK_NULL_HANDLE))
    , m_global(std::exchange(other.m_global, VK_NULL_HANDLE))
    , m_frames(std::exchange(other.m_frames, {}))
    , m_textureCapacity(std::exchange(other.m_textureCapacity, 0))
{
}

DescriptorSets& DescriptorSets::operator=(DescriptorSets&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_materialSampler = std::exchange(other.m_materialSampler, VK_NULL_HANDLE);
        m_clampSampler = std::exchange(other.m_clampSampler, VK_NULL_HANDLE);
        m_skySampler = std::exchange(other.m_skySampler, VK_NULL_HANDLE);
        m_shadowSampler = std::exchange(other.m_shadowSampler, VK_NULL_HANDLE);
        m_globalLayout = std::exchange(other.m_globalLayout, VK_NULL_HANDLE);
        m_frameLayout = std::exchange(other.m_frameLayout, VK_NULL_HANDLE);
        m_globalPool = std::exchange(other.m_globalPool, VK_NULL_HANDLE);
        m_framePool = std::exchange(other.m_framePool, VK_NULL_HANDLE);
        m_global = std::exchange(other.m_global, VK_NULL_HANDLE);
        m_frames = std::exchange(other.m_frames, {});
        m_textureCapacity = std::exchange(other.m_textureCapacity, 0);
    }
    return *this;
}

DescriptorSets::~DescriptorSets()
{
    destroy();
}

void DescriptorSets::destroy() noexcept
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }
    // Destroying the pools frees their sets.
    vkDestroyDescriptorPool(m_device, m_globalPool, nullptr);
    vkDestroyDescriptorPool(m_device, m_framePool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_globalLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_frameLayout, nullptr);
    for (const VkSampler sampler : {m_materialSampler, m_clampSampler, m_skySampler, m_shadowSampler})
    {
        vkDestroySampler(m_device, sampler, nullptr);
    }
    m_frames.clear();
    m_device = VK_NULL_HANDLE;
}

VkDescriptorSetLayout DescriptorSets::globalLayout() const noexcept
{
    return m_globalLayout;
}

VkDescriptorSetLayout DescriptorSets::frameLayout() const noexcept
{
    return m_frameLayout;
}

VkDescriptorSet DescriptorSets::global() const noexcept
{
    return m_global;
}

VkDescriptorSet DescriptorSets::frame(std::uint32_t index) const noexcept
{
    return m_frames[index];
}

std::uint32_t DescriptorSets::textureCapacity() const noexcept
{
    return m_textureCapacity;
}

void DescriptorSets::writeImage(VkDescriptorSet set, std::uint32_t binding, std::uint32_t element,
                                VkImageView view, VkImageLayout layout) const noexcept
{
    const VkDescriptorImageInfo imageInfo{
        .imageView = view,
        .imageLayout = layout,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .dstArrayElement = element,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void DescriptorSets::setTexture(std::uint32_t slot, VkImageView view) noexcept
{
    writeImage(m_global, texturesBinding, slot, view);
}

void DescriptorSets::setEnvironment(VkImageView specular, VkImageView irradiance,
                                    VkImageView sky) noexcept
{
    writeImage(m_global, specularBinding, 0, specular);
    writeImage(m_global, irradianceBinding, 0, irradiance);
    writeImage(m_global, skyBinding, 0, sky);
}

void DescriptorSets::setBrdfLut(VkImageView view) noexcept
{
    writeImage(m_global, brdfBinding, 0, view);
}

void DescriptorSets::setFrameImages(std::uint32_t frame, const FrameImages& images) noexcept
{
    writeImage(m_frames[frame], shadowMapBinding, 0, images.shadowMap);
    writeImage(m_frames[frame], sceneColorBinding, 0, images.sceneColor);
    writeImage(m_frames[frame], selectionMaskBinding, 0, images.selectionMask);
    writeImage(m_frames[frame], velocityBinding, 0, images.velocity);
    writeImage(m_frames[frame], normalBinding, 0, images.normal);
    writeImage(m_frames[frame], ambientOcclusionBinding, 0, images.ambientOcclusion);
    writeImage(m_frames[frame], historyBinding, 0, images.history);
    writeImage(m_frames[frame], depthBinding, 0, images.depth);
    writeImage(m_frames[frame], resolvedBinding, 0, images.resolved);
    for (std::uint32_t level = 0; level < bloomLevels; ++level)
    {
        // The chain is read and drawn into within the same passes, so it stays in one layout.
        writeImage(m_frames[frame], bloomBinding, level, images.bloom[level],
                   VK_IMAGE_LAYOUT_GENERAL);
    }
}

} // namespace devex::render::vulkan
