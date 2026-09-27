#include "TileCache.hpp"

#include <devex/core/Hash.hpp>
#include <devex/core/Log.hpp>
#include <devex/navigation/NavigationWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <DetourCommon.h>
#include <DetourCrowd.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace devex::navigation {
namespace {

using scene::Entity;

// How far from the mesh a point may be for queries to find the mesh under it.
constexpr std::array<float, 3> searchExtents{2.0f, 4.0f, 2.0f};
constexpr int maxPathPolygons = 256;
// Distances below which an entity counts as where the navigation left it.
constexpr float moveTolerance = 0.01f;
// An obstacle is cut out again once it moved or turned this much.
constexpr float obstacleMoveThreshold = 0.1f;
constexpr float obstacleTurnThreshold = 0.05f;

[[nodiscard]] std::uint64_t keyOf(Entity entity) noexcept
{
    return static_cast<std::uint64_t>(entity.generation) << 32 | entity.index;
}

[[nodiscard]] math::Vec3 fromDetour(const float* value) noexcept
{
    return {value[0], value[1], value[2]};
}

[[nodiscard]] math::Mat4 worldOf(const scene::Scene& scene, Entity entity) noexcept
{
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    return world != nullptr ? world->matrix : math::Mat4{1.0f};
}

// The turn of a world matrix around Y: where its X axis points, as Detour measures box obstacles.
[[nodiscard]] float yawOf(const math::Mat4& matrix) noexcept
{
    return std::atan2(-matrix[0][2], matrix[0][0]);
}

[[nodiscard]] math::Vec3 scaleOf(const math::Mat4& matrix) noexcept
{
    return {math::length(math::Vec3(matrix[0])), math::length(math::Vec3(matrix[1])), math::length(math::Vec3(matrix[2]))};
}

class Signature
{
public:
    template <typename T>
    void add(const T& value) noexcept
    {
        m_value = core::hash64(std::as_bytes(std::span(&value, 1)), m_value);
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return m_value;
    }

private:
    std::uint64_t m_value = 0x9E3779B97F4A7C15ull;
};

[[nodiscard]] dtCrowdAgentParams paramsOf(const scene::NavMeshAgent& agent) noexcept
{
    dtCrowdAgentParams params{};
    params.radius = std::max(agent.radius, 0.05f);
    params.height = std::max(agent.height, 0.1f);
    params.maxAcceleration = std::max(agent.acceleration, 0.0f);
    params.maxSpeed = std::max(agent.speed, 0.0f);
    params.collisionQueryRange = params.radius * 12.0f;
    params.pathOptimizationRange = params.radius * 30.0f;
    params.separationWeight = 2.0f;
    params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
    if (agent.avoidance != scene::NavAvoidance::None)
    {
        params.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
        params.obstacleAvoidanceType = static_cast<unsigned char>(std::clamp(static_cast<int>(agent.avoidance) - 1, 0, 3));
    }
    return params;
}

[[nodiscard]] std::uint64_t signatureOf(const scene::NavMeshAgent& agent) noexcept
{
    Signature signature;
    signature.add(agent.speed);
    signature.add(agent.acceleration);
    signature.add(agent.radius);
    signature.add(agent.height);
    signature.add(agent.avoidance);
    return signature.value();
}

// Places an entity at a point of the world through its Transform relative to its parent, and turns
// it upright around Y when a yaw is given; its world transform follows.
void placeEntity(scene::Scene& scene, Entity entity, math::Vec3 position, std::optional<float> yaw)
{
    scene::Transform* const local = scene.tryGet<scene::Transform>(entity);
    if (local == nullptr)
    {
        return;
    }
    const Entity parent = scene.parent(entity);
    const math::Mat4 parentWorld = parent.isValid() ? worldOf(scene, parent) : math::Mat4{1.0f};
    local->position = math::Vec3(math::inverse(parentWorld) * math::Vec4(position, 1.0f));
    if (yaw)
    {
        const math::Quat parentRotation = math::decomposeTrs(parentWorld).rotation;
        local->rotation = math::normalize(math::inverse(parentRotation) * math::angleAxis(*yaw, math::Vec3{0.0f, 1.0f, 0.0f}));
    }
    if (scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity))
    {
        world->matrix = parentWorld * local->matrix();
    }
}

} // namespace

