#include "VulkanRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace devex::render::vulkan
{
namespace
{
// The absolute row sums of A^T A bound its largest eigenvalue, including shear. Unlike
// absolute sums of A, this stays invariant when an object rotates in the world (R * A).
float scaleOf(const math::Mat4& matrix)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return std::numeric_limits<float>::quiet_NaN();
    if (matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0 || matrix[3][3] != 1)
        return std::numeric_limits<float>::quiet_NaN();
    const auto x = math::Vec3(matrix[0]), y = math::Vec3(matrix[1]), z = math::Vec3(matrix[2]);
    const float xy = std::abs(math::dot(x, y)), xz = std::abs(math::dot(x, z)),
                yz = std::abs(math::dot(y, z));
    return std::sqrt(std::max(
        {math::dot(x, x) + xy + xz, math::dot(y, y) + xy + yz, math::dot(z, z) + xz + yz}));
}
} // namespace

MeshHandle VulkanRenderer::chooseMeshLod(const MeshInstance& instance) const
{
    const auto* mesh = m_meshes.find(instance.mesh);
    const float budget = m_meshLodError * instance.lodBias;
    if (mesh == nullptr || mesh->lods.empty() || !(budget > 0) || !std::isfinite(budget) ||
        instance.submesh >= mesh->submeshes.size() ||
        shaderOf(instance.material, asset::ShaderKind::Spatial) != nullptr)
        return instance.mesh;
    auto bounds = math::transform(instance.transform, mesh->bounds);
    float scale = scaleOf(instance.transform);
    if (mesh->skin && instance.boneCount > 0)
    {
        if (instance.boneCount < mesh->jointCount ||
            std::size_t(instance.firstBone) + instance.boneCount > m_world.boneMatrices.size())
            return instance.mesh;
        const auto bones =
            std::span(m_world.boneMatrices).subspan(instance.firstBone, mesh->jointCount);
        bounds = mesh->skinBounds.pose(bones, instance.transform);
        for (const auto& bone : bones)
            scale = std::max(scale, scaleOf(bone));
    }
    if (bounds.isEmpty() || !std::isfinite(scale))
        return instance.mesh;
    const auto& camera = m_world.camera;
    float pixels = float(m_sceneExtent.height) / (2 * std::max(camera.orthographicSize, 0.0001f));
    if (camera.projection == Projection::Perspective)
    {
        if (!(camera.verticalFov > 0 && camera.verticalFov < math::radians(180.0f)))
            return instance.mesh;
        const auto center = camera.view * math::Vec4(bounds.centre(), 1);
        const float distance = -center.z - math::length(bounds.extent());
        if (!(distance > std::max(camera.nearPlane, 0.0001f)))
            return instance.mesh;
        pixels = float(m_sceneExtent.height) / (2 * std::tan(camera.verticalFov * 0.5f) * distance);
    }
    MeshHandle selected = instance.mesh;
    const auto previous =
        std::ranges::lower_bound(m_previousLods, instanceKey(instance), {}, &LodChoice::key);
    const auto kept = instance.objectId != 0 && previous != m_previousLods.end() &&
                              previous->key == instanceKey(instance) &&
                              previous->original == instance.mesh
                          ? previous->selected
                          : MeshHandle{};
    for (const auto& lod : mesh->lods)
        if (lod.error * scale * pixels <= budget * (lod.mesh == kept ? 1.1f : 0.9f) &&
            isReady(lod.mesh))
            selected = lod.mesh;
    return selected;
}

} // namespace devex::render::vulkan
