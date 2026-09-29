#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <array>
#include <cstdint>
#include <string_view>

// Navigation components: where agents walk, the agents, and what they walk around.
namespace devex::scene {

// Where agents walk: the navigation mesh the editor bakes from the static colliders of the scene,
// for agents of the size given here. Bake it again when the scenery changes.
struct DEVEX_API NavMeshSurface
{
    // The baked navigation mesh, a .dvxnavmesh next to the scene.
    asset::AssetId navMesh;
    float agentRadius = 0.4f;
    float agentHeight = 1.8f;
    // The highest step agents climb.
    float agentMaxClimb = 0.4f;
    // The steepest slope agents walk up.
    float agentMaxSlope = math::radians(45.0f);
    // The size of the voxels the scene is cut into, across and up: smaller follows the scenery closer,
    // and takes longer to bake.
    float cellSize = 0.2f;
    float cellHeight = 0.1f;
    // Voxels along a side of a tile, which obstacles rebuild one at a time.
    std::int32_t tileSize = 48;
};
DEVEX_DECLARE_ENGINE_REFLECTION(NavMeshSurface);

// How well agents steer around each other: better costs more.
enum class NavAvoidance : std::uint8_t
{
    None,
    Low,
    Medium,
    Good,
    High,
};

// A character that walks the navigation mesh by itself: given a destination by game code, it finds
// its way, speeds up, turns towards where it goes, steers around the other agents and stops there. It
// moves its entity, which stands on the mesh.
struct DEVEX_API NavMeshAgent
{
    // In meters per second, and meters per second each second.
    float speed = 3.5f;
    float acceleration = 8.0f;
    // How fast it turns to face where it walks, per second; 0 leaves its rotation alone.
    float angularSpeed = math::radians(360.0f);
    float radius = 0.4f;
    float height = 1.8f;
    // How close to its destination it stops.
    float stoppingDistance = 0.1f;
    NavAvoidance avoidance = NavAvoidance::Good;

    // Runtime state, neither saved nor shown: the velocity it walks at.
    math::Vec3 velocity{0.0f};
};
DEVEX_DECLARE_ENGINE_REFLECTION(NavMeshAgent);

enum class NavObstacleShape : std::uint8_t
{
    Box,
    Cylinder,
};

// Something agents walk around, such as a crate pushed in their way: it cuts its shape out of the
// navigation mesh where it stands, and again once it moved.
struct DEVEX_API NavMeshObstacle
{
    NavObstacleShape shape = NavObstacleShape::Box;
    // Full extents in the space of its entity; a cylinder is as wide as X and as high as Y.
    math::Vec3 size{1.0f};
    math::Vec3 center{0.0f};
};
DEVEX_DECLARE_ENGINE_REFLECTION(NavMeshObstacle);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::NavAvoidance>
{
    static constexpr std::array<std::string_view, 5> names{"none", "low", "medium", "good", "high"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::NavObstacleShape>
{
    static constexpr std::array<std::string_view, 2> names{"box", "cylinder"};
};
