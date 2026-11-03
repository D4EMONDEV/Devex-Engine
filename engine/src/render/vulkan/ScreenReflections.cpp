#include "ScreenReflections.hpp"

#include "Commands.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <utility>

namespace devex::render::vulkan {

namespace {

constexpr VkFormat reflectionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
// Rough surfaces fade out of the reflections before they would need anything smaller.
constexpr std::uint32_t maximumLevels = 7;

struct ReflectionPushConstants
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

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

core::Result<std::unique_ptr<ScreenReflectionRenderer>> ScreenReflectionRenderer::create(const Device& device, const Allocator& allocator,
                                                                                         UploadContext& upload, const DescriptorSets& descriptors,
                                                                                         const std::filesystem::path& shaderDirectory)
{
    std::unique_ptr<ScreenReflectionRenderer> reflections(new ScreenReflectionRenderer(device, allocator, upload));
    const VkDevice handle = device.handle();

    const VkDescriptorSetLayoutBinding binding{.binding = 0,
                                               .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                               .descriptorCount = 1,
                                               .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    DEVEX_VK_TRY(vkCreateDescriptorSetLayout, handle, &layoutInfo, nullptr, &reflections->m_layout);
    const VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1};
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    DEVEX_VK_TRY(vkCreateDescriptorPool, handle, &poolInfo, nullptr, &reflections->m_pool);
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = reflections->m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &reflections->m_layout,
    };
    DEVEX_VK_TRY(vkAllocateDescriptorSets, handle, &allocateInfo, &reflections->m_set);

    // The global set for the samplers, the frame set for the scene, and the image of its own.
    const std::array setLayouts{descriptors.globalLayout(), descriptors.frameLayout(), reflections->m_layout};
    core::Result<Pipeline> shrink = createComputePipeline(handle, {
                                                                      .shaderPath = shaderDirectory / "reflections.spv",
                                                                      .entry = "shrinkScene",
                                                                      .setLayouts = setLayouts,
                                                                      .pushConstantSize = sizeof(ReflectionPushConstants),
                                                                  });
    if (!shrink)
    {
        return std::unexpected(shrink.error());
    }
    reflections->m_shrink = std::move(*shrink);

    // The frames name the image from the start, whether they reflect or not.
    if (core::Result<void> ensured = reflections->ensure({2, 2}); !ensured)
    {
        return std::unexpected(ensured.error());
    }
    return reflections;
}

ScreenReflectionRenderer::ScreenReflectionRenderer(const Device& device, const Allocator& allocator, UploadContext& upload) noexcept
    : m_device(device)
    , m_allocator(allocator)
    , m_upload(upload)
{
}

ScreenReflectionRenderer::~ScreenReflectionRenderer()
{
    const VkDevice device = m_device.handle();
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
}

core::Result<void> ScreenReflectionRenderer::ensure(math::Extent2D sceneExtent)
{
    if (m_image && m_sceneExtent == sceneExtent)
    {
        return {};
    }
    // Frames in flight may still read the previous image; it goes once their frames are done.
    DEVEX_VK_TRY(vkDeviceWaitIdle, m_device.handle());
    const math::Extent2D extent{std::max(sceneExtent.width / 2, 1u), std::max(sceneExtent.height / 2, 1u)};
    const std::uint32_t levels = std::min(static_cast<std::uint32_t>(std::bit_width(std::max(extent.width, extent.height))), maximumLevels);
    core::Result<Image> image =
        Image::create(m_device, m_allocator,
                      {
                          .format = reflectionFormat,
                          .extent = extent,
                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          .mipLevels = levels,
                      });
    if (!image)
    {
        return std::unexpected(image.error());
    }
    m_image = std::move(*image);
    m_sceneExtent = sceneExtent;
    m_filled = false;
    const VkImage target = m_image->handle();
    if (core::Result<void> ready = m_upload.submit([&](VkCommandBuffer commands) {
            transitionImage(commands, target, ImageState::Undefined, ImageState::TransferDestination);
            const VkClearColorValue empty{};
            const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                .levelCount = VK_REMAINING_MIP_LEVELS,
                                                .layerCount = VK_REMAINING_ARRAY_LAYERS};
            vkCmdClearColorImage(commands, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &empty, 1, &range);
            transitionImage(commands, target, ImageState::TransferDestination, ImageState::ComputeStorage);
        });
        !ready)
    {
        return std::unexpected(ready.error());
    }
    const VkDescriptorImageInfo imageInfo{.imageView = m_image->subview(VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1),
                                          .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .dstSet = m_set,
                                     .dstBinding = 0,
                                     .descriptorCount = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                     .pImageInfo = &imageInfo};
    vkUpdateDescriptorSets(m_device.handle(), 1, &write, 0, nullptr);
    return {};
}

void ScreenReflectionRenderer::record(VkCommandBuffer commands, VkDescriptorSet global, VkDescriptorSet frame, float exposure)
{
    // The surfaces of this frame read what the previous one left.
    barrier(commands, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const math::Extent2D extent = m_image->extent();
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_shrink->handle());
    const std::array sets{global, frame, m_set};
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_shrink->layout(), 0, static_cast<std::uint32_t>(sets.size()), sets.data(), 0,
                            nullptr);
    const ReflectionPushConstants constants{.width = extent.width, .height = extent.height};
    vkCmdPushConstants(commands, m_shrink->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commands, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);

    // Each level from the one before, which is enough of a blur for a surface already rough.
    barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const auto size = [](std::uint32_t pixels, std::uint32_t level) { return static_cast<std::int32_t>(std::max(pixels >> level, 1u)); };
    for (std::uint32_t level = 1; level < m_image->mipLevels(); ++level)
    {
        const VkImageBlit region{
            .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level - 1, .layerCount = 1},
            .srcOffsets = {{}, {size(extent.width, level - 1), size(extent.height, level - 1), 1}},
            .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .layerCount = 1},
            .dstOffsets = {{}, {size(extent.width, level), size(extent.height, level), 1}},
        };
        vkCmdBlitImage(commands, m_image->handle(), VK_IMAGE_LAYOUT_GENERAL, m_image->handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region, VK_FILTER_LINEAR);
        barrier(commands, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    }
    // For the surfaces of the next frame.
    barrier(commands, VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    m_filled = true;
    m_exposure = exposure;
}

void ScreenReflectionRenderer::forget() noexcept
{
    m_filled = false;
}

bool ScreenReflectionRenderer::filled() const noexcept
{
    return m_filled;
}

float ScreenReflectionRenderer::exposure() const noexcept
{
    return m_exposure;
}

std::uint32_t ScreenReflectionRenderer::levels() const noexcept
{
    return m_image->mipLevels();
}

VkImageView ScreenReflectionRenderer::view() const noexcept
{
    return m_image->view();
}

} // namespace devex::render::vulkan