struct NavigationWorld::Implementation
{
    struct Agent
    {
        Entity entity;
        int index = -1;
        math::Vec3 lastPosition{0.0f};
        std::uint64_t signature = 0;
        std::optional<math::Vec3> destination;
        // The destination is still to be given to the crowd.
        bool pending = false;
        bool seen = false;
    };

    struct Obstacle
    {
        dtObstacleRef reference = 0;
        math::Vec3 position{0.0f};
        float yaw = 0.0f;
        std::uint64_t signature = 0;
        bool seen = false;
    };

    explicit Implementation(NavigationWorldConfig worldConfig)
        : config(std::move(worldConfig))
    {
    }

    ~Implementation()
    {
        clearMesh();
    }

    Implementation(const Implementation&) = delete;
    Implementation& operator=(const Implementation&) = delete;

    NavigationWorldConfig config;
    asset::AssetId navMeshId;
    std::shared_ptr<const asset::NavMeshData> navMeshData;
    std::unique_ptr<detail::TileCache> cache;
    dtCrowd* crowd = nullptr;
    dtQueryFilter filter;
    std::unordered_map<std::uint64_t, Agent> agents;
    std::unordered_map<std::uint64_t, Obstacle> obstacles;
    const scene::Scene* scene = nullptr;
    std::unordered_set<std::string> warnings;

    void warnOnce(const std::string& message)
    {
        if (warnings.insert(message).second)
        {
            DEVEX_LOG_WARNING("{}", message);
        }
    }

    // Forgets the mesh, and what walked it; agents are added again to the next one.
    void clearMesh()
    {
        dtFreeCrowd(crowd);
        crowd = nullptr;
        cache.reset();
        navMeshData.reset();
        for (auto& [key, agent] : agents)
        {
            agent.index = -1;
            agent.pending = agent.destination.has_value();
        }
        obstacles.clear();
    }

    // Takes the mesh of the first NavMeshSurface that has one, or its new version.
    void syncMesh(scene::Scene& sceneToRead)
    {
        asset::AssetId wanted;
        std::size_t surfaces = 0;
        for ([[maybe_unused]] auto [entity, surface] : sceneToRead.view<scene::NavMeshSurface>())
        {
            ++surfaces;
            if (!wanted.isValid() && surface.navMesh.isValid())
            {
                wanted = surface.navMesh;
            }
        }
        if (surfaces > 1)
        {
            warnOnce("the scene has more than one NavMeshSurface: agents walk the first one");
        }
        std::shared_ptr<const asset::NavMeshData> data = wanted.isValid() && config.navMeshes ? config.navMeshes(wanted) : nullptr;
        if (data == navMeshData && wanted == navMeshId)
        {
            return;
        }
        clearMesh();
        navMeshId = wanted;
        if (data == nullptr)
        {
            if (wanted.isValid())
            {
                warnOnce("the navigation mesh of the scene cannot be loaded: bake it in the NavMeshSurface");
            }
            return;
        }
        core::Result<std::unique_ptr<detail::TileCache>> made = detail::TileCache::create(*data, config.maxObstacles);
        if (!made)
        {
            warnOnce(std::format("the navigation mesh cannot be used: {}", made.error().message));
            return;
        }
        cache = std::move(*made);
        navMeshData = std::move(data);
        crowd = dtAllocCrowd();
        if (!crowd->init(config.maxAgents, std::max(navMeshData->settings.agentRadius * 2.0f, 1.0f), &cache->mesh()))
        {
            warnOnce("the crowd of agents cannot be made");
            clearMesh();
            return;
        }
        // Four qualities of avoidance, from the cheapest.
        dtObstacleAvoidanceParams avoidance = *crowd->getObstacleAvoidanceParams(0);
        constexpr std::array<std::array<unsigned char, 3>, 4> qualities{{{5, 2, 1}, {5, 2, 2}, {7, 2, 3}, {7, 3, 3}}};
        for (std::size_t quality = 0; quality < qualities.size(); ++quality)
        {
            avoidance.velBias = 0.5f;
            avoidance.adaptiveDivs = qualities[quality][0];
            avoidance.adaptiveRings = qualities[quality][1];
            avoidance.adaptiveDepth = qualities[quality][2];
            crowd->setObstacleAvoidanceParams(static_cast<int>(quality), &avoidance);
        }
    }

