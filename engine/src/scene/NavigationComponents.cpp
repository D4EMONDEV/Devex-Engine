#include <devex/scene/NavigationComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(NavMeshSurface)
{
    type.field("nav_mesh", &NavMeshSurface::navMesh, {.assetType = "navmesh"})
        .field("agent_radius", &NavMeshSurface::agentRadius)
        .field("agent_height", &NavMeshSurface::agentHeight)
        .field("agent_max_climb", &NavMeshSurface::agentMaxClimb)
        .field("agent_max_slope", &NavMeshSurface::agentMaxSlope, {.angle = true})
        .field("cell_size", &NavMeshSurface::cellSize)
        .field("cell_height", &NavMeshSurface::cellHeight)
        .field("tile_size", &NavMeshSurface::tileSize);
}

DEVEX_REFLECT(NavMeshAgent)
{
    type.field("speed", &NavMeshAgent::speed)
        .field("acceleration", &NavMeshAgent::acceleration)
        .field("angular_speed", &NavMeshAgent::angularSpeed, {.angle = true})
        .field("radius", &NavMeshAgent::radius)
        .field("height", &NavMeshAgent::height)
        .field("stopping_distance", &NavMeshAgent::stoppingDistance)
        .field("avoidance", &NavMeshAgent::avoidance)
        .field("velocity", &NavMeshAgent::velocity, {.runtime = true});
}

DEVEX_REFLECT(NavMeshObstacle)
{
    type.field("shape", &NavMeshObstacle::shape).field("size", &NavMeshObstacle::size).field("center", &NavMeshObstacle::center);
}

} // namespace devex::scene
