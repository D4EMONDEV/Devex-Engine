#pragma once

#include "Vulkan.hpp"

namespace devex::render::vulkan {

// States an image goes through during a frame. Each state carries the pipeline stages at which it
// is used, so that the synchronization with acquisition and presentation stays explicit.
enum class ImageState
{
    // Contents are irrelevant and will be overwritten.
    Undefined,
    // A swapchain image just acquired, usable once the acquire semaphore is signaled.
    AcquiredBackbuffer,
    ColorAttachment,
    DepthAttachment,
    // Receiving the depth resolved from a multisampled depth attachment when rendering ends.
    DepthResolveAttachment,
    Present,
    // Receiving data copied from a buffer.
    TransferDestination,
    // Copied to a buffer.
    TransferSource,
    // Sampled by fragment shaders.
    ShaderReadOnly,
    // Sampled by compute shaders.
    ComputeReadOnly,
    // Written by compute shaders as a storage image.
    ComputeStorage,
    // Sampled while the image stays in the general layout, which lets other levels of the same
    // chain be drawn into at the same time: the bloom reads one image while it writes another,
    // and both are named by the same descriptor.
    GeneralRead,
    // Drawn into while staying in the general layout.
    GeneralAttachment,
};

// True for states that only read the image, which need no barrier between each other when their
// layouts match.
[[nodiscard]] bool isReadOnly(ImageState state) noexcept;
[[nodiscard]] VkImageLayout layoutOf(ImageState state) noexcept;

// Records a synchronization2 barrier between two states of every level and layer of an image.
void transitionImage(VkCommandBuffer commandBuffer, VkImage image, ImageState from, ImageState to,
                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

// The aspect of images of the format: depth for depth formats, color otherwise.
[[nodiscard]] VkImageAspectFlags aspectOf(VkFormat format) noexcept;

} // namespace devex::render::vulkan
