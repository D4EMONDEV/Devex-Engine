#include <devex/physics2d/Physics2DWorld.hpp>

#include <devex/core/Hash.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Physics2DComponents.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <box2d/box2d.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace devex::physics2d {
namespace {

using scene::BodyType;
using scene::Entity;

// Distances and angles below which a pose written by the simulation counts as unchanged.
constexpr float positionTolerance = 1e-4f;
constexpr float angleTolerance = 1e-4f;
// How thick the one-way ledge of a top tile is, in cells.
constexpr float ledgeThickness = 0.1f;
// The skin a character keeps from what it touches, and how far below its feet it looks for ground.
constexpr float groundProbe = 0.05f;
// How far beside a corner under a character the top it stands on is looked for.
constexpr float cornerReach = 0.02f;
// How deep Box2D lets shapes overlap, in meters: its linear slop.
constexpr float linearSlop = 0.005f;
constexpr int moverIterations = 5;

// Category bits of shapes: the 16 collision layers of the project for solid shapes and characters,
// then one bit for one-way ledges and one for triggers, which solid shapes list in their masks: a
// ledge holds up bodies but not characters, which the mover lands on it itself, and triggers detect
// what they overlap without blocking it.
constexpr std::uint64_t oneWayBit = 1ull << 16;
constexpr std::uint64_t triggerBit = 1ull << 18;

// The user data of shapes: flags, then the collision layer from the ninth bit.
constexpr std::uintptr_t triggerFlag = 1;
constexpr std::uintptr_t oneWayFlag = 2;
constexpr int layerShift = 8;

// ---- Conversions -------------------------------------------------------------------------------

[[nodiscard]] b2Vec2 toBox(math::Vec2 value) noexcept
{
    return {value.x, value.y};
}

[[nodiscard]] math::Vec2 fromBox(b2Vec2 value) noexcept
{
    return {value.x, value.y};
}

// Entities are stored in the user data of bodies.
[[nodiscard]] std::uint64_t keyOf(Entity entity) noexcept
{
    return static_cast<std::uint64_t>(entity.generation) << 32 | entity.index;
}

[[nodiscard]] Entity entityOf(std::uint64_t key) noexcept
{
    Entity entity;
    entity.index = static_cast<std::uint32_t>(key);
    entity.generation = static_cast<std::uint32_t>(key >> 32);
    return entity;
}

[[nodiscard]] std::uintptr_t flagsOf(b2ShapeId shape) noexcept
{
    return reinterpret_cast<std::uintptr_t>(b2Shape_GetUserData(shape));
}

[[nodiscard]] void* userDataOf(std::uint32_t layer, bool trigger, bool oneWay) noexcept
{
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(layer) << layerShift | (trigger ? triggerFlag : 0) |
                                   (oneWay ? oneWayFlag : 0));
}

[[nodiscard]] bool inLayers(b2ShapeId shape, std::uint16_t layers) noexcept
{
    return (layers >> (flagsOf(shape) >> layerShift) & 1u) != 0;
}

// Where a mover touches a shape: Box2D gives the point in the frame of the shape's body.
[[nodiscard]] math::Vec2 contactPoint(b2ShapeId shape, const b2PlaneResult& result) noexcept
{
    return fromBox(b2Body_GetWorldPoint(b2Shape_GetBody(shape), result.point));
}

[[nodiscard]] Entity ownerOf(b2ShapeId shape) noexcept
{
    return entityOf(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(shape)))));
}

// The angle around Z of a world matrix: where its X axis points.
[[nodiscard]] float angleOf(const math::Mat4& matrix) noexcept
{
    return std::atan2(matrix[0][1], matrix[0][0]);
}

[[nodiscard]] float angleDifference(float first, float second) noexcept
{
    return std::abs(std::remainder(first - second, 2.0f * 3.14159265358979f));
}

[[nodiscard]] math::Mat4 worldMatrixOf(const scene::Scene& scene, Entity entity) noexcept
{
    for (Entity current = entity; current.isValid(); current = scene.parent(current))
    {
        if (const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(current))
        {
            return world->matrix;
        }
    }
    return math::Mat4{1.0f};
}

// The frame of a body: its position in XY and its angle around Z, without scale.
[[nodiscard]] math::Mat4 frameOf(math::Vec2 position, float angle) noexcept
{
    return math::translate(math::Mat4{1.0f}, math::Vec3{position, 0.0f}) *
           math::mat4_cast(math::angleAxis(angle, math::Vec3{0.0f, 0.0f, 1.0f}));
}

// Places an entity in the plane, keeping its Z, its scale and the turns it has around other axes
// than Z, by changing its Transform relative to its parent and its world transform.
void setWorldPose(scene::Scene& scene, Entity entity, math::Vec2 position, std::optional<float> angle)
{
    scene::Transform* const local = scene.tryGet<scene::Transform>(entity);
    if (local == nullptr)
    {
        return;
    }
    scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    const math::Trs current = math::decomposeTrs(world != nullptr ? world->matrix : math::Mat4{1.0f});
    math::Quat rotation = current.rotation;
    if (angle)
    {
        const float turn = *angle - angleOf(world != nullptr ? world->matrix : math::Mat4{1.0f});
        rotation = math::normalize(math::angleAxis(turn, math::Vec3{0.0f, 0.0f, 1.0f}) * rotation);
    }
    const math::Mat4 target = math::composeTrs({math::Vec3{position, current.translation.z}, rotation, current.scale});
    const math::Trs relative = math::decomposeTrs(math::inverse(worldMatrixOf(scene, scene.parent(entity))) * target);
    local->position = relative.translation;
    if (angle)
    {
        local->rotation = math::normalize(relative.rotation);
    }
    if (world != nullptr)
    {
        world->matrix = target;
    }
}

// Recomputes the world transforms below an entity whose world transform changed.
void updateDescendants(scene::Scene& scene, Entity entity, const math::Mat4& world)
{
    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        const scene::Transform* const local = scene.tryGet<scene::Transform>(child);
        const math::Mat4 childWorld = local != nullptr ? world * local->matrix() : world;
        if (scene::WorldTransform* const childTransform = scene.tryGet<scene::WorldTransform>(child))
        {
            childTransform->matrix = childWorld;
        }
        updateDescendants(scene, child, childWorld);
    }
}

// ---- Descriptions of bodies --------------------------------------------------------------------

enum class ShapeKind : std::uint8_t
{
    Polygon,
    Circle,
    Capsule,
};

// One shape of a body, already placed in the frame of the body: the points of a polygon, the two
// centers of a capsule, or the center of a circle, with the radius of the last two.
struct ShapePart
{
    ShapeKind kind = ShapeKind::Polygon;
    std::vector<math::Vec2> points;
    float radius = 0.0f;
    bool trigger = false;
    bool oneWay = false;
    Entity entity;
};

struct BodyDescription
{
    Entity owner;
    bool hasRigidBody = false;
    scene::RigidBody2D rigidBody;
    std::vector<ShapePart> parts;
    math::Vec2 position{0.0f};
    float angle = 0.0f;
    std::uint64_t signature = 0;
};

class Signature
{
public:
    template <typename T>
    void add(const T& value) noexcept
    {
        m_value = core::hash64(std::as_bytes(std::span(&value, 1)), m_value);
    }

    // Placements come from world matrices, whose rounding changes as a body turns: they count to a
    // tenth of a millimeter, so that only real changes rebuild the body.
    void addPlacement(float value) noexcept
    {
        add(static_cast<std::int64_t>(std::lround(value * 10000.0f)));
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return m_value;
    }

private:
    std::uint64_t m_value = 0x9E3779B97F4A7C15ull;
};

