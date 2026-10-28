#include <devex/render/CubeMaps.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <format>

TEST_CASE("Cameras that capture the faces of a cube map see them mirrored left to right", "[render][probe]")
{
    constexpr std::uint32_t size = 16;
    const devex::math::Mat4 projection = devex::math::perspectiveReverseZ(devex::math::radians(90.0f), 1.0f, 0.05f);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        const devex::math::Mat4 viewProjection = projection * devex::render::cubeFaceView(devex::math::Vec3{0.0f}, face);
        for (const auto& [x, y] : {std::pair{0u, 0u}, std::pair{3u, 12u}, std::pair{15u, 7u}, std::pair{9u, 15u}})
        {
            INFO(std::format("face {}, pixel {} {}", face, x, y));
            // Where the camera draws the direction of the pixel: the top row of its image is up.
            const devex::math::Vec4 clip = viewProjection * devex::math::Vec4(devex::render::cubeFaceDirection(face, x, y, size), 1.0f);
            REQUIRE(clip.w > 0.0f);
            const float column = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(size);
            const float row = (0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(size);
            CHECK(static_cast<std::uint32_t>(std::floor(column)) == size - 1 - x);
            CHECK(static_cast<std::uint32_t>(std::floor(row)) == y);
        }
    }
}
