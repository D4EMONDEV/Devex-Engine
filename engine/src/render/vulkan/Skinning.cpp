// Pose outputs belong to a frame, not a camera. Each view prepares only its missing useful poses.
#include "VulkanRenderer.hpp"

#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <bit>
#include <cstring>

namespace devex::render::vulkan
{
namespace
{
std::size_t hashKey(const std::array<std::uint32_t, 6>& key)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto word : key)
        hash = (hash ^ word) * 1099511628211ull;
    return static_cast<std::size_t>(hash ^ (hash >> 32));
}
} // namespace

core::Result<void> VulkanRenderer::resetSkinning()
{
    auto& frame = m_skinFrames[m_frameIndex % framesInFlight];
    // Usually the main fence covers every view. A failed acquire may have left only extra views
    // submitted: their last fence must complete before any shared page can be reused.
    if (frame.completed != VK_NULL_HANDLE &&
        frame.completed != m_frames[m_frameIndex % framesInFlight].completed)
        DEVEX_VK_TRY(vkWaitForFences, m_device.handle(), 1, &frame.completed, VK_TRUE,
                     ~std::uint64_t{0});
    frame.completed = VK_NULL_HANDLE;
    frame.usedPages = 0;
    for (auto& page : frame.pages)
        if (m_frameIndex >= page.lastUsed + 120)
            page = {};
    m_skinPoses.clear();
    m_skinLookup.clear();
    m_skinView = 0;
    m_skinnedPoseCount = 0;
    m_skinnedVertexCount = 0;
    m_skinCulled = 0;
    m_skinReused = 0;
    m_lodInstances = 0;
    m_lodTrianglesSaved = 0;
    return {};
}

