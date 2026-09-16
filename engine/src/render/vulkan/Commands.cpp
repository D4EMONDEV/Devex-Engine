#include "Commands.hpp"

namespace devex::render::vulkan {
namespace {

struct StateUsage
{
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = 0;
};

[[nodiscard]] StateUsage usageOf(ImageState state) noexcept
{
    switch (state)
    {
    case ImageState::Undefined:
        return {};
    case ImageState::AcquiredBackbuffer:
        // The submission waits for the acquire semaphore at the color attachment output stage.
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0};
    case ImageState::ColorAttachment:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    case ImageState::DepthAttachment:
        return {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case ImageState::Present:
        // Presentation waits for the semaphore signaled once all graphics stages completed.
        return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0};
    case ImageState::TransferDestination:
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case ImageState::ShaderReadOnly:
        return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    }
    return {};
}

} // namespace

void transitionImage(VkCommandBuffer commandBuffer, VkImage image, ImageState from, ImageState to,
                     std::uint32_t mipLevels)
{
    const StateUsage source = usageOf(from);
    const StateUsage destination = usageOf(to);
    const bool isDepth = from == ImageState::DepthAttachment || to == ImageState::DepthAttachment;

    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = source.stage,
        .srcAccessMask = source.access,
        .dstStageMask = destination.stage,
        .dstAccessMask = destination.access,
        .oldLayout = source.layout,
        .newLayout = destination.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
            {
                .aspectMask =
                    static_cast<VkImageAspectFlags>(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                            : VK_IMAGE_ASPECT_COLOR_BIT),
                .levelCount = mipLevels,
                .layerCount = 1,
            },
    };
    const VkDependencyInfo dependencies{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencies);
}

} // namespace devex::render::vulkan
