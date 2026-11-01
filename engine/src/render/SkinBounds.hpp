#pragma once

#include <devex/asset/MeshData.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace devex::render::detail
{

// Every positively weighted vertex lies in the convex hull of its joint-transformed positions.
// These boxes cost one transform per used joint, instead of one skinning operation per vertex.
// Signed weights and projective or invalid matrices deliberately leave the pose unbounded.
struct SkinBounds
{
    std::vector<math::Aabb> joints;
    math::Aabb unweighted;
    bool bounded = true;

    [[nodiscard]] static SkinBounds of(const asset::MeshData& mesh)
    {
        SkinBounds result;
        result.joints.resize(mesh.inverseBind.size());
        for (std::size_t index = 0; index < mesh.skin.size(); ++index)
        {
            const auto& skin = mesh.skin[index];
            const float total = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
            if (total <= 0.0001f)
            {
                result.unweighted.add(mesh.vertices[index].position);
                continue;
            }
            for (int slot = 0; slot < 4; ++slot)
            {
                if (skin.weights[slot] < 0.0f || !std::isfinite(total))
                    result.bounded = false;
                if (skin.weights[slot] > 0.0f)
                    result.joints[skin.joints[slot]].add(mesh.vertices[index].position);
            }
        }
        return result;
    }

    [[nodiscard]] math::Aabb pose(std::span<const math::Mat4> bones,
                                  const math::Mat4& world) const noexcept
    {
        if (!bounded || bones.size() < joints.size())
            return {};
        math::Aabb result;
        const auto append = [&](const math::Aabb& box, const math::Mat4& matrix)
        {
            if (box.isEmpty())
                return true;
            if (matrix[0][3] != 0.0f || matrix[1][3] != 0.0f || matrix[2][3] != 0.0f ||
                matrix[3][3] != 1.0f)
                return false;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row]))
                        return false;
            const auto center = math::Vec3(matrix * math::Vec4(box.centre(), 1.0f));
            const auto half = glm::abs(math::Vec3(matrix[0])) * box.extent().x +
                              glm::abs(math::Vec3(matrix[1])) * box.extent().y +
                              glm::abs(math::Vec3(matrix[2])) * box.extent().z;
            result.add(center - half);
            result.add(center + half);
            return true;
        };
        if (!append(unweighted, world))
            return {};
        for (std::size_t joint = 0; joint < joints.size(); ++joint)
            if (!append(joints[joint], bones[joint]))
                return {};
        if (!result.isEmpty())
        {
            // Include float rounding in both the weighted matrices and the plane tests.
            const auto margin =
                glm::max(glm::max(glm::abs(result.min), glm::abs(result.max)), math::Vec3{1.0f}) *
                0.0001f;
            result.min -= margin;
            result.max += margin;
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(result.min[axis]) || !std::isfinite(result.max[axis]))
                    return {};
        }
        return result;
    }
};

} // namespace devex::render::detail
