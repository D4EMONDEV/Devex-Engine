#include "render/SkinBounds.hpp"

#include <devex/asset/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("Joint boxes enclose blended poses including unweighted vertices", "[render][skinning]")
{
    using namespace devex;
    auto mesh = asset::makeUvSphere();
    mesh.inverseBind.assign(2, math::Mat4{1.0f});
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
    {
        // Non-normalized positive weights still describe a convex hull after division by W.
        const float weight = std::clamp(mesh.vertices[index].position.y + 0.5f, 0.0f, 1.0f);
        mesh.skin.push_back({.joints = {0, 1, 0, 0},
                             .weights = index % 17 == 0
                                            ? math::Vec4{0.0f}
                                            : math::Vec4{2 * weight, 2 * (1 - weight), 0, 0}});
    }
    const auto bounds = render::detail::SkinBounds::of(mesh);
    for (int pose = 0; pose < 32; ++pose)
    {
        const auto world = math::translate(math::Mat4{1}, {25, -4, 10});
        const std::array bones{
            math::rotate(math::scale(world, {-2, 0.5f, 3}), float(pose) * 0.2f, {0, 0, 1}),
            math::rotate(math::translate(world, {-10, 5, 2}), float(pose) * -0.3f, {0, 1, 0})};
        const auto box = bounds.pose(bones, world);
        REQUIRE_FALSE(box.isEmpty());
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
        {
            const auto& skin = mesh.skin[index];
            auto matrix = bones[0] * skin.weights.x + bones[1] * skin.weights.y;
            if (skin.weights.x + skin.weights.y <= 0.0001f)
                matrix = world;
            const auto transformed = matrix * math::Vec4(mesh.vertices[index].position, 1);
            const auto point = math::Vec3(transformed) / transformed.w;
            for (int axis = 0; axis < 3; ++axis)
            {
                CHECK(point[axis] >= box.min[axis]);
                CHECK(point[axis] <= box.max[axis]);
            }
        }
    }
}

TEST_CASE("Uncertain skinning bounds keep poses instead of hiding them", "[render][skinning]")
{
    using namespace devex;
    auto mesh = asset::makeCube();
    mesh.inverseBind.assign(2, math::Mat4{1});
    mesh.skin.assign(mesh.vertices.size(), {.joints = {0, 1, 0, 0}, .weights = {0.5f, 0.5f, 0, 0}});
    std::array bones{math::Mat4{1}, math::Mat4{1}};
    const auto bounds = render::detail::SkinBounds::of(mesh);
    REQUIRE_FALSE(bounds.pose(bones, math::Mat4{1}).isEmpty());
    CHECK(bounds.pose(std::span(bones).first(1), math::Mat4{1}).isEmpty());
    bones[1][0][3] = 0.1f;
    CHECK(bounds.pose(bones, math::Mat4{1}).isEmpty());
    bones[1] = math::Mat4{1};
    bones[1][3][0] = std::numeric_limits<float>::infinity();
    CHECK(bounds.pose(bones, math::Mat4{1}).isEmpty());
    bones[1] = math::Mat4{1};
    mesh.skin[0].weights = {2, -1, 0, 0};
    CHECK(render::detail::SkinBounds::of(mesh).pose(bones, math::Mat4{1}).isEmpty());
    mesh.skin[0].weights = math::Vec4{0};
    auto projective = math::Mat4{1};
    projective[3][3] = 0.5f;
    CHECK(render::detail::SkinBounds::of(mesh).pose(bones, projective).isEmpty());
}
