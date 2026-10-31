// The standard skinned meshes prepare one pose per mesh instance, reused by all its submeshes,
// views within that render and passes. Each frame context owns its output until its fence ends.
#include "VulkanRenderer.hpp"

#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace devex::render::vulkan
{

core::Result<void> VulkanRenderer::prepareSkinning(FrameContext& frame)
{
    DEVEX_PROFILE_SCOPE("Skinning data");
    m_skinJobs.clear();
    m_skinInstances.assign(m_world.meshes.size(), noCullView);
    m_skinSources.clear();
    m_skinLookup.clear();
    m_skinnedVertexCount = 0;
    if (!m_gpuCulling || !m_gpuSkinning)
        return {};

    const auto keyOf = [&](std::uint32_t index)
    {
        const MeshInstance& instance = m_world.meshes[index];
        return std::array{instance.mesh.index, instance.objectId, instance.firstBone,
                          instance.objectId == 0 ? index : 0u};
    };
    for (std::uint32_t index = 0; index < m_world.meshes.size(); ++index)
    {
        const MeshInstance& instance = m_world.meshes[index];
        if (instance.boneCount == 0 || !isGpuDriven(instance))
            continue;
        const GpuMesh& mesh = *m_meshes.find(instance.mesh);
        if (!mesh.skin)
            continue;
        const PreviousInstance* previous = previousInstance(index);
        const VkDeviceAddress bones =
            frame.bones->deviceAddress() + instance.firstBone * sizeof(math::Mat4);
        const bool previousSkin = previous != nullptr && previous->skinned &&
                                  previous->boneCount >= mesh.jointCount &&
                                  static_cast<std::size_t>(previous->firstBone) + mesh.jointCount <=
                                      m_previousBoneMatrices.size();
        const VkDeviceAddress previousBones =
            previousSkin
                ? frame.previousBones->deviceAddress() + previous->firstBone * sizeof(math::Mat4)
                : bones;
        const math::Mat4 previousWorld =
            previous != nullptr ? previous->transform : instance.transform;
        // Submeshes may share a pose only when both the weighted and unweighted vertices, now
        // and previously, are identical. Picking IDs alone need not identify one transform.
        const auto nextJob = static_cast<std::uint32_t>(m_skinJobs.size());
        if (m_skinLookup.empty())
        {
            // Open addressing at at most half occupancy: one retained allocation, with no node
            // allocated or freed for every pose every frame. Static scenes never create it.
            m_skinLookup.assign(std::bit_ceil(std::max<std::size_t>(m_world.meshes.size() * 2, 1)),
                                noCullView);
        }
        const auto key = keyOf(index);
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto word : key)
            hash = (hash ^ word) * 1099511628211ull;
        hash ^= hash >> 32;
        const auto mask = m_skinLookup.size() - 1;
        std::size_t slot = static_cast<std::size_t>(hash) & mask;
        while (m_skinLookup[slot] != noCullView)
        {
            const auto candidate = m_skinLookup[slot];
            const GpuSkinJob& existing = m_skinJobs[candidate];
            if (keyOf(m_skinSources[candidate]) == key && existing.world == instance.transform &&
                existing.previousWorld == previousWorld && existing.previousBones == previousBones)
            {
                m_skinInstances[index] = candidate;
                break;
            }
            slot = (slot + 1) & mask;
        }
        if (m_skinInstances[index] != noCullView)
            continue;
        m_skinLookup[slot] = nextJob;
        m_skinSources.push_back(index);
        m_skinInstances[index] = nextJob;
        const auto vertexCount =
            static_cast<std::uint32_t>(mesh.vertices.size() / sizeof(GpuVertex));
        m_skinJobs.push_back({.vertices = mesh.vertices.deviceAddress(),
                              .skin = mesh.skin->deviceAddress(),
                              .bones = bones,
                              .previousBones = previousBones,
                              .output = m_skinnedVertexCount * sizeof(GpuSkinnedVertex),
                              .bounds = nextJob * sizeof(GpuSkinBounds),
                              .world = instance.transform,
                              .previousWorld = previousWorld,
                              .vertexCount = vertexCount});
        m_skinnedVertexCount += vertexCount;
    }
    if (m_skinJobs.empty())
        return {};

    constexpr VkBufferUsageFlags storage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (auto result = ensureHostBuffer(frame.skinJobs, m_skinJobs.size() * sizeof(GpuSkinJob));
        !result)
        return result;
    if (auto result = ensureBuffer(frame.skinnedVertices,
                                   m_skinnedVertexCount * sizeof(GpuSkinnedVertex), storage, false);
        !result)
        return result;
    if (auto result = ensureBuffer(frame.skinnedBounds, m_skinJobs.size() * sizeof(GpuSkinBounds),
                                   storage, false);
        !result)
        return result;
    for (GpuSkinJob& job : m_skinJobs)
    {
        job.output += frame.skinnedVertices->deviceAddress();
        job.bounds += frame.skinnedBounds->deviceAddress();
    }
    std::memcpy(frame.skinJobs->mappedBytes().data(), m_skinJobs.data(),
                m_skinJobs.size() * sizeof(GpuSkinJob));
    return {};
}

void VulkanRenderer::recordSkinning(VkCommandBuffer commands, const FrameContext& frame) const
{
    const auto barrier = [&](VkPipelineStageFlags2 source, VkAccessFlags2 sourceAccess,
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
    };
    // Mesh uploads were originally consumed only by vertex shaders. The first pose may read them
    // in compute in the same submission which uploaded them.
    barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    const auto jobCount = static_cast<std::uint32_t>(m_skinJobs.size());
    const std::uint32_t width = std::min(jobCount, 65535u);
    const SkinningPushConstants constants{
        .jobs = frame.skinJobs->deviceAddress(), .jobCount = jobCount, .dispatchWidth = width};
    const Pipeline& pipeline = *m_skinningPipeline;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
    vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDispatch(commands, width, (jobCount + width - 1) / width, 1);
    barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

} // namespace devex::render::vulkan
