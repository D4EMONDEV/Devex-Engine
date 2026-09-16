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
};

// Records a synchronization2 barrier between two states of an image.
void transitionImage(VkCommandBuffer commandBuffer, VkImage image, ImageState from,
                     ImageState to);

} // namespace devex::render::vulkan
