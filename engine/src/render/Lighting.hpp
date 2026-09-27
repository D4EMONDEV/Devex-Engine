#pragma once

#include <devex/math/Math.hpp>
#include <devex/render/RenderWorld.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Lighting computations that run on the CPU every frame: shadow cascades and the assignment of
// local lights to clusters. They use no Vulkan type so that tests can check them.
namespace devex::render {

inline constexpr std::uint32_t cascadeCount = 4;

// How wide a view is along its axis: at a distance, its half width and half height are
// halfExtent + slope * distance. A perspective starts from a point and widens with its field of
// view; an orthographic view keeps its size.
struct ViewVolume
{
    math::Vec2 halfExtent{0.0f};
    math::Vec2 slope{1.0f};

    [[nodiscard]] static ViewVolume perspective(float verticalFov, float aspectRatio) noexcept;
    [[nodiscard]] static ViewVolume orthographic(float halfHeight, float aspectRatio) noexcept;
    // The volume a camera sees.
    [[nodiscard]] static ViewVolume of(const RenderCamera& camera, float aspectRatio) noexcept;

    [[nodiscard]] math::Vec2 halfSizeAt(float distance) const noexcept
    {
        return halfExtent + slope * distance;
    }
};

struct ShadowCascades
{
    // World to shadow clip space, with the Vulkan clip space convention (Y down, depth 0 to 1).
    std::array<math::Mat4, cascadeCount> viewProjections{};
    // View-space distance at which each cascade ends.
    std::array<float, cascadeCount> splitDistances{};
    // Width of a shadow map texel in world units, for normal offset biasing.
    std::array<float, cascadeCount> texelSizes{};
};

// Distances at which cascades end: a blend of uniform and logarithmic splits, weighted by lambda.
[[nodiscard]] std::array<float, cascadeCount> computeCascadeSplits(float nearPlane, float shadowDistance,
                                                                   float lambda) noexcept;

// Fits an orthographic shadow projection around each slice of the camera frustum. Each cascade
// covers the bounding sphere of its slice, and snaps to shadow map texels, so that shadows do not
// shimmer when the camera moves or turns.
[[nodiscard]] ShadowCascades computeShadowCascades(const math::Mat4& cameraWorld, const ViewVolume& volume,
                                                   float nearPlane, float shadowDistance,
                                                   math::Vec3 lightDirection, std::uint32_t resolution) noexcept;
// The same for a perspective camera.
[[nodiscard]] ShadowCascades computeShadowCascades(const math::Mat4& cameraWorld, float verticalFov,
                                                   float aspectRatio, float nearPlane,
                                                   float shadowDistance, math::Vec3 lightDirection,
                                                   std::uint32_t resolution) noexcept;

struct ClusterGrid
{
    std::uint32_t tilesX = 16;
    std::uint32_t tilesY = 9;
    std::uint32_t slices = 24;
    // View-space distances covered by the slices; farther lights share the last slice.
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
};

struct ClusterRange
{
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct LightClusters
{
    // Indexed by (slice * tilesY + tileY) * tilesX + tileX, with tile rows from the top.
    std::vector<ClusterRange> clusters;
    std::vector<std::uint32_t> lightIndices;
    // slice = floor(log2(viewDistance) * sliceScale + sliceBias).
    float sliceScale = 0.0f;
    float sliceBias = 0.0f;
};

// Lists, for every cluster of the view frustum, the lights whose sphere of influence touches it.
void assignLightsToClusters(const ClusterGrid& grid, const math::Mat4& view, const ViewVolume& volume,
                            std::span<const RenderLight> lights, LightClusters& result);
// The same for a perspective camera.
void assignLightsToClusters(const ClusterGrid& grid, const math::Mat4& view, float verticalFov,
                            float aspectRatio, std::span<const RenderLight> lights,
                            LightClusters& result);

} // namespace devex::render
