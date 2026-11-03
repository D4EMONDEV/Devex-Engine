#include "VolumetricFog.hpp"

#include "Commands.hpp"

#include <utility>

namespace devex::render::vulkan {

namespace {

constexpr VkFormat froxelFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr std::uint32_t setCount = 4;

struct FogPushConstants
{
    VkDeviceAddress scene = 0;
    VkDeviceAddress fog = 0;
};

std::uint32_t groups(std::uint32_t size)
{
    return (size + 7) / 8;
}

// Writes of a pass waited for by the passes that read them, on the GPU and in the frame.
void barrier(VkCommandBuffer commands, VkPipelineStageFlags2 sourceStages, VkAccessFlags2 sourceAccess, VkPipelineStageFlags2 destinationStages,
             VkAccessFlags2 destinationAccess)
{
    const VkMemoryBarrier2 memory{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = sourceStages,
        .srcAccessMask = sourceAccess,
        .dstStageMask = destinationStages,
        .dstAccessMask = destinationAccess,
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &memory,
    };
    vkCmdPipelineBarrier2(commands, &dependency);
}

} // namespace

core::Result<std::unique_ptr<VolumetricFogRenderer>> VolumetricFogRenderer::create(const Device& device, const Allocator& allocator,
                                                                                   UploadContext& upload, const DescriptorSets& descriptors,
                                                                                   const std::filesystem::path& shaderDirectory)
{
    std::unique_ptr<VolumetricFogRenderer> fog(new VolumetricFogRenderer(device));
    const VkDevice handle = device.handle();

    const std::array bindings{
        VkDescriptorSetLayoutBinding{.binding = 0,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        VkDescriptorSetLayoutBinding{.binding = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorSetLayout, handle, &layoutInfo, nullptr, &fog->m_layout);
    const std::array poolSizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = setCount},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = setCount},
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = setCount,
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorPool, handle, &poolInfo, nullptr, &fog->m_pool);

    // The global set for the light of the sky and the samplers, the frame set for the shadows of the
    // sun, and the fog's own.
    const std::array setLayouts{descriptors.globalLayout(), descriptors.frameLayout(), fog->m_layout};
    for (const auto& [entry, pipeline] :
         {std::pair<const char*, std::optional<Pipeline>*>{"lightFroxels", &fog->m_light}, {"integrateFroxels", &fog->m_integrate}})
    {
        core::Result<Pipeline> created = createComputePipeline(handle, {
                                                                           .shaderPath = shaderDirectory / "fog.spv",
                                                                           .entry = entry,
                                                                           .setLayouts = setLayouts,
                                                                           .pushConstantSize = sizeof(FogPushConstants),
                                                                       });
        if (!created)
        {
            return std::unexpected(created.error());
        }
        *pipeline = std::move(*created);
    }

    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (std::optional<Image>* target : {&fog->m_froxels[0], &fog->m_froxels[1], &fog->m_integrated})
    {
        core::Result<Image> created =
            Image::create(device, allocator, {.format = froxelFormat, .extent = {width, height}, .usage = usage, .layers = depth});
        if (!created)
        {
            return std::unexpected(created.error());
        }
        *target = std::move(*created);
    }
    // Empty until a frame with fog fills them, and in the general layout from now on.
    const std::array images{fog->m_froxels[0]->handle(), fog->m_froxels[1]->handle(), fog->m_integrated->handle()};
    if (core::Result<void> ready = upload.submit([&](VkCommandBuffer commands) {
            for (const VkImage each : images)
            {
                transitionImage(commands, each, ImageState::Undefined, ImageState::TransferDestination);
                const VkClearColorValue empty{};
                const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                    .levelCount = VK_REMAINING_MIP_LEVELS,
                                                    .layerCount = VK_REMAINING_ARRAY_LAYERS};
                vkCmdClearColorImage(commands, each, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &empty, 1, &range);
                transitionImage(commands, each, ImageState::TransferDestination, ImageState::ComputeStorage);
            }
        });
        !ready)
    {
        return std::unexpected(ready.error());
    }

    for (std::uint32_t turn = 0; turn < 2; ++turn)
    {
        fog->m_lightSets[turn] = fog->makeSet(fog->m_froxels[turn]->view(), fog->m_froxels[1 - turn]->view());
        fog->m_integrateSets[turn] = fog->makeSet(fog->m_integrated->view(), fog->m_froxels[turn]->view());
        if (fog->m_lightSets[turn] == VK_NULL_HANDLE || fog->m_integrateSets[turn] == VK_NULL_HANDLE)
        {
            return core::makeError(core::ErrorCode::Graphics, "the descriptor sets of the volumetric fog cannot be allocated");
        }
    }
    return fog;
}

VolumetricFogRenderer::VolumetricFogRenderer(const Device& device) noexcept
    : m_device(device)
{
}

VolumetricFogRenderer::~VolumetricFogRenderer()
{
    const VkDevice device = m_device.handle();
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
}

VkDescriptorSet VolumetricFogRenderer::makeSet(VkImageView written, VkImageView read)
{
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device.handle(), &allocateInfo, &set) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    const VkDescriptorImageInfo writtenInfo{.imageView = written, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo readInfo{.imageView = read, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const std::array writes{
        VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                             .dstSet = set,
                             .dstBinding = 0,
                             .descriptorCount = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                             .pImageInfo = &writtenInfo},
        VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                             .dstSet = set,
                             .dstBinding = 1,
                             .descriptorCount = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .pImageInfo = &readInfo},
    };
    vkUpdateDescriptorSets(m_device.handle(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return set;
}

void VolumetricFogRenderer::record(VkCommandBuffer commands, VkDescriptorSet global, VkDescriptorSet frame, VkDeviceAddress scene,
                                   VkDeviceAddress fog)
{
    m_current = 1 - m_current;
    // The frames before may still sample what this one writes.
    barrier(commands, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const FogPushConstants constants{.scene = scene, .fog = fog};
    const auto dispatch = [&](const Pipeline& pipeline, VkDescriptorSet set, std::uint32_t slices) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
        const std::array sets{global, frame, set};
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, static_cast<std::uint32_t>(sets.size()),
                                sets.data(), 0, nullptr);
        vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commands, groups(width), groups(height), slices);
    };
    dispatch(*m_light, m_lightSets[m_current], depth);
    barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    dispatch(*m_integrate, m_integrateSets[m_current], 1);
    // Sampled by the surfaces and the sky of this frame, and by the lighting of the next.
    barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

VkImageView VolumetricFogRenderer::integratedView() const noexcept
{
    return m_integrated->view();
}

} // namespace devex::render::vulkan
