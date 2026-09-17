#include "Lighting.hpp"

#include <algorithm>
#include <cmath>

namespace devex::render {

std::array<float, cascadeCount> computeCascadeSplits(float nearPlane, float shadowDistance,
                                                     float lambda) noexcept
{
    std::array<float, cascadeCount> splits{};
    const float farPlane = std::max(shadowDistance, nearPlane * 2.0f);
    for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
    {
        const float fraction = static_cast<float>(cascade + 1) / static_cast<float>(cascadeCount);
        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, fraction);
        const float uniform = nearPlane + (farPlane - nearPlane) * fraction;
        splits[cascade] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
    return splits;
}

ShadowCascades computeShadowCascades(const math::Mat4& cameraWorld, float verticalFov,
                                     float aspectRatio, float nearPlane, float shadowDistance,
                                     math::Vec3 lightDirection, std::uint32_t resolution) noexcept
{
    ShadowCascades cascades;
    cascades.splitDistances = computeCascadeSplits(nearPlane, shadowDistance, 0.75f);

    const math::Vec3 direction = math::normalize(lightDirection);
    // Any vector not parallel to the light works as the up direction of the shadow view.
    const math::Vec3 up = std::abs(direction.y) > 0.99f ? math::Vec3{0.0f, 0.0f, 1.0f}
                                                         : math::Vec3{0.0f, 1.0f, 0.0f};
    const float tanY = std::tan(verticalFov * 0.5f);
    const float tanX = tanY * aspectRatio;

    math::Mat4 clipCorrection{1.0f};
    clipCorrection[1][1] = -1.0f;

    float sliceNear = nearPlane;
    for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
    {
        const float sliceFar = cascades.splitDistances[cascade];

        // The bounding sphere of the slice depends only on its distances and the field of view,
        // so its radius does not change when the camera turns. Corners lie at `diagonal` times
        // their distance from the view axis.
        const float diagonal = std::sqrt(tanX * tanX + tanY * tanY);
        const float nearCorner = sliceNear * diagonal;
        const float farCorner = sliceFar * diagonal;
        const float length = sliceFar - sliceNear;
        // Distance along the view axis from the slice near plane to the sphere center.
        const float centerOffset = std::clamp(
            (length * length + farCorner * farCorner - nearCorner * nearCorner) / (2.0f * length),
            0.0f, length);
        float radius = std::max(std::sqrt(nearCorner * nearCorner + centerOffset * centerOffset),
                                std::sqrt(farCorner * farCorner +
                                          (length - centerOffset) * (length - centerOffset)));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const math::Vec3 viewCenter{0.0f, 0.0f, -(sliceNear + centerOffset)};
        math::Vec3 center = math::Vec3(cameraWorld * math::Vec4(viewCenter, 1.0f));

        // Casters between the light and the sphere must fit in the depth range.
        const float casterMargin = std::max(radius, 50.0f);
        math::Mat4 lightView = math::lookAt(math::Vec3{0.0f}, direction, up);

        // Snap the center to whole texels in light space.
        const float texelSize = 2.0f * radius / static_cast<float>(resolution);
        math::Vec3 lightCenter = math::Vec3(lightView * math::Vec4(center, 1.0f));
        lightCenter.x = std::floor(lightCenter.x / texelSize) * texelSize;
        lightCenter.y = std::floor(lightCenter.y / texelSize) * texelSize;
        center = math::Vec3(math::inverse(lightView) * math::Vec4(lightCenter, 1.0f));

        const math::Vec3 eye = center - direction * (radius + casterMargin);
        lightView = math::lookAt(eye, center, up);
        const math::Mat4 projection =
            math::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + casterMargin);

        cascades.viewProjections[cascade] = clipCorrection * projection * lightView;
        cascades.texelSizes[cascade] = texelSize;
        sliceNear = sliceFar;
    }
    return cascades;
}