[[nodiscard]] std::uint64_t signatureOf(const BodyDescription& description) noexcept
{
    Signature signature;
    const scene::RigidBody2D& body = description.rigidBody;
    signature.add(description.hasRigidBody);
    signature.add(body.type);
    signature.add(body.mass);
    signature.add(body.friction);
    signature.add(body.restitution);
    signature.add(body.linearDamping);
    signature.add(body.angularDamping);
    signature.add(body.gravityScale);
    signature.add(body.layer);
    signature.add(body.fixedRotation);
    signature.add(body.continuousCollision);
    for (const ShapePart& part : description.parts)
    {
        signature.add(part.kind);
        signature.add(part.trigger);
        signature.add(part.oneWay);
        signature.addPlacement(part.radius);
        for (const math::Vec2 point : part.points)
        {
            signature.addPlacement(point.x);
            signature.addPlacement(point.y);
        }
    }
    return signature.value();
}

// The entity whose body a collider belongs to.
[[nodiscard]] Entity bodyOwner(const scene::Scene& scene, Entity collider) noexcept
{
    for (Entity current = collider; current.isValid(); current = scene.parent(current))
    {
        if (scene.has<scene::RigidBody2D>(current))
        {
            return current;
        }
    }
    return collider;
}

[[nodiscard]] float areaOf(const ShapePart& part) noexcept
{
    switch (part.kind)
    {
    case ShapeKind::Circle:
        return 3.14159265f * part.radius * part.radius;
    case ShapeKind::Capsule: {
        const float length = part.points.size() == 2 ? math::length(part.points[1] - part.points[0]) : 0.0f;
        return 3.14159265f * part.radius * part.radius + 2.0f * part.radius * length;
    }
    case ShapeKind::Polygon:
        break;
    }
    float twice = 0.0f;
    for (std::size_t index = 0; index < part.points.size(); ++index)
    {
        const math::Vec2 a = part.points[index];
        const math::Vec2 b = part.points[(index + 1) % part.points.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return std::abs(twice) * 0.5f;
}

// ---- Records of simulated objects --------------------------------------------------------------

struct BodyRecord
{
    Entity owner;
    BodyType type = BodyType::Static;
    b2BodyId body = b2_nullBodyId;
    std::uint64_t signature = 0;
    // The pose and velocities last read from or written to the scene, to notice game code changing them.
    math::Vec2 position{0.0f};
    float angle = 0.0f;
    math::Vec2 linearVelocity{0.0f};
    float angularVelocity = 0.0f;
    // The pose of the previous step, for interpolation.
    math::Vec2 previousPosition{0.0f};
    float previousAngle = 0.0f;
    bool seen = false;
};

struct CharacterRecord
{
    Entity owner;
    // A kinematic capsule that follows the character, for what the character touches to feel it.
    b2BodyId body = b2_nullBodyId;
    std::uint64_t signature = 0;
    math::Vec2 position{0.0f};
    math::Vec2 previousPosition{0.0f};
    // The velocity of what the character stands on, which carries it.
    math::Vec2 groundVelocity{0.0f};
    bool seen = false;
};

// The rectangles of a tilemap collider, kept while its cells and its tileset stay the same.
struct TileCache
{
    std::uint64_t key = 0;
    std::vector<TileRectangle> rectangles;
};

// ---- The character mover -----------------------------------------------------------------------

// What a character accepts to touch: never its own body nor triggers; a one-way ledge only from
// above, when the feet were over it as the move began.
struct MoverFilter
{
    b2BodyId self = b2_nullBodyId;
    b2QueryFilter solids{};
    b2QueryFilter ledges{};
    float feet = 0.0f;
    float walkable = 0.7f;

    [[nodiscard]] bool accepts(b2ShapeId shape, math::Vec2 normal, math::Vec2 point, math::Vec2 motion) const noexcept
    {
        if (B2_ID_EQUALS(b2Shape_GetBody(shape), self))
        {
            return false;
        }
        const std::uintptr_t flags = flagsOf(shape);
        if ((flags & triggerFlag) != 0)
        {
            return false;
        }
        if ((flags & oneWayFlag) != 0)
        {
            return normal.y >= walkable && feet >= point.y - 0.02f && math::dot(motion, normal) <= 0.0f;
        }
        return true;
    }
};

struct PlaneGathering
{
    const MoverFilter* filter = nullptr;
    math::Vec2 motion{0.0f};
    std::vector<b2CollisionPlane>* planes = nullptr;
    // The shapes of dynamic bodies the character ran into, which it pushes.
    std::vector<std::pair<b2ShapeId, math::Vec2>>* pushed = nullptr;
};

bool gatherPlane(b2ShapeId shape, const b2PlaneResult* result, void* context)
{
    auto& gathering = *static_cast<PlaneGathering*>(context);
    if (!result->hit || !gathering.filter->accepts(shape, fromBox(result->plane.normal), contactPoint(shape, *result), gathering.motion))
    {
        return true;
    }
    gathering.planes->push_back({result->plane, FLT_MAX, 0.0f, true});
    if (gathering.pushed != nullptr && b2Body_GetType(b2Shape_GetBody(shape)) == b2_dynamicBody)
    {
        gathering.pushed->emplace_back(shape, fromBox(result->plane.normal));
    }
    return true;
}

struct SweepResult
{
    const MoverFilter* filter = nullptr;
    math::Vec2 motion{0.0f};
    float fraction = 1.0f;
};

float sweepHit(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context)
{
    auto& sweep = *static_cast<SweepResult*>(context);
    // A ledge touched from the start gives neither normal nor point on its top: it stops the fall
    // of feet standing on its top, and lets through feet inside it.
    const bool accepted = fraction > 0.0f ? sweep.filter->accepts(shape, fromBox(normal), fromBox(point), sweep.motion)
                                          : sweep.filter->accepts(shape, math::Vec2{0.0f, 1.0f},
                                                                  math::Vec2{point.x, b2Shape_GetAABB(shape).upperBound.y},
                                                                  sweep.motion);
    if (!accepted)
    {
        return -1.0f;
    }
    sweep.fraction = std::min(sweep.fraction, fraction);
    return fraction;
}

// One-way ledges hold up what lands on them from above: a dynamic body touches them only while the
// normal from the ledge points up at it, and while it is not already deep inside.
bool preSolve(b2ShapeId shapeA, b2ShapeId shapeB, b2Manifold* manifold, void* /*context*/)
{
    const bool ledgeA = (flagsOf(shapeA) & oneWayFlag) != 0;
    const bool ledgeB = (flagsOf(shapeB) & oneWayFlag) != 0;
    if (!ledgeA && !ledgeB)
    {
        return true;
    }
    // The normal points from A to B.
    const float up = ledgeA ? manifold->normal.y : -manifold->normal.y;
    if (up < 0.7f)
    {
        return false;
    }
    float separation = FLT_MAX;
    for (int point = 0; point < manifold->pointCount; ++point)
    {
        separation = std::min(separation, manifold->points[point].separation);
    }
    return separation > -0.05f;
}

} // namespace

std::vector<TileRectangle> tileRectangles(const scene::TileGrid& grid,
                                          const std::function<asset::TileCollision(std::uint32_t tile)>& collisionOf)
{
    // Rows of cells, bottom to top, each from left to right.
    std::map<std::pair<std::int32_t, std::int32_t>, asset::TileCollision> cells;
    for (const scene::TileCell& cell : grid.cells())
    {
        const asset::TileCollision collision = collisionOf(scene::tileIdOf(cell.value));
        if (collision != asset::TileCollision::None)
        {
            cells[{cell.cell.y, cell.cell.x}] = collision;
        }
    }
    std::vector<TileRectangle> rectangles;
    // Full rectangles still growing upwards, by their columns.
    std::map<std::pair<std::int32_t, std::int32_t>, TileRectangle> open;
    std::int32_t row = std::numeric_limits<std::int32_t>::min();
    const auto closeBelow = [&](std::int32_t current) {
        for (auto rectangle = open.begin(); rectangle != open.end();)
        {
            if (rectangle->second.cell.y + rectangle->second.size.y < current)
            {
                rectangles.push_back(rectangle->second);
                rectangle = open.erase(rectangle);
            }
            else
            {
                ++rectangle;
            }
        }
    };
    for (auto cell = cells.begin(); cell != cells.end();)
    {
        const std::int32_t y = cell->first.first;
        if (y != row)
        {
            closeBelow(y);
            row = y;
        }
        // A run of the same collision along the row.
        const std::int32_t first = cell->first.second;
        const asset::TileCollision collision = cell->second;
        std::int32_t last = first;
        auto next = std::next(cell);
        while (next != cells.end() && next->first.first == y && next->first.second == last + 1 && next->second == collision)
        {
            last = next->first.second;
            ++next;
        }
        cell = next;
        if (collision == asset::TileCollision::Top)
        {
            rectangles.push_back({.cell = {first, y}, .size = {last - first + 1, 1}, .oneWay = true});
            continue;
        }
        const auto growing = open.find({first, last});
        if (growing != open.end() && growing->second.cell.y + growing->second.size.y == y)
        {
            ++growing->second.size.y;
            continue;
        }
        if (growing != open.end())
        {
            rectangles.push_back(growing->second);
            open.erase(growing);
        }
        open[{first, last}] = {.cell = {first, y}, .size = {last - first + 1, 1}};
    }
    for (const auto& [columns, rectangle] : open)
    {
        rectangles.push_back(rectangle);
    }
    return rectangles;
}

struct Physics2DWorld::Implementation
{
    explicit Implementation(Physics2DWorldConfig worldConfig)
        : config(std::move(worldConfig))
    {
        b2WorldDef definition = b2DefaultWorldDef();
        definition.gravity = b2Vec2{config.settings.gravity.x, config.settings.gravity.y};
        world = b2CreateWorld(&definition);
        b2World_SetPreSolveCallback(world, &preSolve, nullptr);
    }

    ~Implementation()
    {
        b2DestroyWorld(world);
    }

    Implementation(const Implementation&) = delete;
    Implementation& operator=(const Implementation&) = delete;

    Physics2DWorldConfig config;
    b2WorldId world = b2_nullWorldId;
    std::unordered_map<std::uint64_t, BodyRecord> bodies;
    std::unordered_map<std::uint64_t, CharacterRecord> characters;
    std::unordered_map<std::uint64_t, TileCache> tileCaches;
    // The entity of each shape, by its stored id.
    std::unordered_map<std::uint64_t, Entity> shapeOwners;
    // The pairs of shapes touching, by their stored ids, and how many touch between two entities:
    // a contact begins with the first pair and ends with the last. The pairs of destroyed shapes end
    // with them, whenever Box2D reports it.
    std::map<std::pair<std::uint64_t, std::uint64_t>, Contact> touches;
    std::unordered_map<std::uint64_t, int> contactCounts;
    std::vector<Contact> contacts;
    std::unordered_set<std::uint64_t> warnings;
    const scene::Scene* simulatedScene = nullptr;

    void warnOnce(Entity entity, std::string_view problem, const std::string& message)
    {
        if (warnings.insert(keyOf(entity) ^ core::hash64(problem)).second)
        {
            DEVEX_LOG_WARNING("{}", message);
        }
    }

    // The layers a layer collides with.
    [[nodiscard]] std::uint64_t collisionsOf(std::uint32_t layer) const noexcept
    {
        std::uint64_t mask = 0;
        for (std::uint32_t other = 0; other < asset::physicsLayerCount; ++other)
        {
            if (config.settings.collides(layer, other))
            {
                mask |= 1ull << other;
            }
        }
        return mask;
    }

    [[nodiscard]] static std::uint32_t clampLayer(std::uint32_t layer) noexcept
    {
        return std::min<std::uint32_t>(layer, asset::physicsLayerCount - 1);
    }

    [[nodiscard]] b2Filter filterOf(std::uint32_t layer, bool trigger, bool oneWay) const noexcept
    {
        b2Filter filter = b2DefaultFilter();
        filter.maskBits = collisionsOf(layer);
        if (trigger)
        {
            filter.categoryBits = triggerBit;
        }
        else if (oneWay)
        {
            filter.categoryBits = oneWayBit;
        }
        else
        {
            filter.categoryBits = 1ull << layer;
            filter.maskBits |= oneWayBit | triggerBit;
        }
        return filter;
    }

    // What a character of a layer moves against: the solid shapes and the other characters of the
    // layers it collides with, or the ledges of these layers.
    [[nodiscard]] b2QueryFilter moverFilterOf(std::uint32_t layer, bool ledges) const noexcept
    {
        return b2QueryFilter{.categoryBits = 1ull << layer, .maskBits = ledges ? oneWayBit : collisionsOf(layer)};
    }

    // Queries of game code see every shape, then keep those of their layers.
    static constexpr b2QueryFilter everything{.categoryBits = ~0ull, .maskBits = ~0ull};

    void clear()
    {
        for (auto& [key, record] : bodies)
        {
            b2DestroyBody(record.body);
        }
        for (auto& [key, record] : characters)
        {
            b2DestroyBody(record.body);
        }
        bodies.clear();
        characters.clear();
        tileCaches.clear();
        shapeOwners.clear();
        touches.clear();
        contactCounts.clear();
        contacts.clear();
    }

    // Destroys a body; what its shapes touched stops touching them.
    void destroyBody(b2BodyId body)
    {
        std::vector<b2ShapeId> shapes(static_cast<std::size_t>(b2Body_GetShapeCount(body)));
        shapes.resize(static_cast<std::size_t>(b2Body_GetShapes(body, shapes.data(), static_cast<int>(shapes.size()))));
        std::unordered_set<std::uint64_t> retired;
        for (const b2ShapeId shape : shapes)
        {
            retired.insert(b2StoreShapeId(shape));
            shapeOwners.erase(b2StoreShapeId(shape));
        }
        for (auto touch = touches.begin(); touch != touches.end();)
        {
            if (retired.contains(touch->first.first) || retired.contains(touch->first.second))
            {
                release(touch->second);
                touch = touches.erase(touch);
            }
            else
            {
                ++touch;
            }
        }
        b2DestroyBody(body);
    }

    // ---- Descriptions from the scene ----

    // Adds a collider to the body of its owner, its points brought into the frame of that body.
    void describeCollider(scene::Scene& scene, std::unordered_map<std::uint64_t, BodyDescription>& descriptions, Entity entity,
                          ShapePart part)
    {
        if (scene.has<scene::CharacterController2D>(entity))
        {
            return;
        }
        const Entity owner = bodyOwner(scene, entity);
        BodyDescription& description = descriptions[keyOf(owner)];
        if (!description.owner.isValid())
        {
            description.owner = owner;
            if (const scene::RigidBody2D* const rigidBody = scene.tryGet<scene::RigidBody2D>(owner))
            {
                description.hasRigidBody = true;
                description.rigidBody = *rigidBody;
            }
            const math::Mat4 ownerWorld = worldMatrixOf(scene, owner);
            description.position = math::Vec2(ownerWorld[3]);
            description.angle = angleOf(ownerWorld);
        }
        const math::Mat4 toBody = math::inverse(frameOf(description.position, description.angle)) * worldMatrixOf(scene, entity);
        const auto place = [&](math::Vec2 point) { return math::Vec2(toBody * math::Vec4(point, 0.0f, 1.0f)); };
        for (math::Vec2& point : part.points)
        {
            point = place(point);
        }
        // Circles and capsules stay round: their radius follows the larger scale of the plane.
        const float scale = std::max(math::length(math::Vec2(toBody[0])), math::length(math::Vec2(toBody[1])));
        part.radius *= scale;
        part.entity = entity;
        description.parts.push_back(std::move(part));
    }

    [[nodiscard]] const std::vector<TileRectangle>& rectanglesOf(Entity entity, const scene::Tilemap& tilemap)
    {
        const std::shared_ptr<const asset::TilesetData> tileset =
            config.tilesets && tilemap.tileset.isValid() ? config.tilesets(tilemap.tileset) : nullptr;
        Signature key;
        for (const std::string& block : tilemap.blocks)
        {
            key.add(core::hash64(block));
        }
        key.add(reinterpret_cast<std::uintptr_t>(tileset.get()));
        TileCache& cache = tileCaches[keyOf(entity)];
        if (cache.key != key.value() || cache.key == 0)
        {
            cache.key = key.value();
            cache.rectangles = tileset == nullptr ? std::vector<TileRectangle>{}
                                                  : tileRectangles(scene::TileGrid::read(tilemap), [&](std::uint32_t id) {
                                                        const asset::TileData* const tile = tileset->find(id);
                                                        return tile != nullptr ? tile->collision : asset::TileCollision::None;
                                                    });
        }
        return cache.rectangles;
    }

    [[nodiscard]] std::unordered_map<std::uint64_t, BodyDescription> describeBodies(scene::Scene& scene)
    {
        std::unordered_map<std::uint64_t, BodyDescription> descriptions;
        const auto box = [](math::Vec2 low, math::Vec2 high) {
            return std::vector<math::Vec2>{low, {high.x, low.y}, high, {low.x, high.y}};
        };
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::BoxCollider2D>())
        {
            const math::Vec2 half = math::abs(collider.size) * 0.5f;
            describeCollider(scene, descriptions, entity,
                             {.kind = ShapeKind::Polygon,
                              .points = box(collider.center - half, collider.center + half),
                              .trigger = collider.trigger,
                              .oneWay = collider.oneWay});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::CircleCollider2D>())
        {
            describeCollider(scene, descriptions, entity,
                             {.kind = ShapeKind::Circle, .points = {collider.center}, .radius = collider.radius,
                              .trigger = collider.trigger});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::CapsuleCollider2D>())
        {
            const float radius = std::max(collider.radius, 0.001f);
            const float reach = std::max(collider.height * 0.5f - radius, 0.0005f);
            describeCollider(scene, descriptions, entity,
                             {.kind = ShapeKind::Capsule,
                              .points = {collider.center - math::Vec2{0.0f, reach}, collider.center + math::Vec2{0.0f, reach}},
                              .radius = radius,
                              .trigger = collider.trigger});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::PolygonCollider2D>())
        {
            if (collider.points.size() < 3)
            {
                warnOnce(entity, "few points", std::format("the polygon collider of '{}' needs three points", scene.name(entity)));
                continue;
            }
            describeCollider(scene, descriptions, entity,
                             {.kind = ShapeKind::Polygon, .points = collider.points, .trigger = collider.trigger,
                              .oneWay = collider.oneWay});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::TilemapCollider2D>())
        {
            const scene::Tilemap* const tilemap = scene.tryGet<scene::Tilemap>(entity);
            if (tilemap == nullptr)
            {
                warnOnce(entity, "no tilemap", std::format("the tilemap collider of '{}' has no Tilemap", scene.name(entity)));
                continue;
            }
            const math::Vec2 cell = tilemap->cellSize;
            for (const TileRectangle& rectangle : rectanglesOf(entity, *tilemap))
            {
                const math::Vec2 low = math::Vec2(rectangle.cell) * cell;
                math::Vec2 high = math::Vec2(rectangle.cell + rectangle.size) * cell;
                describeCollider(scene, descriptions, entity,
                                 {.kind = ShapeKind::Polygon,
                                  .points = box(rectangle.oneWay ? math::Vec2{low.x, high.y - ledgeThickness * cell.y} : low, high),
                                  .trigger = collider.trigger,
                                  .oneWay = rectangle.oneWay && !collider.trigger});
            }
        }
        for ([[maybe_unused]] auto [entity, rigidBody] : scene.view<scene::RigidBody2D>())
        {
            if (!descriptions.contains(keyOf(entity)))
            {
                warnOnce(entity, "no collider", std::format("the RigidBody2D of '{}' has no 2D collider", scene.name(entity)));
            }
        }
        for (auto& [key, description] : descriptions)
        {
            // Pools change order as components come and go: the colliders of a body keep theirs.
            std::ranges::stable_sort(description.parts, {}, [](const ShapePart& part) { return keyOf(part.entity); });
            description.signature = signatureOf(description);
        }
        return descriptions;
    }

    // ---- Bodies ----

    void createBody(scene::Scene& scene, std::uint64_t key, const BodyDescription& description)
    {
        const scene::RigidBody2D& rigidBody = description.rigidBody;
        const BodyType type = description.hasRigidBody ? rigidBody.type : BodyType::Static;
        b2BodyDef definition = b2DefaultBodyDef();
        definition.type = type == BodyType::Dynamic ? b2_dynamicBody : type == BodyType::Kinematic ? b2_kinematicBody : b2_staticBody;
        definition.position = toBox(description.position);
        definition.rotation = b2MakeRot(description.angle);
        definition.linearVelocity = toBox(rigidBody.linearVelocity);
        definition.angularVelocity = rigidBody.angularVelocity;
        definition.linearDamping = rigidBody.linearDamping;
        definition.angularDamping = rigidBody.angularDamping;
        definition.gravityScale = rigidBody.gravityScale;
        definition.fixedRotation = rigidBody.fixedRotation;
        definition.isBullet = rigidBody.continuousCollision;
        definition.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(keyOf(description.owner)));
        const b2BodyId body = b2CreateBody(world, &definition);

        // The mass of the body spreads over the area of its solid shapes.
        float area = 0.0f;
        for (const ShapePart& part : description.parts)
        {
            area += part.trigger ? 0.0f : areaOf(part);
        }
        const float density = type == BodyType::Dynamic && area > 0.0f ? std::max(rigidBody.mass, 0.001f) / area : 1.0f;
        for (const ShapePart& part : description.parts)
        {
            b2ShapeDef shapeDefinition = b2DefaultShapeDef();
            shapeDefinition.density = part.trigger ? 0.0f : density;
            shapeDefinition.material.friction = rigidBody.friction;
            shapeDefinition.material.restitution = rigidBody.restitution;
            shapeDefinition.filter = filterOf(clampLayer(rigidBody.layer), part.trigger, part.oneWay);
            shapeDefinition.isSensor = part.trigger;
            shapeDefinition.enableSensorEvents = true;
            shapeDefinition.enableContactEvents = true;
            // Only the shapes of dynamic bodies ask whether a one-way ledge holds them.
            shapeDefinition.enablePreSolveEvents = type == BodyType::Dynamic;
            shapeDefinition.userData = userDataOf(clampLayer(rigidBody.layer), part.trigger, part.oneWay);
            b2ShapeId shape = b2_nullShapeId;
            switch (part.kind)
            {
            case ShapeKind::Polygon: {
                std::vector<b2Vec2> points;
                for (const math::Vec2 point : part.points)
                {
                    points.push_back(toBox(point));
                }
                const b2Hull hull = b2ComputeHull(points.data(), static_cast<int>(std::min<std::size_t>(points.size(), B2_MAX_POLYGON_VERTICES)));
                if (hull.count == 0)
                {
                    warnOnce(part.entity, "degenerate",
                             std::format("a 2D collider of '{}' is too small or flat to collide", scene.name(part.entity)));
                    continue;
                }
                const b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
                shape = b2CreatePolygonShape(body, &shapeDefinition, &polygon);
                break;
            }
            case ShapeKind::Circle: {
                const b2Circle circle{toBox(part.points.front()), std::max(part.radius, 0.001f)};
                shape = b2CreateCircleShape(body, &shapeDefinition, &circle);
                break;
            }
            case ShapeKind::Capsule: {
                const b2Capsule capsule{toBox(part.points[0]), toBox(part.points[1]), std::max(part.radius, 0.001f)};
                shape = b2CreateCapsuleShape(body, &shapeDefinition, &capsule);
                break;
            }
            }
            shapeOwners[b2StoreShapeId(shape)] = description.owner;
        }
        bodies[key] = BodyRecord{
            .owner = description.owner,
            .type = type,
            .body = body,
            .signature = description.signature,
            .position = description.position,
            .angle = description.angle,
            .linearVelocity = rigidBody.linearVelocity,
            .angularVelocity = rigidBody.angularVelocity,
            .previousPosition = description.position,
            .previousAngle = description.angle,
            .seen = true,
        };
    }

    void removeBody(std::unordered_map<std::uint64_t, BodyRecord>::iterator record)
    {
        destroyBody(record->second.body);
        bodies.erase(record);
    }

    void syncBodies(scene::Scene& scene, float seconds)
    {
        for (auto& [key, record] : bodies)
        {
            record.seen = false;
        }
        for (const auto& [key, description] : describeBodies(scene))
        {
            auto record = bodies.find(key);
            if (record != bodies.end() && record->second.signature != description.signature)
            {
                removeBody(record);
                record = bodies.end();
            }
            if (record == bodies.end())
            {
                createBody(scene, key, description);
                continue;
            }
            BodyRecord& body = record->second;
            body.seen = true;
            const bool moved = math::length(description.position - body.position) > positionTolerance ||
                               angleDifference(description.angle, body.angle) > angleTolerance;
            if (body.type == BodyType::Kinematic)
            {
                b2Body_SetTargetTransform(body.body, b2Transform{toBox(description.position), b2MakeRot(description.angle)}, seconds);
                body.previousPosition = body.position;
                body.previousAngle = body.angle;
                body.position = description.position;
                body.angle = description.angle;
            }
            else if (moved)
            {
                // Game code moved the entity: the body jumps there.
                b2Body_SetTransform(body.body, toBox(description.position), b2MakeRot(description.angle));
                b2Body_SetAwake(body.body, true);
                body.position = body.previousPosition = description.position;
                body.angle = body.previousAngle = description.angle;
            }
            if (body.type == BodyType::Dynamic)
            {
                const scene::RigidBody2D& rigidBody = description.rigidBody;
                if (math::length(rigidBody.linearVelocity - body.linearVelocity) > positionTolerance ||
                    std::abs(rigidBody.angularVelocity - body.angularVelocity) > positionTolerance)
                {
                    b2Body_SetLinearVelocity(body.body, toBox(rigidBody.linearVelocity));
                    b2Body_SetAngularVelocity(body.body, rigidBody.angularVelocity);
                    body.linearVelocity = rigidBody.linearVelocity;
                    body.angularVelocity = rigidBody.angularVelocity;
                }
            }
        }
        for (auto record = bodies.begin(); record != bodies.end();)
        {
            if (!record->second.seen)
            {
                const auto next = std::next(record);
                removeBody(record);
                record = next;
            }
            else
            {
                ++record;
            }
        }
    }

    void writeBodies(scene::Scene& scene)
    {
        for (auto& [key, record] : bodies)
        {
            if (record.type != BodyType::Dynamic)
            {
                continue;
            }
            record.previousPosition = record.position;
            record.previousAngle = record.angle;
            record.position = fromBox(b2Body_GetPosition(record.body));
            record.angle = b2Rot_GetAngle(b2Body_GetRotation(record.body));
            setWorldPose(scene, record.owner, record.position, record.angle);
            if (const scene::WorldTransform* const transform = scene.tryGet<scene::WorldTransform>(record.owner))
            {
                updateDescendants(scene, record.owner, transform->matrix);
            }
            record.linearVelocity = fromBox(b2Body_GetLinearVelocity(record.body));
            record.angularVelocity = b2Body_GetAngularVelocity(record.body);
            if (scene::RigidBody2D* const rigidBody = scene.tryGet<scene::RigidBody2D>(record.owner))
            {
                rigidBody->linearVelocity = record.linearVelocity;
                rigidBody->angularVelocity = record.angularVelocity;
            }
        }
    }

    // ---- Characters ----

    [[nodiscard]] static b2Capsule capsuleAt(const scene::CharacterController2D& controller, math::Vec2 feet) noexcept
    {
        const float radius = std::max(controller.radius, 0.01f);
        const float height = std::max(controller.height, radius * 2.0f + 0.01f);
        return b2Capsule{toBox(feet + math::Vec2{0.0f, radius}), toBox(feet + math::Vec2{0.0f, height - radius}), radius};
    }

    void removeCharacter(std::unordered_map<std::uint64_t, CharacterRecord>::iterator record)
    {
        destroyBody(record->second.body);
        characters.erase(record);
    }

    void syncCharacters(scene::Scene& scene)
    {
        for (auto& [key, record] : characters)
        {
            record.seen = false;
        }
        for ([[maybe_unused]] auto [entity, controller] : scene.view<scene::CharacterController2D>())
        {
            const std::uint64_t key = keyOf(entity);
            const std::uint32_t layer = clampLayer(controller.layer);
            Signature signature;
            signature.add(controller.radius);
            signature.add(controller.height);
            signature.add(layer);
            const math::Vec2 position = math::Vec2(worldMatrixOf(scene, entity)[3]);
            auto record = characters.find(key);
            if (record != characters.end() && record->second.signature != signature.value())
            {
                removeCharacter(record);
                record = characters.end();
            }
            if (record == characters.end())
            {
                b2BodyDef definition = b2DefaultBodyDef();
                definition.type = b2_kinematicBody;
                definition.position = toBox(position);
                definition.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(key));
                const b2BodyId body = b2CreateBody(world, &definition);
                b2ShapeDef shapeDefinition = b2DefaultShapeDef();
                // Characters meet the solid shapes and characters of the layers they collide with,
                // and triggers; not ledges, which the mover handles.
                shapeDefinition.filter = b2Filter{.categoryBits = 1ull << layer, .maskBits = collisionsOf(layer) | triggerBit};
                shapeDefinition.enableSensorEvents = true;
                shapeDefinition.enableContactEvents = true;
                shapeDefinition.userData = userDataOf(layer, false, false);
                const b2Capsule capsule = capsuleAt(controller, math::Vec2{0.0f});
                const b2ShapeId shape = b2CreateCapsuleShape(body, &shapeDefinition, &capsule);
                shapeOwners[b2StoreShapeId(shape)] = entity;
                characters[key] = CharacterRecord{.owner = entity,
                                                  .body = body,
                                                  .signature = signature.value(),
                                                  .position = position,
                                                  .previousPosition = position,
                                                  .seen = true};
                continue;
            }
            record->second.seen = true;
            if (math::length(position - record->second.position) > positionTolerance)
            {
                // Game code moved the character: it jumps there.
                b2Body_SetTransform(record->second.body, toBox(position), b2Rot_identity);
                record->second.position = record->second.previousPosition = position;
            }
        }
        for (auto record = characters.begin(); record != characters.end();)
        {
            const auto next = std::next(record);
            if (!record->second.seen)
            {
                removeCharacter(record);
            }
            record = next;
        }
    }

    // The solid shapes and the accepted ledges around the capsule, as planes pushing it out.
    void gatherPlanes(const b2Capsule& capsule, const MoverFilter& filter, math::Vec2 motion, std::vector<b2CollisionPlane>& planes,
                      std::vector<std::pair<b2ShapeId, math::Vec2>>* pushed) const
    {
        planes.clear();
        PlaneGathering gathering{.filter = &filter, .motion = motion, .planes = &planes, .pushed = pushed};
        b2World_CollideMover(world, &capsule, filter.solids, &gatherPlane, &gathering);
        b2World_CollideMover(world, &capsule, filter.ledges, &gatherPlane, &gathering);
    }

    // How far along the translation the capsule goes before it touches something it accepts.
    [[nodiscard]] float sweep(const b2Capsule& capsule, math::Vec2 translation, const MoverFilter& filter) const
    {
        float fraction = b2World_CastMover(world, &capsule, toBox(translation), filter.solids);
        // Ledges only stop a fall onto them.
        if (translation.y < 0.0f)
        {
            const std::array<b2Vec2, 2> centers{capsule.center1, capsule.center2};
            const b2ShapeProxy proxy = b2MakeProxy(centers.data(), 2, capsule.radius);
            SweepResult result{.filter = &filter, .motion = translation};
            b2World_CastShape(world, &proxy, toBox(translation), filter.ledges, &sweepHit, &result);
            fraction = std::min(fraction, result.fraction);
        }
        return fraction;
    }

    // Moves the feet by a translation, sliding along what the capsule touches, and leaves the planes
    // it touched last.
    [[nodiscard]] math::Vec2 slide(const scene::CharacterController2D& controller, math::Vec2 feet, math::Vec2 translation,
                                   const MoverFilter& filter, bool walking, std::vector<b2CollisionPlane>& planes,
                                   std::vector<std::pair<b2ShapeId, math::Vec2>>* pushed) const
    {
        const math::Vec2 target = feet + translation;
        for (int iteration = 0; iteration < moverIterations; ++iteration)
        {
            const b2Capsule capsule = capsuleAt(controller, feet);
            gatherPlanes(capsule, filter, target - feet, planes, iteration == 0 ? pushed : nullptr);
            if (walking)
            {
                // Walking, what is too steep to walk on, slopes or the corners of steps, is a wall
                // that the character does not slide up.
                for (b2CollisionPlane& plane : planes)
                {
                    if (plane.plane.normal.y > 0.0f && plane.plane.normal.y < filter.walkable)
                    {
                        // As deep horizontally as along its normal, beyond the slop the solver allows.
                        const float across = std::abs(plane.plane.normal.x);
                        plane.plane = b2Plane{b2Vec2{plane.plane.normal.x < 0.0f ? -1.0f : 1.0f, 0.0f},
                                              (plane.plane.offset - linearSlop) / across + linearSlop};
                    }
                }
            }
            const b2PlaneSolverResult solved = b2SolvePlanes(toBox(target - feet), planes.data(), static_cast<int>(planes.size()));
            const math::Vec2 step = fromBox(solved.translation);
            const math::Vec2 delta = step * sweep(capsule, step, filter);
            feet += delta;
            if (math::dot(delta, delta) < 1e-8f)
            {
                break;
            }
        }
        return feet;
    }

    struct Ground
    {
        math::Vec2 normal{0.0f, 1.0f};
        b2ShapeId shape = b2_nullShapeId;
        // Where the character touches it.
        math::Vec2 point{0.0f};
        // How far below the feet it is.
        float gap = 0.0f;
    };

    // The normal of a walkable top beside a corner, on the side of the corner that the corner does
    // not push the character to.
    [[nodiscard]] static std::optional<math::Vec2> topBeside(b2WorldId world, const MoverFilter& filter, math::Vec2 corner, float side)
    {
        struct Ray
        {
            const MoverFilter* filter = nullptr;
            std::optional<math::Vec2> normal;
            float fraction = 2.0f;
        } ray{.filter = &filter};
        const auto hit = [](b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context) {
            auto& state = *static_cast<Ray*>(context);
            if (!state.filter->accepts(shape, fromBox(normal), fromBox(point), math::Vec2{0.0f, -1.0f}))
            {
                return -1.0f;
            }
            if (fraction < state.fraction)
            {
                state.fraction = fraction;
                state.normal = fromBox(normal);
            }
            return fraction;
        };
        const math::Vec2 origin{corner.x + side * cornerReach, corner.y + groundProbe};
        b2World_CastRay(world, toBox(origin), b2Vec2{0.0f, -2.0f * groundProbe}, filter.solids, hit, &ray);
        return ray.normal && ray.normal->y >= filter.walkable ? ray.normal : std::nullopt;
    }

    // What walkable the character stands on, if anything: ground, or the edge of ground.
    [[nodiscard]] std::optional<Ground> groundUnder(const scene::CharacterController2D& controller, math::Vec2 feet,
                                                    const MoverFilter& filter) const
    {
        struct Search
        {
            const MoverFilter* filter = nullptr;
            b2WorldId world = b2_nullWorldId;
            std::optional<Ground> found;
        } search{.filter = &filter, .world = world};
        const b2Capsule capsule = capsuleAt(controller, feet - math::Vec2{0.0f, groundProbe});
        const auto gather = [](b2ShapeId shape, const b2PlaneResult* result, void* context) {
            auto& state = *static_cast<Search*>(context);
            math::Vec2 normal = fromBox(result->plane.normal);
            const math::Vec2 point = contactPoint(shape, *result);
            if (!result->hit || normal.y <= 0.0f)
            {
                return true;
            }
            // The offset of the plane is how deep the lowered capsule goes into the ground.
            const float gap = std::max(groundProbe - result->plane.offset / normal.y, 0.0f);
            if (normal.y < state.filter->walkable)
            {
                // The rounded bottom of the capsule on the edge of a walkable top, which a short ray
                // finds beside the corner.
                const std::optional<math::Vec2> top =
                    topBeside(state.world, *state.filter, point, normal.x > 0.0f ? -1.0f : 1.0f);
                if (!top)
                {
                    return true;
                }
                normal = *top;
            }
            if (!state.filter->accepts(shape, normal, point, math::Vec2{0.0f, -1.0f}))
            {
                return true;
            }
            if (!state.found || gap < state.found->gap - 1e-4f || (gap < state.found->gap + 1e-4f && normal.y > state.found->normal.y))
            {
                state.found = Ground{normal, shape, point, gap};
            }
            return true;
        };
        b2World_CollideMover(world, &capsule, filter.solids, gather, &search);
        b2World_CollideMover(world, &capsule, filter.ledges, gather, &search);
        return search.found;
    }

    void moveCharacters(scene::Scene& scene, float seconds)
    {
        std::vector<b2CollisionPlane> planes;
        std::vector<std::pair<b2ShapeId, math::Vec2>> pushed;
        std::vector<b2BodyId> pushedBodies;
        const math::Vec2 worldGravity = fromBox(b2World_GetGravity(world));
        for (auto& [key, record] : characters)
        {
            scene::CharacterController2D* const controller = scene.tryGet<scene::CharacterController2D>(record.owner);
            if (controller == nullptr)
            {
                continue;
            }
            const std::uint32_t layer = clampLayer(controller->layer);
            const MoverFilter filter{.self = record.body,
                                     .solids = moverFilterOf(layer, false),
                                     .ledges = moverFilterOf(layer, true),
                                     .feet = record.position.y,
                                     .walkable = std::cos(std::clamp(controller->maxSlope, 0.0f, 1.5f))};

            math::Vec2 velocity = controller->velocity;
            const bool standing = controller->grounded && velocity.y <= 0.0f;
            if (standing)
            {
                // Standing: gravity does not build up, and the ground carries the character.
                velocity.y = 0.0f;
            }
            else
            {
                velocity += worldGravity * controller->gravityScale * seconds;
                record.groundVelocity = math::Vec2{0.0f};
            }
            const math::Vec2 motion = (velocity + record.groundVelocity) * seconds;
            pushed.clear();
            math::Vec2 feet = slide(*controller, record.position, motion, filter, standing, planes, &pushed);
            bool climbed = false;

            // Blocked by a step no higher than the step height: over it.
            const float wanted = std::abs(motion.x);
            if (standing && controller->stepHeight > 0.0f && wanted > 1e-4f && std::abs(feet.x - record.position.x) < wanted - 1e-4f)
            {
                const math::Vec2 up{0.0f, controller->stepHeight};
                const math::Vec2 raised = record.position + up * sweep(capsuleAt(*controller, record.position), up, filter);
                const math::Vec2 across{motion.x, 0.0f};
                const math::Vec2 moved = raised + across * sweep(capsuleAt(*controller, raised), across, filter);
                const math::Vec2 down{0.0f, -(controller->stepHeight + groundProbe)};
                const math::Vec2 landed = moved + down * sweep(capsuleAt(*controller, moved), down, filter);
                // Further, onto higher ground: the top of the step, or its corner, over which it goes on.
                const std::optional<Ground> top = groundUnder(*controller, landed, filter);
                if (std::abs(landed.x - record.position.x) > std::abs(feet.x - record.position.x) + 1e-3f && top &&
                    top->point.y > record.position.y + 1e-3f)
                {
                    feet = landed;
                    climbed = true;
                }
            }
            // Walking down a slope or off a small step: kept on the ground.
            if (standing && !groundUnder(*controller, feet, filter))
            {
                const math::Vec2 down{0.0f, -controller->stepHeight};
                const math::Vec2 lowered = feet + down * sweep(capsuleAt(*controller, feet), down, filter);
                if (groundUnder(*controller, lowered, filter))
                {
                    feet = lowered;
                }
            }

            // Dynamic bodies in the way are pushed to the speed of the character, with at most its
            // push force.
            pushedBodies.clear();
            for (const auto& [shape, normal] : pushed)
            {
                const b2BodyId body = b2Shape_GetBody(shape);
                const math::Vec2 into = -normal;
                if (std::abs(normal.y) >= 0.7f ||
                    std::ranges::any_of(pushedBodies, [&](b2BodyId other) { return B2_ID_EQUALS(other, body); }))
                {
                    continue;
                }
                pushedBodies.push_back(body);
                const float missing = math::dot(velocity, into) - math::dot(fromBox(b2Body_GetLinearVelocity(body)), into);
                if (missing > 0.0f)
                {
                    const float impulse = std::min(b2Body_GetMass(body) * missing, std::max(controller->pushForce, 0.0f) * seconds);
                    b2Body_ApplyLinearImpulseToCenter(body, toBox(into * impulse), true);
                }
            }

            // What stops the character stops its velocity. Walking, only walls do: the ground it
            // walks along does not send it up slopes.
            if (climbed)
            {
                planes.clear();
            }
            else if (standing)
            {
                std::erase_if(planes, [&](const b2CollisionPlane& plane) { return plane.plane.normal.y >= filter.walkable; });
            }
            velocity = fromBox(b2ClipVector(toBox(velocity), planes.data(), static_cast<int>(planes.size())));
            if (standing)
            {
                velocity.y = std::min(velocity.y, 0.0f);
            }
            const std::optional<Ground> ground = groundUnder(*controller, feet, filter);
            if (ground && velocity.y <= 1e-3f)
            {
                // Down onto the ground the probe found below: a move may end just above it.
                feet.y -= ground->gap;
            }
            controller->grounded = ground.has_value() && velocity.y <= 1e-3f;
            controller->groundNormal = controller->grounded ? ground->normal : math::Vec2{0.0f, 1.0f};
            record.groundVelocity = math::Vec2{0.0f};
            if (controller->grounded)
            {
                velocity.y = std::max(velocity.y, 0.0f);
                const b2BodyId groundBody = b2Shape_GetBody(ground->shape);
                if (b2Body_GetType(groundBody) != b2_staticBody)
                {
                    record.groundVelocity = fromBox(b2Body_GetWorldPointVelocity(groundBody, toBox(feet)));
                }
            }
            controller->velocity = velocity;
            record.previousPosition = record.position;
            record.position = feet;
            setWorldPose(scene, record.owner, feet, std::nullopt);
            if (const scene::WorldTransform* const worldTransform = scene.tryGet<scene::WorldTransform>(record.owner))
            {
                updateDescendants(scene, record.owner, worldTransform->matrix);
            }
            // The body of the character follows it, for what it touches during the step.
            b2Body_SetTargetTransform(record.body, b2Transform{toBox(feet), b2Rot_identity}, seconds);
        }
    }

    // ---- Contacts ----

    [[nodiscard]] static std::uint64_t pairOf(const Contact& contact) noexcept
    {
        const std::uint64_t second = keyOf(contact.second);
        return core::hash64(std::as_bytes(std::span(&second, 1)), keyOf(contact.first)) ^ (contact.trigger ? 1u : 0u);
    }

    // Two shapes started touching: the first pair between their entities begins a contact.
    // The stored ids of two shapes, in order. (std::minmax returns references, which must not
    // outlive the values they compare.)
    [[nodiscard]] static std::pair<std::uint64_t, std::uint64_t> shapePair(b2ShapeId shapeA, b2ShapeId shapeB) noexcept
    {
        const std::uint64_t first = b2StoreShapeId(shapeA);
        const std::uint64_t second = b2StoreShapeId(shapeB);
        return {std::min(first, second), std::max(first, second)};
    }

    void hold(b2ShapeId shapeA, b2ShapeId shapeB, bool trigger)
    {
        const std::pair<std::uint64_t, std::uint64_t> shapes = shapePair(shapeA, shapeB);
        const auto ownerA = shapeOwners.find(shapes.first);
        const auto ownerB = shapeOwners.find(shapes.second);
        if (ownerA == shapeOwners.end() || ownerB == shapeOwners.end() || ownerA->second == ownerB->second)
        {
            return;
        }
        const std::uint64_t keyA = keyOf(ownerA->second);
        const std::uint64_t keyB = keyOf(ownerB->second);
        const Contact contact{ContactPhase::Begin, entityOf(std::min(keyA, keyB)), entityOf(std::max(keyA, keyB)), trigger};
        if (touches.emplace(shapes, contact).second && ++contactCounts[pairOf(contact)] == 1)
        {
            contacts.push_back(contact);
        }
    }

    // A pair stopped touching: the last pair between two entities ends their contact.
    void release(const Contact& contact)
    {
        const auto count = contactCounts.find(pairOf(contact));
        if (count != contactCounts.end() && --count->second == 0)
        {
            contactCounts.erase(count);
            contacts.push_back({ContactPhase::End, contact.first, contact.second, contact.trigger});
        }
    }

    void release(b2ShapeId shapeA, b2ShapeId shapeB)
    {
        const auto touch = touches.find(shapePair(shapeA, shapeB));
        if (touch != touches.end())
        {
            release(touch->second);
            touches.erase(touch);
        }
    }

    void gatherContacts()
    {
        const b2ContactEvents events = b2World_GetContactEvents(world);
        for (int index = 0; index < events.beginCount; ++index)
        {
            hold(events.beginEvents[index].shapeIdA, events.beginEvents[index].shapeIdB, false);
        }
        for (int index = 0; index < events.endCount; ++index)
        {
            release(events.endEvents[index].shapeIdA, events.endEvents[index].shapeIdB);
        }
        const b2SensorEvents sensors = b2World_GetSensorEvents(world);
        for (int index = 0; index < sensors.beginCount; ++index)
        {
            hold(sensors.beginEvents[index].sensorShapeId, sensors.beginEvents[index].visitorShapeId, true);
        }
        for (int index = 0; index < sensors.endCount; ++index)
        {
            release(sensors.endEvents[index].sensorShapeId, sensors.endEvents[index].visitorShapeId);
        }
    }

    [[nodiscard]] const BodyRecord* dynamicBody(Entity entity) const
    {
        const auto record = bodies.find(keyOf(entity));
        return record != bodies.end() && record->second.type == BodyType::Dynamic ? &record->second : nullptr;
    }
};