    void syncObstacles(scene::Scene& sceneToRead)
    {
        dtTileCache& tiles = cache->tiles();
        for (auto& [key, obstacle] : obstacles)
        {
            obstacle.seen = false;
        }
        for ([[maybe_unused]] auto [entity, component] : sceneToRead.view<scene::NavMeshObstacle>())
        {
            const math::Mat4 world = worldOf(sceneToRead, entity);
            const math::Vec3 scale = scaleOf(world);
            const math::Vec3 center(world * math::Vec4(component.center, 1.0f));
            const math::Vec3 half = math::abs(component.size) * scale * 0.5f;
            const float yaw = yawOf(world);
            Signature signature;
            signature.add(component.shape);
            signature.add(half);
            Obstacle& obstacle = obstacles[keyOf(entity)];
            obstacle.seen = true;
            const bool moved = math::length(center - obstacle.position) > obstacleMoveThreshold ||
                               std::abs(std::remainder(yaw - obstacle.yaw, 2.0f * std::numbers::pi_v<float>)) > obstacleTurnThreshold;
            if (obstacle.reference != 0 && !moved && obstacle.signature == signature.value())
            {
                continue;
            }
            if (obstacle.reference != 0 && dtStatusFailed(tiles.removeObstacle(obstacle.reference)))
            {
                // Too many changes this frame: next frame.
                continue;
            }
            obstacle.reference = 0;
            dtStatus added = DT_FAILURE;
            if (component.shape == scene::NavObstacleShape::Box)
            {
                added = tiles.addBoxObstacle(&center.x, &half.x, yaw, &obstacle.reference);
            }
            else
            {
                const math::Vec3 base = center - math::Vec3{0.0f, half.y, 0.0f};
                added = tiles.addObstacle(&base.x, std::max(half.x, half.z), half.y * 2.0f, &obstacle.reference);
            }
            if (dtStatusFailed(added))
            {
                obstacle.reference = 0;
                if (dtStatusDetail(added, DT_OUT_OF_MEMORY))
                {
                    warnOnce(std::format("more than {} obstacles cut the navigation mesh", config.maxObstacles));
                }
                continue;
            }
            obstacle.position = center;
            obstacle.yaw = yaw;
            obstacle.signature = signature.value();
        }
        for (auto obstacle = obstacles.begin(); obstacle != obstacles.end();)
        {
            if (!obstacle->second.seen)
            {
                if (obstacle->second.reference != 0 && dtStatusFailed(tiles.removeObstacle(obstacle->second.reference)))
                {
                    ++obstacle;
                    continue;
                }
                obstacle = obstacles.erase(obstacle);
            }
            else
            {
                ++obstacle;
            }
        }
    }

    // Asks the crowd to take an agent to its destination, on the mesh.
    void request(Agent& agent)
    {
        if (!agent.destination || agent.index < 0)
        {
            return;
        }
        dtPolyRef reference = 0;
        float nearest[3]{};
        cache->query().findNearestPoly(&agent.destination->x, searchExtents.data(), &filter, &reference, nearest);
        if (reference == 0)
        {
            agent.destination.reset();
            agent.pending = false;
            return;
        }
        agent.destination = fromDetour(nearest);
        agent.pending = !crowd->requestMoveTarget(agent.index, reference, nearest);
    }

