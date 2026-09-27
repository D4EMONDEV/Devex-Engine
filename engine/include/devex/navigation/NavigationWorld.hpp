#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/NavMeshData.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace devex::scene {
class Scene;
}

namespace devex::navigation {

// The baked navigation mesh of an asset; null when it cannot be loaded.
using NavMeshSource = std::function<std::shared_ptr<const asset::NavMeshData>(asset::AssetId navMesh)>;

struct NavigationWorldConfig
{
    NavMeshSource navMeshes;
    // Agents walking at the same time, and obstacles cut out of the mesh.
    int maxAgents = 256;
    int maxObstacles = 256;
};

// Where a ray along the navigation mesh leaves it.
struct NavHit
{
    math::Vec3 position{0.0f};
    // Along the ground, away from the edge it hit.
    math::Vec3 normal{0.0f};
    float distance = 0.0f;
};

// The navigation of a scene while it plays: the mesh of its NavMeshSurface, with its NavMeshObstacle
// components cut out of it, and its NavMeshAgent components walking it as a crowd that steers around
// itself.
class NavigationWorld
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<NavigationWorld>> create(NavigationWorldConfig config);
    ~NavigationWorld();

    NavigationWorld(const NavigationWorld&) = delete;
    NavigationWorld& operator=(const NavigationWorld&) = delete;

    // Once per frame, after game code: the mesh follows the surface and the obstacles, agents appear,
    // change and go with their components, and walk; they write their position, their rotation and
    // their velocity. An agent whose entity game code moved starts from there. Using the world with
    // another scene starts over from that scene.
    void update(scene::Scene& scene, core::Duration delta);

    // Sends an agent to the point of the mesh closest to a destination; false when it is far from the
    // mesh. It may be called before the agent first walks.
    bool setDestination(scene::Entity agent, math::Vec3 destination);
    // Stops the agent where it is.
    void stop(scene::Entity agent);
    // Whether the agent walks to a destination.
    [[nodiscard]] bool hasDestination(scene::Entity agent) const;
    [[nodiscard]] std::optional<math::Vec3> destination(scene::Entity agent) const;
    // How far it still has to walk, along its path; 0 without a destination.
    [[nodiscard]] float remainingDistance(scene::Entity agent) const;
    // The corners of the path ahead of the agent, from where it stands to its destination.
    [[nodiscard]] std::vector<math::Vec3> path(scene::Entity agent) const;

    // The corners of the shortest path along the mesh between two points, from the first to the
    // point of the mesh closest to the second; empty when there is none.
    [[nodiscard]] std::vector<math::Vec3> findPath(math::Vec3 from, math::Vec3 to) const;
    // The point of the mesh closest to a point, within a distance.
    [[nodiscard]] std::optional<math::Vec3> samplePosition(math::Vec3 point, float maxDistance) const;
    // Walks a straight line along the mesh; where it leaves the mesh, if it does.
    [[nodiscard]] std::optional<NavHit> raycast(math::Vec3 from, math::Vec3 to) const;

    [[nodiscard]] bool hasNavMesh() const noexcept;
    [[nodiscard]] std::size_t agentCount() const noexcept;
    [[nodiscard]] std::size_t obstacleCount() const noexcept;

private:
    struct Implementation;

    explicit NavigationWorld(std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> m_implementation;
};

} // namespace devex::navigation