core::Result<std::unique_ptr<Physics2DWorld>> Physics2DWorld::create(Physics2DWorldConfig config)
{
    config.subSteps = std::clamp(config.subSteps, 1, 16);
    return std::unique_ptr<Physics2DWorld>(new Physics2DWorld(std::make_unique<Implementation>(std::move(config))));
}

Physics2DWorld::Physics2DWorld(std::unique_ptr<Implementation> implementation) noexcept
    : m_implementation(std::move(implementation))
{
}

Physics2DWorld::~Physics2DWorld() = default;

void Physics2DWorld::step(scene::Scene& scene, core::Duration delta)
{
    Implementation& world = *m_implementation;
    if (world.simulatedScene != &scene)
    {
        world.clear();
        world.simulatedScene = &scene;
    }
    const auto seconds = static_cast<float>(delta.count());
    if (seconds <= 0.0f)
    {
        return;
    }
    world.syncBodies(scene, seconds);
    world.syncCharacters(scene);
    // Characters move against the bodies where the last step left them.
    world.moveCharacters(scene, seconds);
    b2World_Step(world.world, seconds, world.config.subSteps);
    world.writeBodies(scene);
    world.gatherContacts();
}

void Physics2DWorld::interpolate(scene::Scene& scene, float alpha)
{
    Implementation& world = *m_implementation;
    if (world.simulatedScene != &scene)
    {
        return;
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const auto place = [&](Entity entity, math::Vec2 position, std::optional<float> angle) {
        scene::WorldTransform* const transform = scene.tryGet<scene::WorldTransform>(entity);
        if (transform == nullptr)
        {
            return;
        }
        const math::Trs current = math::decomposeTrs(transform->matrix);
        math::Quat rotation = current.rotation;
        if (angle)
        {
            rotation = math::normalize(math::angleAxis(*angle - angleOf(transform->matrix), math::Vec3{0.0f, 0.0f, 1.0f}) * rotation);
        }
        transform->matrix = math::composeTrs({math::Vec3{position, current.translation.z}, rotation, current.scale});
        updateDescendants(scene, entity, transform->matrix);
    };
    for (const auto& [key, record] : world.bodies)
    {
        if (record.type == BodyType::Static)
        {
            continue;
        }
        const float turn = std::remainder(record.angle - record.previousAngle, 2.0f * 3.14159265358979f);
        place(record.owner, math::mix(record.previousPosition, record.position, alpha), record.previousAngle + turn * alpha);
    }
    for (const auto& [key, record] : world.characters)
    {
        place(record.owner, math::mix(record.previousPosition, record.position, alpha), std::nullopt);
    }
}

std::span<const Contact> Physics2DWorld::contacts() const noexcept
{
    return m_implementation->contacts;
}

void Physics2DWorld::clearContacts() noexcept
{
    m_implementation->contacts.clear();
}

namespace {

struct RayQuery
{
    std::uint16_t layers = allLayers;
    Entity ignore;
    std::optional<RayHit> hit;
    float length = 1.0f;
};

float rayHit(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context)
{
    auto& query = *static_cast<RayQuery*>(context);
    if ((flagsOf(shape) & triggerFlag) != 0 || !inLayers(shape, query.layers) ||
        (query.ignore.isValid() && ownerOf(shape) == query.ignore))
    {
        return -1.0f;
    }
    query.hit = RayHit{.entity = ownerOf(shape), .point = fromBox(point), .normal = fromBox(normal), .distance = fraction * query.length};
    return fraction;
}

struct OverlapQuery
{
    std::uint16_t layers = allLayers;
    Entity ignore;
    std::vector<Entity> found;
};

bool overlapHit(b2ShapeId shape, void* context)
{
    auto& query = *static_cast<OverlapQuery*>(context);
    const Entity owner = ownerOf(shape);
    if (inLayers(shape, query.layers) && (!query.ignore.isValid() || owner != query.ignore) &&
        std::ranges::find(query.found, owner) == query.found.end())
    {
        query.found.push_back(owner);
    }
    return true;
}

} // namespace

std::optional<RayHit> Physics2DWorld::raycast(math::Vec2 origin, math::Vec2 direction, float maxDistance, std::uint16_t layers,
                                              scene::Entity ignore) const
{
    const float length = math::length(direction);
    if (length <= 0.0f || maxDistance <= 0.0f)
    {
        return std::nullopt;
    }
    RayQuery query{.layers = layers, .ignore = ignore, .length = maxDistance};
    b2World_CastRay(m_implementation->world, toBox(origin), toBox(direction / length * maxDistance), Implementation::everything,
                    &rayHit, &query);
    return query.hit;
}

std::vector<scene::Entity> Physics2DWorld::overlapCircle(math::Vec2 center, float radius, std::uint16_t layers,
                                                         scene::Entity ignore) const
{
    if (radius <= 0.0f)
    {
        return {};
    }
    const b2Vec2 point = toBox(center);
    const b2ShapeProxy proxy = b2MakeProxy(&point, 1, radius);
    OverlapQuery query{.layers = layers, .ignore = ignore};
    b2World_OverlapShape(m_implementation->world, &proxy, Implementation::everything, &overlapHit, &query);
    return query.found;
}

void Physics2DWorld::addForce(scene::Entity entity, math::Vec2 force)
{
    if (const BodyRecord* const record = m_implementation->dynamicBody(entity))
    {
        b2Body_ApplyForceToCenter(record->body, toBox(force), true);
    }
}

void Physics2DWorld::addTorque(scene::Entity entity, float torque)
{
    if (const BodyRecord* const record = m_implementation->dynamicBody(entity))
    {
        b2Body_ApplyTorque(record->body, torque, true);
    }
}

void Physics2DWorld::addImpulse(scene::Entity entity, math::Vec2 impulse)
{
    if (const BodyRecord* const record = m_implementation->dynamicBody(entity))
    {
        b2Body_ApplyLinearImpulseToCenter(record->body, toBox(impulse), true);
    }
}

std::size_t Physics2DWorld::bodyCount() const noexcept
{
    return m_implementation->bodies.size() + m_implementation->characters.size();
}

math::Vec2 Physics2DWorld::gravity() const noexcept
{
    return fromBox(b2World_GetGravity(m_implementation->world));
}

} // namespace devex::physics2d