    void addAgent(scene::Scene& sceneToRead, Agent& agent, const scene::NavMeshAgent& component, math::Vec3 position)
    {
        const dtCrowdAgentParams params = paramsOf(component);
        agent.index = crowd->addAgent(&position.x, &params);
        agent.signature = signatureOf(component);
        agent.lastPosition = position;
        if (agent.index < 0)
        {
            warnOnce(std::format("more than {} agents walk the navigation mesh", config.maxAgents));
            return;
        }
        const dtCrowdAgent* const added = crowd->getAgent(agent.index);
        if (added->state == DT_CROWDAGENT_STATE_INVALID)
        {
            warnOnce(std::format("the agent '{}' stands off the navigation mesh", sceneToRead.name(agent.entity)));
        }
        agent.pending = agent.destination.has_value();
    }

    void syncAgents(scene::Scene& sceneToRead)
    {
        for (auto& [key, agent] : agents)
        {
            agent.seen = false;
        }
        for ([[maybe_unused]] auto [entity, component] : sceneToRead.view<scene::NavMeshAgent>())
        {
            Agent& agent = agents[keyOf(entity)];
            agent.entity = entity;
            agent.seen = true;
            const math::Vec3 position(worldOf(sceneToRead, entity)[3]);
            if (agent.index < 0)
            {
                addAgent(sceneToRead, agent, component, position);
            }
            else if (math::length(position - agent.lastPosition) > moveTolerance)
            {
                // Game code moved it: it starts again from there.
                crowd->removeAgent(agent.index);
                addAgent(sceneToRead, agent, component, position);
            }
            else if (agent.signature != signatureOf(component))
            {
                const dtCrowdAgentParams params = paramsOf(component);
                crowd->updateAgentParameters(agent.index, &params);
                agent.signature = signatureOf(component);
            }
            if (agent.pending)
            {
                request(agent);
            }
        }
        for (auto agent = agents.begin(); agent != agents.end();)
        {
            if (!agent->second.seen)
            {
                if (agent->second.index >= 0 && crowd != nullptr)
                {
                    crowd->removeAgent(agent->second.index);
                }
                agent = agents.erase(agent);
            }
            else
            {
                ++agent;
            }
        }
    }

    // Writes where the agents walked, turns them towards where they go, and stops those that arrived.
    void writeAgents(scene::Scene& sceneToWrite, float seconds)
    {
        for (auto& [key, agent] : agents)
        {
            scene::NavMeshAgent* const component = sceneToWrite.tryGet<scene::NavMeshAgent>(agent.entity);
            const dtCrowdAgent* const walked = agent.index >= 0 ? crowd->getAgent(agent.index) : nullptr;
            if (component == nullptr || walked == nullptr || !walked->active)
            {
                continue;
            }
            const math::Vec3 position = fromDetour(walked->npos);
            const math::Vec3 velocity = fromDetour(walked->vel);
            std::optional<float> yaw;
            const math::Vec2 flat{velocity.x, velocity.z};
            if (component->angularSpeed > 0.0f && math::length(flat) > 0.1f)
            {
                const math::Mat4 world = worldOf(sceneToWrite, agent.entity);
                const math::Vec3 forward = -math::normalize(math::Vec3(world[2]));
                const float current = std::atan2(-forward.x, -forward.z);
                const float wanted = std::atan2(-flat.x, -flat.y);
                const float difference = std::remainder(wanted - current, 2.0f * std::numbers::pi_v<float>);
                const float step = std::min(std::abs(difference), component->angularSpeed * seconds);
                yaw = current + std::copysign(step, difference);
            }
            placeEntity(sceneToWrite, agent.entity, position, yaw);
            agent.lastPosition = position;
            component->velocity = velocity;

            if (agent.destination && !agent.pending)
            {
                const math::Vec3 toTarget = *agent.destination - position;
                const float flatDistance = math::length(math::Vec2{toTarget.x, toTarget.z});
                if (flatDistance <= std::max(component->stoppingDistance, 0.05f) && std::abs(toTarget.y) < component->height)
                {
                    crowd->resetMoveTarget(agent.index);
                    agent.destination.reset();
                }
            }
        }
    }

    [[nodiscard]] const Agent* find(Entity entity) const
    {
        const auto found = agents.find(keyOf(entity));
        return found != agents.end() ? &found->second : nullptr;
    }

