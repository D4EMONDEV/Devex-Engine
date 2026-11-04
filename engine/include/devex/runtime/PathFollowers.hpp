#pragma once

#include <devex/core/Export.hpp>
#include <devex/core/Time.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/net/Multiplayer.hpp>
#include <devex/scene/PathComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <cstdint>
#include <unordered_map>

namespace devex::runtime {

// Moves the PathFollower components of a scene along their paths, keeping the curves that do not change.
class DEVEX_API PathFollowers
{
public:
    // Once per frame. In a session, what another player has authority over and keeps in sync is left to
    // that player. Null without a session.
    void update(scene::Scene& scene, core::Duration delta, const net::Multiplayer* multiplayer);
    // Another scene replaced the one it knew.
    void clear() noexcept;

private:
    struct Baked
    {
        std::uint64_t signature = 0;
        scene::PathCurve curve;
    };
    [[nodiscard]] const scene::PathCurve& curveOf(const core::Uuid& entity, const scene::Path3D& path);

    std::unordered_map<core::Uuid, Baked> m_curves;
};

// What a Path3D tells, in the coordinates of the world. These walk the whole curve at each call: code that asks
// many times a frame builds a scene::PathCurve and keeps it. Zero or the origin for an entity without a path.
[[nodiscard]] DEVEX_API float pathLength(const scene::Scene& scene, scene::Entity entity);
[[nodiscard]] DEVEX_API math::Vec3 pathPositionAt(const scene::Scene& scene, scene::Entity entity, float distance);
[[nodiscard]] DEVEX_API math::Vec3 pathDirectionAt(const scene::Scene& scene, scene::Entity entity, float distance);
[[nodiscard]] DEVEX_API float pathClosestDistance(const scene::Scene& scene, scene::Entity entity, math::Vec3 position);

} // namespace devex::runtime
