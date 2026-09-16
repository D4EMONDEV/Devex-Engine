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
    Present,
    // Receiving data copied from a buffer.
    TransferDestination,
    // Sampled by fragment shaders.
    ShaderReadOnly,
};

// Records a synchronization2 barrier between two states of an image and its first mip levels.
void transitionImage(VkCommandBuffer commandBuffer, VkImage image, ImageState from, ImageState to,
                     std::uint32_t mipLevels = 1);

} // namespace devex::render::vulkan