    [[nodiscard]] std::vector<math::Vec3> straightPath(const float* start, const float* end, const dtPolyRef* polygons, int count) const
    {
        std::vector<float> points(static_cast<std::size_t>(maxPathPolygons) * 3);
        int pointCount = 0;
        cache->query().findStraightPath(start, end, polygons, count, points.data(), nullptr, nullptr, &pointCount, maxPathPolygons);
        std::vector<math::Vec3> corners;
        for (int point = 0; point < pointCount; ++point)
        {
            corners.push_back(fromDetour(&points[static_cast<std::size_t>(point) * 3]));
        }
        return corners;
    }
};

core::Result<std::unique_ptr<NavigationWorld>> NavigationWorld::create(NavigationWorldConfig config)
{
    config.maxAgents = std::clamp(config.maxAgents, 1, 4096);
    config.maxObstacles = std::clamp(config.maxObstacles, 1, 4096);
    return std::unique_ptr<NavigationWorld>(new NavigationWorld(std::make_unique<Implementation>(std::move(config))));
}

NavigationWorld::NavigationWorld(std::unique_ptr<Implementation> implementation) noexcept
    : m_implementation(std::move(implementation))
{
}

NavigationWorld::~NavigationWorld() = default;

void NavigationWorld::update(scene::Scene& scene, core::Duration delta)
{
    Implementation& world = *m_implementation;
    if (world.scene != &scene)
    {
        if (world.scene != nullptr)
        {
            world.clearMesh();
            world.agents.clear();
            world.navMeshId = {};
        }
        world.scene = &scene;
    }
    world.syncMesh(scene);
    if (world.cache == nullptr)
    {
        return;
    }
    const auto seconds = static_cast<float>(std::max(delta.count(), 0.0));
    world.syncObstacles(scene);
    world.cache->tiles().update(seconds, &world.cache->mesh());
    world.syncAgents(scene);
    if (seconds > 0.0f)
    {
        world.crowd->update(seconds, nullptr);
    }
    world.writeAgents(scene, seconds);
}

bool NavigationWorld::setDestination(scene::Entity agent, math::Vec3 destination)
{
    Implementation& world = *m_implementation;
    if (world.cache != nullptr)
    {
        dtPolyRef reference = 0;
        float nearest[3]{};
        world.cache->query().findNearestPoly(&destination.x, searchExtents.data(), &world.filter, &reference, nearest);
        if (reference == 0)
        {
            return false;
        }
    }
    Implementation::Agent& record = world.agents[keyOf(agent)];
    record.entity = agent;
    record.destination = destination;
    record.pending = true;
    if (world.cache != nullptr && record.index >= 0)
    {
        world.request(record);
    }
    return true;
}

void NavigationWorld::stop(scene::Entity agent)
{
    Implementation& world = *m_implementation;
    const auto found = world.agents.find(keyOf(agent));
    if (found == world.agents.end())
    {
        return;
    }
    found->second.destination.reset();
    found->second.pending = false;
    if (world.crowd != nullptr && found->second.index >= 0)
    {
        world.crowd->resetMoveTarget(found->second.index);
    }
}

bool NavigationWorld::hasDestination(scene::Entity agent) const
{
    const Implementation::Agent* const record = m_implementation->find(agent);
    return record != nullptr && record->destination.has_value();
}

std::optional<math::Vec3> NavigationWorld::destination(scene::Entity agent) const
{
    const Implementation::Agent* const record = m_implementation->find(agent);
    return record != nullptr ? record->destination : std::nullopt;
}

std::vector<math::Vec3> NavigationWorld::path(scene::Entity agent) const
{
    const Implementation& world = *m_implementation;
    const Implementation::Agent* const record = world.find(agent);
    if (record == nullptr || !record->destination || record->index < 0 || world.crowd == nullptr)
    {
        return {};
    }
    const dtCrowdAgent* const walked = world.crowd->getAgent(record->index);
    if (walked == nullptr || !walked->active || walked->corridor.getPathCount() == 0)
    {
        return {};
    }
    return world.straightPath(walked->npos, walked->corridor.getTarget(), walked->corridor.getPath(), walked->corridor.getPathCount());
}

