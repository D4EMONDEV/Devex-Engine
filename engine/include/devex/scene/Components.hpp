#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

// Components provided by the engine. Game code defines its own the same way: a plain struct and
// its reflection, then registerComponent<T>() to make it saveable.
namespace devex::scene {

// Position, rotation and scale relative to the parent entity.
struct Transform
{
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec3 scale{1.0f};

    [[nodiscard]] math::Mat4 matrix() const noexcept
    {
        return math::composeTrs({position, rotation, scale});
    }
};
DEVEX_DECLARE_REFLECTION(Transform);

// Transform relative to the world, computed by Scene::updateTransforms. It is never saved.
struct WorldTransform
{
    math::Mat4 matrix{1.0f};
};

// Draws a mesh asset. Each submesh uses its own material unless `material` replaces them all.
struct MeshRenderer
{
    asset::AssetId mesh;
    asset::AssetId material;
};
DEVEX_DECLARE_REFLECTION(MeshRenderer);

// Perspective camera looking along -Z of its entity. The renderer uses the first primary camera.
struct Camera
{
    float verticalFov = math::radians(60.0f);
    float nearPlane = 0.1f;
    bool primary = true;
};
DEVEX_DECLARE_REFLECTION(Camera);

// Sunlight travelling along -Z of its entity.
struct DirectionalLight
{
    // Fraction of the light that reaches surfaces facing away from the light.
    float ambient = 0.25f;
};
DEVEX_DECLARE_REFLECTION(DirectionalLight);

} // namespace devex::scene
