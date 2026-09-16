#pragma once

#include "Vulkan.hpp"

namespace devex::render::vulkan {

// States a swapchain image goes through during a frame.
enum class BackbufferState
{
    // Just acquired: contents undefined, usable once the acquire semaphore is signaled.
    Acquired,
    ColorAttachment,
    Present,
};

// Records a synchronization2 barrier between two backbuffer states. Each state carries the
// pipeline stages at which it is used, so that the synchronization with image acquisition and
// presentation is explicit.
void transitionBackbuffer(VkCommandBuffer commandBuffer, VkImage image, BackbufferState from,
                          BackbufferState to);

} // namespace devex::render::vulkan
