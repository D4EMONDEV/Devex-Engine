#include "render/Lighting.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinRel;
using devex::math::Mat4;
using devex::math::Vec3;
using devex::math::Vec4;

TEST_CASE("Cascade splits grow towards the shadow distance", "[render][lighting]")
{
    const auto splits = devex::render::computeCascadeSplits(0.1f, 80.0f, 0.75f);
    CHECK(std::ranges::is_sorted(splits));
    CHECK(splits.front() > 0.1f);
    CHECK_THAT(splits.back(), WithinRel(80.0f, 1e-4f));
}

TEST_CASE("Each shadow cascade contains its slice of the camera frustum", "[render][lighting]")
{
    // A camera above the ground, turned and pitched down.
    const Mat4 cameraWorld = devex::math::translate(Mat4{1.0f}, Vec3{3.0f, 2.0f, 5.0f}) *
                             devex::math::mat4_cast(devex::math::angleAxis(0.7f, Vec3{0.0f, 1.0f, 0.0f}) *
                                                    devex::math::angleAxis(-0.3f, Vec3{1.0f, 0.0f, 0.0f}));
    const float fov = devex::math::radians(60.0f);
    const float aspect = 16.0f / 9.0f;
    const float nearPlane = 0.1f;
    const devex::render::ShadowCascades cascades = devex::render::computeShadowCascades(
        cameraWorld, fov, aspect, nearPlane, 60.0f, Vec3{-0.3f, -1.0f, -0.4f}, 2048);

    const float tanY = std::tan(fov * 0.5f);
    float sliceNear = nearPlane;
    for (std::uint32_t cascade = 0; cascade < devex::render::cascadeCount; ++cascade)
    {
        const float sliceFar = cascades.splitDistances[cascade];
        for (const float distance : {sliceNear, sliceFar})
        {
            for (const float sx : {-1.0f, 1.0f})
            {
                for (const float sy : {-1.0f, 1.0f})
                {
                    const Vec4 viewCorner{sx * tanY * aspect * distance, sy * tanY * distance, -distance, 1.0f};
                    const Vec4 clip = cascades.viewProjections[cascade] * (cameraWorld * viewCorner);
                    CHECK(std::abs(clip.x) <= 1.0f);
                    CHECK(std::abs(clip.y) <= 1.0f);
                    CHECK(clip.z >= 0.0f);
                    CHECK(clip.z <= 1.0f);
                }
            }
        }
        CHECK(cascades.texelSizes[cascade] > 0.0f);
        sliceNear = sliceFar;
    }
    CHECK(cascades.texelSizes[0] < cascades.texelSizes[3]);
}

TEST_CASE("Lights are assigned to the clusters their range touches", "[render][lighting]")
{
    const devex::render::ClusterGrid grid;
    const float fov = devex::math::radians(60.0f);
    const std::vector<devex::render::RenderLight> lights{
        // In front of the camera, at the center of the screen.
        {.position = {0.0f, 0.0f, -10.0f}, .range = 1.0f},
        // Behind the camera.
        {.position = {0.0f, 0.0f, 10.0f}, .range = 1.0f},
    };
    devex::render::LightClusters clusters;
    devex::render::assignLightsToClusters(grid, Mat4{1.0f}, fov, 16.0f / 9.0f, lights, clusters);

    REQUIRE(clusters.clusters.size() == grid.tilesX * grid.tilesY * grid.slices);
    const auto slice = static_cast<std::uint32_t>(std::floor(std::log2(10.0f) * clusters.sliceScale + clusters.sliceBias));
    REQUIRE(slice < grid.slices);
    const devex::render::ClusterRange center =
        clusters.clusters[(slice * grid.tilesY + grid.tilesY / 2) * grid.tilesX + grid.tilesX / 2];
    REQUIRE(center.count == 1);
    CHECK(clusters.lightIndices[center.offset] == 0);

    // The light behind the camera is in no cluster, and far corners hold nothing.
    CHECK(std::ranges::count(clusters.lightIndices, 1u) == 0);
    CHECK(clusters.clusters.front().count == 0);
    CHECK(clusters.clusters.back().count == 0);
}
