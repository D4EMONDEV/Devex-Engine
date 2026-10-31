// Current-frame, two-phase occlusion. Older visibility only schedules the seed depth prepass;
// every deferred instance is tested against current depth before being left out.
#include "VulkanRenderer.hpp"

#include <algorithm>
#include <cstring>

namespace devex::render::vulkan
{
namespace
{

void barrier(VkCommandBuffer commands, VkPipelineStageFlags2 source, VkAccessFlags2 sourceAccess,
             VkPipelineStageFlags2 target, VkAccessFlags2 targetAccess)
{
    const VkMemoryBarrier2 memory{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                  .srcStageMask = source,
                                  .srcAccessMask = sourceAccess,
                                  .dstStageMask = target,
                                  .dstAccessMask = targetAccess};
    const VkDependencyInfo dependency{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                      .memoryBarrierCount = 1,
                                      .pMemoryBarriers = &memory};
    vkCmdPipelineBarrier2(commands, &dependency);
}

} // namespace

void VulkanRenderer::readCulling(FrameContext& frame)
{
    m_lastGpuTested = static_cast<std::uint32_t>(frame.cullKeys.size());
    m_lastGpuFrustumCulled = m_lastGpuOccluded = 0;
    frame.occludedKeys.clear();
    if (!frame.cullKeys.empty() && frame.cullReadback)
    {
        frame.cullReadback->invalidate();
        const auto bytes = frame.cullReadback->mappedBytes();
        for (std::size_t index = 0; index < frame.cullKeys.size(); ++index)
        {
            std::uint32_t state = 0;
            std::memcpy(&state, bytes.data() + index * sizeof(state), sizeof(state));
            m_lastGpuFrustumCulled += state == 0 ? 1u : 0u;
            m_lastGpuOccluded += state == 1 ? 1u : 0u;
            if (state == 1 && frame.cullKeys[index] != 0)
            {
                frame.occludedKeys.push_back(frame.cullKeys[index]);
            }
        }
    }
    // Reuse one flat allocation rather than allocating a hash node per hidden instance per frame.
    std::ranges::sort(frame.occludedKeys);
    frame.cullKeys.clear();
}

void VulkanRenderer::recordDepthPyramid(VkCommandBuffer commands, const FrameContext& frame,
                                        std::uint32_t frameSlot) const
{
    const std::array sets{m_descriptors->global(), frameDescriptors(frameSlot)};
    const Pipeline& pipeline = *m_depthReducePipeline;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 2,
                            sets.data(), 0, nullptr);
    DepthReducePushConstants constants{.pyramid = frame.depthPyramid->deviceAddress(),
                                       .sourceWidth = m_sceneExtent.width,
                                       .sourceHeight = m_sceneExtent.height,
                                       .firstLevel = 1};
    do
    {
        const std::uint32_t width = (constants.sourceWidth + 1) / 2;
        const std::uint32_t height = (constants.sourceHeight + 1) / 2;
        vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(constants), &constants);
        vkCmdDispatch(commands, (width + 7) / 8, (height + 7) / 8, 1);
        barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        constants.sourceOffset = constants.targetOffset;
        constants.targetOffset += width * height;
        constants.sourceWidth = width;
        constants.sourceHeight = height;
        constants.firstLevel = 0;
    } while (constants.sourceWidth > 1 || constants.sourceHeight > 1);
}

void VulkanRenderer::recordOcclusion(VkCommandBuffer commands, const FrameContext& frame,
                                     std::uint32_t frameSlot) const
{
    // Reset only the camera's row. Shadows retain their own visibility, including hidden casters.
    barrier(commands,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const VkBufferCopy copy{.size = m_cullBatches.size() * sizeof(VkDrawIndexedIndirectCommand)};
    vkCmdCopyBuffer(commands, frame.cullCommandUpload->handle(), frame.cullCommands->handle(), 1,
                    &copy);
    barrier(commands, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const Pipeline& pipeline = *m_occlusionPipeline;
    const std::array sets{m_descriptors->global(), frameDescriptors(frameSlot)};
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 2,
                            sets.data(), 0, nullptr);
    const OcclusionPushConstants constants{.instances = frame.cullInstances->deviceAddress(),
                                           .views = frame.cullViews->deviceAddress(),
                                           .commands = frame.cullCommands->deviceAddress(),
                                           .visible = frame.cullVisible->deviceAddress(),
                                           .states = frame.cullStates->deviceAddress(),
                                           .pyramid = frame.depthPyramid->deviceAddress(),
                                           .scene = frame.sceneData->deviceAddress(),
                                           .instanceCount = m_culledCount,
                                           .batchCount =
                                               static_cast<std::uint32_t>(m_cullBatches.size()),
                                           .deferredView = m_deferredCullView,
                                           .width = m_sceneExtent.width,
                                           .height = m_sceneExtent.height};
    vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDispatch(commands, (m_culledCount + 63) / 64, 1, 1);
    barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

void VulkanRenderer::recordCullingReadback(VkCommandBuffer commands,
                                           const FrameContext& frame) const
{
    barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferCopy copy{.size = m_culledCount * sizeof(std::uint32_t)};
    vkCmdCopyBuffer(commands, frame.cullStates->handle(), frame.cullReadback->handle(), 1, &copy);
    barrier(commands, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
}

} // namespace devex::render::vulkan