void assignLightsToClusters(const ClusterGrid& grid, const math::Mat4& view, float verticalFov,
                            float aspectRatio, std::span<const RenderLight> lights,
                            LightClusters& result)
{
    const std::uint32_t clusterCount = grid.tilesX * grid.tilesY * grid.slices;
    result.clusters.assign(clusterCount, ClusterRange{});
    result.lightIndices.clear();

    const float logRange = std::log2(grid.farPlane / grid.nearPlane);
    result.sliceScale = static_cast<float>(grid.slices) / logRange;
    result.sliceBias = -static_cast<float>(grid.slices) * std::log2(grid.nearPlane) / logRange;
    if (lights.empty())
    {
        return;
    }

    const float tanY = std::tan(verticalFov * 0.5f);
    const float tanX = tanY * aspectRatio;
    const auto sliceDistance = [&](std::uint32_t slice) {
        return grid.nearPlane * std::pow(grid.farPlane / grid.nearPlane,
                                         static_cast<float>(slice) / static_cast<float>(grid.slices));
    };

    // For each cluster, the lights touching it, gathered before being packed.
    std::vector<std::vector<std::uint32_t>> perCluster(clusterCount);
    for (std::uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
    {
        const RenderLight& light = lights[lightIndex];
        const math::Vec3 center = math::Vec3(view * math::Vec4(light.position, 1.0f));
        const float radius = light.range;
        const float distance = -center.z;
        if (distance + radius < grid.nearPlane)
        {
            continue;
        }

        const auto sliceOf = [&](float viewDistance) {
            const float slice = std::floor(std::log2(std::max(viewDistance, grid.nearPlane)) *
                                               result.sliceScale +
                                           result.sliceBias);
            return static_cast<std::uint32_t>(
                std::clamp(slice, 0.0f, static_cast<float>(grid.slices - 1)));
        };
        const std::uint32_t firstSlice = sliceOf(distance - radius);
        const std::uint32_t lastSlice = sliceOf(distance + radius);

        for (std::uint32_t slice = firstSlice; slice <= lastSlice; ++slice)
        {
            const float nearDistance = sliceDistance(slice);
            const float farDistance =
                slice + 1 == grid.slices ? std::max(grid.farPlane, distance + radius) : sliceDistance(slice + 1);
            for (std::uint32_t tileY = 0; tileY < grid.tilesY; ++tileY)
            {
                // Rows start at the top of the screen, where view-space Y is largest.
                const float top = 1.0f - 2.0f * static_cast<float>(tileY) / static_cast<float>(grid.tilesY);
                const float bottom = 1.0f - 2.0f * static_cast<float>(tileY + 1) / static_cast<float>(grid.tilesY);
                const float minY = std::min(bottom * tanY * nearDistance, bottom * tanY * farDistance);
                const float maxY = std::max(top * tanY * nearDistance, top * tanY * farDistance);
                for (std::uint32_t tileX = 0; tileX < grid.tilesX; ++tileX)
                {
                    const float left = -1.0f + 2.0f * static_cast<float>(tileX) / static_cast<float>(grid.tilesX);
                    const float right = -1.0f + 2.0f * static_cast<float>(tileX + 1) / static_cast<float>(grid.tilesX);
                    const float minX = std::min(left * tanX * nearDistance, left * tanX * farDistance);
                    const float maxX = std::max(right * tanX * nearDistance, right * tanX * farDistance);

                    // Distance from the sphere center to the cluster's bounding box.
                    const float dx = std::max({minX - center.x, 0.0f, center.x - maxX});
                    const float dy = std::max({minY - center.y, 0.0f, center.y - maxY});
                    const float dz = std::max({nearDistance - distance, 0.0f, distance - farDistance});
                    if (dx * dx + dy * dy + dz * dz <= radius * radius)
                    {
                        perCluster[(slice * grid.tilesY + tileY) * grid.tilesX + tileX].push_back(lightIndex);
                    }
                }
            }
        }
    }

    for (std::uint32_t cluster = 0; cluster < clusterCount; ++cluster)
    {
        result.clusters[cluster] = {static_cast<std::uint32_t>(result.lightIndices.size()),
                                    static_cast<std::uint32_t>(perCluster[cluster].size())};
        result.lightIndices.insert(result.lightIndices.end(), perCluster[cluster].begin(),
                                   perCluster[cluster].end());
    }
}

} // namespace devex::render
