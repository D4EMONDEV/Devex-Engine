#pragma once

#include <devex/core/Hash.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <cmath>
#include <cstdint>
#include <span>

namespace devex::physics::detail
{

// The closest cached transform, including through grouping entities without a Transform.
[[nodiscard]] inline math::Mat4 worldMatrixOf(const scene::Scene& scene,
                                              scene::Entity entity) noexcept
{
    for (scene::Entity current = entity; current.isValid(); current = scene.parent(current))
    {
        if (const auto* world = scene.tryGet<scene::WorldTransform>(current))
            return world->matrix;
    }
    return math::Mat4{1.0f};
}

template <typename RigidBody>
[[nodiscard]] scene::Entity bodyOwner(const scene::Scene& scene, scene::Entity collider) noexcept
{
    for (scene::Entity current = collider; current.isValid(); current = scene.parent(current))
    {
        if (scene.has<RigidBody>(current))
            return current;
    }
    return collider;
}

// A simulation changes the world pose, while the scene keeps a parent-relative Transform.
// Each backend computes its target and decides whether it owns the rotation.
inline void applyWorldPose(scene::Scene& scene, scene::Entity entity, const math::Mat4& target,
                           bool rotate)
{
    auto* local = scene.tryGet<scene::Transform>(entity);
    if (local == nullptr)
        return;
    const math::Trs relative =
        math::decomposeTrs(math::inverse(worldMatrixOf(scene, scene.parent(entity))) * target);
    local->position = relative.translation;
    if (rotate)
        local->rotation = math::normalize(relative.rotation);
    if (auto* world = scene.tryGet<scene::WorldTransform>(entity))
        world->matrix = target;
}

// Recomputes descendants immediately after physics writes or interpolates a cached world pose.
inline void updateDescendants(scene::Scene& scene, scene::Entity entity, const math::Mat4& world)
{
    for (scene::Entity child = scene.firstChild(entity); child.isValid();
         child = scene.nextSibling(child))
    {
        const auto* local = scene.tryGet<scene::Transform>(child);
        const math::Mat4 childWorld = local != nullptr ? world * local->matrix() : world;
        if (auto* transform = scene.tryGet<scene::WorldTransform>(child))
            transform->matrix = childWorld;
        updateDescendants(scene, child, childWorld);
    }
}

// Identical hashing and placement precision for both backends. Ordinary properties are exact;
// world-space placements tolerate the rounding of a turning body, to a tenth of a millimeter.
class Signature
{
  public:
    template <typename T> void add(const T& value) noexcept
    {
        m_value = core::hash64(std::as_bytes(std::span(&value, 1)), m_value);
    }

    void addPlacement(float value) noexcept
    {
        add(static_cast<std::int64_t>(std::lround(value * 10000.0f)));
    }

    void addPlacement(math::Vec3 value) noexcept
    {
        for (int axis = 0; axis < 3; ++axis)
            addPlacement(value[axis]);
    }

    void addPlacement(math::Quat rotation) noexcept
    {
        // q and -q denote the same rotation, including half turns whose w is zero.
        if (rotation.w < 0.0f ||
            (rotation.w == 0.0f &&
             (rotation.x < 0.0f ||
              (rotation.x == 0.0f &&
               (rotation.y < 0.0f || (rotation.y == 0.0f && rotation.z < 0.0f))))))
            rotation = -rotation;
        addPlacement(math::Vec3(rotation.x, rotation.y, rotation.z));
        addPlacement(rotation.w);
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return m_value; }

  private:
    std::uint64_t m_value = 0x9E3779B97F4A7C15ull;
};

} // namespace devex::physics::detail