float NavigationWorld::remainingDistance(scene::Entity agent) const
{
    const std::vector<math::Vec3> corners = path(agent);
    float distance = 0.0f;
    for (std::size_t corner = 1; corner < corners.size(); ++corner)
    {
        distance += math::length(corners[corner] - corners[corner - 1]);
    }
    return distance;
}

std::vector<math::Vec3> NavigationWorld::findPath(math::Vec3 from, math::Vec3 to) const
{
    const Implementation& world = *m_implementation;
    if (world.cache == nullptr)
    {
        return {};
    }
    const dtNavMeshQuery& query = world.cache->query();
    dtPolyRef start = 0;
    dtPolyRef end = 0;
    float startPoint[3]{};
    float endPoint[3]{};
    query.findNearestPoly(&from.x, searchExtents.data(), &world.filter, &start, startPoint);
    query.findNearestPoly(&to.x, searchExtents.data(), &world.filter, &end, endPoint);
    if (start == 0 || end == 0)
    {
        return {};
    }
    std::array<dtPolyRef, maxPathPolygons> polygons{};
    int count = 0;
    query.findPath(start, end, startPoint, endPoint, &world.filter, polygons.data(), &count, maxPathPolygons);
    if (count == 0)
    {
        return {};
    }
    // A path that stops short ends at the point of its last polygon closest to the destination.
    if (polygons[static_cast<std::size_t>(count - 1)] != end)
    {
        query.closestPointOnPoly(polygons[static_cast<std::size_t>(count - 1)], endPoint, endPoint, nullptr);
    }
    return world.straightPath(startPoint, endPoint, polygons.data(), count);
}

std::optional<math::Vec3> NavigationWorld::samplePosition(math::Vec3 point, float maxDistance) const
{
    const Implementation& world = *m_implementation;
    if (world.cache == nullptr || !(maxDistance > 0.0f))
    {
        return std::nullopt;
    }
    const std::array<float, 3> extents{maxDistance, maxDistance, maxDistance};
    dtPolyRef reference = 0;
    float nearest[3]{};
    world.cache->query().findNearestPoly(&point.x, extents.data(), &world.filter, &reference, nearest);
    if (reference == 0 || math::length(fromDetour(nearest) - point) > maxDistance)
    {
        return std::nullopt;
    }
    return fromDetour(nearest);
}

std::optional<NavHit> NavigationWorld::raycast(math::Vec3 from, math::Vec3 to) const
{
    const Implementation& world = *m_implementation;
    if (world.cache == nullptr)
    {
        return std::nullopt;
    }
    const dtNavMeshQuery& query = world.cache->query();
    dtPolyRef start = 0;
    float startPoint[3]{};
    query.findNearestPoly(&from.x, searchExtents.data(), &world.filter, &start, startPoint);
    if (start == 0)
    {
        return std::nullopt;
    }
    float along = FLT_MAX;
    float normal[3]{};
    std::array<dtPolyRef, maxPathPolygons> polygons{};
    int count = 0;
    query.raycast(start, startPoint, &to.x, &world.filter, &along, normal, polygons.data(), &count, maxPathPolygons);
    if (along == FLT_MAX || along > 1.0f)
    {
        return std::nullopt;
    }
    const math::Vec3 origin = fromDetour(startPoint);
    return NavHit{.position = origin + (to - origin) * along, .normal = fromDetour(normal), .distance = math::length(to - origin) * along};
}

bool NavigationWorld::hasNavMesh() const noexcept
{
    return m_implementation->cache != nullptr;
}

std::size_t NavigationWorld::agentCount() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(m_implementation->agents, [](const auto& entry) { return entry.second.index >= 0; }));
}

std::size_t NavigationWorld::obstacleCount() const noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(m_implementation->obstacles, [](const auto& entry) { return entry.second.reference != 0; }));
}

} // namespace devex::navigation