core::Result<void> VulkanRenderer::prepareSkinning(FrameContext& frame, bool cascades, bool atlas,
                                                   bool pick)
{
    DEVEX_PROFILE_SCOPE("Skinning data");
    m_skinJobs.clear();
    m_skinPrepared.clear();
    m_skinPage = nullptr;
    m_skinInstances.assign(m_world.meshes.size(), noCullView);
    m_skinNeeded.assign(m_world.meshes.size(), 0);
    if (!m_gpuCulling || !m_gpuSkinning)
        return {};
    const auto view = m_skinView++;
    std::uint64_t vertices = 0;
    for (std::uint32_t index = 0; index < m_world.meshes.size(); ++index)
    {
        const MeshInstance& instance = m_world.meshes[index];
        if (instance.boneCount == 0 || !isGpuDriven(instance))
            continue;
        const GpuMesh& mesh = *m_meshes.find(instance.mesh);
        if (!mesh.skin)
            continue;
        const PreviousInstance* previous = previousInstance(index);
        const bool previousSkin = previous != nullptr && previous->skinned &&
                                  previous->boneCount >= mesh.jointCount &&
                                  static_cast<std::size_t>(previous->firstBone) + mesh.jointCount <=
                                      m_previousBoneMatrices.size();
        const auto previousBone = previousSkin ? previous->firstBone : noCullView;
        const auto previousWorld = previous != nullptr ? previous->transform : instance.transform;
        // Anonymous draws stay separate even when an earlier view omitted pending uploads.
        const std::array key{instance.mesh.index,
                             instance.mesh.generation,
                             instance.objectId,
                             instance.firstBone,
                             instance.objectId == 0 ? index : 0u,
                             instance.objectId == 0 ? view : 0u};
        if (m_skinLookup.size() <= m_skinPoses.size() * 2)
        {
            m_skinLookup.assign(std::bit_ceil(std::max<std::size_t>(
                                    {m_world.meshes.size() * 2, m_skinPoses.size() * 4, 16})),
                                noCullView);
            const auto mask = m_skinLookup.size() - 1;
            for (std::uint32_t pose = 0; pose < m_skinPoses.size(); ++pose)
            {
                auto slot = hashKey(m_skinPoses[pose].key) & mask;
                while (m_skinLookup[slot] != noCullView)
                    slot = (slot + 1) & mask;
                m_skinLookup[slot] = pose;
            }
        }
        const auto mask = m_skinLookup.size() - 1;
        auto slot = hashKey(key) & mask;
        while (m_skinLookup[slot] != noCullView)
        {
            const auto& pose = m_skinPoses[m_skinLookup[slot]];
            if (pose.key == key && pose.job.world == instance.transform &&
                pose.job.previousWorld == previousWorld && pose.previousBone == previousBone)
                break;
            slot = (slot + 1) & mask;
        }
        if (m_skinLookup[slot] == noCullView)
        {
            m_skinLookup[slot] = static_cast<std::uint32_t>(m_skinPoses.size());
            const auto bones =
                std::span(m_world.boneMatrices).subspan(instance.firstBone, mesh.jointCount);
            m_skinPoses.push_back(
                {.key = key,
                 .previousBone = previousBone,
                 .conservativeBounds = mesh.skinBounds.pose(bones, instance.transform),
                 .job = {.world = instance.transform, .previousWorld = previousWorld}});
        }
        const auto poseIndex = m_skinLookup[slot];
        auto& pose = m_skinPoses[poseIndex];
        m_skinInstances[index] = poseIndex;
        if (pose.lastView != view)
        {
            pose.lastView = view;
            const auto& box = pose.conservativeBounds;
            pose.needed = pick || m_cameraFrustum.intersects(box) ||
                          (cascades && std::ranges::any_of(m_cascadeFrustums, [&](const Frustum& f)
                                                           { return f.intersectsSides(box); })) ||
                          (atlas && std::ranges::any_of(m_shadowViewFrustums, [&](const Frustum& f)
                                                        { return f.intersects(box); }));
            if (!pose.needed)
                ++m_skinCulled;
            else if (pose.job.output != 0)
                ++m_skinReused;
            else
            {
                const auto bones =
                    frame.bones->deviceAddress() + instance.firstBone * sizeof(math::Mat4);
                const auto vertexCount =
                    static_cast<std::uint32_t>(mesh.vertices.size() / sizeof(GpuVertex));
                m_skinJobs.push_back({.vertices = mesh.vertices.deviceAddress(),
                                      .skin = mesh.skin->deviceAddress(),
                                      .bones = bones,
                                      .previousBones = previousSkin
                                                           ? frame.previousBones->deviceAddress() +
                                                                 previousBone * sizeof(math::Mat4)
                                                           : bones,
                                      .output = vertices * sizeof(GpuSkinnedVertex),
                                      .bounds = m_skinJobs.size() * sizeof(GpuSkinBounds),
                                      .world = instance.transform,
                                      .previousWorld = previousWorld,
                                      .vertexCount = vertexCount});
                m_skinPrepared.push_back(poseIndex);
                vertices += vertexCount;
            }
        }
        m_skinNeeded[index] = pose.needed ? 1 : 0;
    }
    if (m_skinJobs.empty())
        return {};

    auto& arena = m_skinFrames[m_frameIndex % framesInFlight];
    if (arena.usedPages == arena.pages.size())
        arena.pages.emplace_back();
    auto& page = arena.pages[arena.usedPages++];
    page.lastUsed = m_frameIndex;
    const auto bytes = vertices * sizeof(GpuSkinnedVertex);
    page.underused = page.vertices && page.vertices->size() > bytes * 2 ? page.underused + 1 : 0;
    if (page.underused >= 30)
    {
        page.vertices.reset();
        page.bounds.reset();
        page.underused = 0;
    }
    const auto ensureOutput = [&](std::optional<Buffer>& buffer,
                                  VkDeviceSize size) -> core::Result<void>
    {
        if (buffer && buffer->size() >= size)
            return {};
        // Modest headroom rounded to 64 KiB, rather than doubling large pose outputs.
        const auto capacity = (size + size / 8 + 65535) / 65536 * 65536;
        auto made =
            Buffer::create(m_allocator, {.size = capacity,
                                         .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        if (!made)
            return std::unexpected(made.error());
        buffer = std::move(*made);
        return {};
    };
    if (auto result = ensureHostBuffer(page.jobs, m_skinJobs.size() * sizeof(GpuSkinJob)); !result)
        return result;
    if (auto result = ensureOutput(page.vertices, bytes); !result)
        return result;
    if (auto result = ensureOutput(page.bounds, m_skinJobs.size() * sizeof(GpuSkinBounds)); !result)
        return result;
    for (std::size_t index = 0; index < m_skinJobs.size(); ++index)
    {
        auto& job = m_skinJobs[index];
        job.output += page.vertices->deviceAddress();
        job.bounds += page.bounds->deviceAddress();
        m_skinPoses[m_skinPrepared[index]].job = job;
    }
    std::memcpy(page.jobs->mappedBytes().data(), m_skinJobs.data(),
                m_skinJobs.size() * sizeof(GpuSkinJob));
    m_skinPage = &page;
    m_skinnedPoseCount += static_cast<std::uint32_t>(m_skinJobs.size());
    m_skinnedVertexCount += vertices;
    return {};
}

void VulkanRenderer::recordSkinning(VkCommandBuffer commands) const
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
    barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    const auto jobCount = static_cast<std::uint32_t>(m_skinJobs.size());
    const std::uint32_t width = std::min(jobCount, 65535u);
    const SkinningPushConstants constants{
        .jobs = m_skinPage->jobs->deviceAddress(), .jobCount = jobCount, .dispatchWidth = width};
    const Pipeline& pipeline = *m_skinningPipeline;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
    vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDispatch(commands, width, (jobCount + width - 1) / width, 1);
    // Includes later camera submissions on this queue, which reuse these outputs.
    barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

} // namespace devex::render::vulkan
