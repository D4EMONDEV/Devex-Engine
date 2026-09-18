// Jolt must be included first: its headers configure the platform for the ones that follow.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <devex/core/Hash.hpp>
#include <devex/core/Log.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <mutex>
#include <source_location>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace devex::physics {
namespace {

using scene::BodyType;
using scene::Entity;

// Limits of the simulation, sized for scenes of tens of thousands of bodies.
constexpr std::uint32_t maxBodyPairs = 65536;
constexpr std::uint32_t maxContactConstraints = 16384;
constexpr std::size_t temporaryMemory = 32 * 1024 * 1024;
// Distances below which a pose written by the simulation counts as unchanged.
constexpr float positionTolerance = 1.0e-4f;
constexpr float rotationTolerance = 1.0e-6f;

// ---- Conversions -------------------------------------------------------------------------------

[[nodiscard]] JPH::Vec3 toJolt(math::Vec3 value) noexcept
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] JPH::RVec3 toJoltPosition(math::Vec3 value) noexcept
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] JPH::Quat toJolt(math::Quat value) noexcept
{
    return JPH::Quat(value.x, value.y, value.z, value.w).Normalized();
}

[[nodiscard]] math::Vec3 fromJolt(JPH::Vec3Arg value) noexcept
{
    return {value.GetX(), value.GetY(), value.GetZ()};
}

[[nodiscard]] math::Vec3 fromJoltPosition(JPH::RVec3Arg value) noexcept
{
    return {static_cast<float>(value.GetX()), static_cast<float>(value.GetY()), static_cast<float>(value.GetZ())};
}

[[nodiscard]] math::Quat fromJolt(JPH::QuatArg value) noexcept
{
    math::Quat result;
    result.x = value.GetX();
    result.y = value.GetY();
    result.z = value.GetZ();
    result.w = value.GetW();
    return result;
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

[[nodiscard]] std::uint64_t bodyKey(const JPH::BodyID& id) noexcept
{
    return id.GetIndexAndSequenceNumber();
}

// ---- Jolt as a library -------------------------------------------------------------------------

void trace(const char* format, ...)
{
    std::array<char, 1024> buffer{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    DEVEX_LOG_DEBUG("Jolt: {}", buffer.data());
}

#ifdef JPH_ENABLE_ASSERTS
// A failed check of Jolt is reported like the engine's own: written to the error output, which is
// not buffered, then a break for the debugger.
bool assertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
{
    core::logMessage(core::LogLevel::Fatal,
                     std::format("Jolt: assertion failed: {}{}{}{} ({}:{})", expression, message != nullptr ? " (" : "",
                                 message != nullptr ? message : "", message != nullptr ? ")" : "", file, line),
                     std::source_location{});
    return true;
}
#endif

// Jolt's allocator, factory and types are global: they exist while any world does.
class JoltLibrary
{
public:
    JoltLibrary()
    {
        const std::lock_guard lock(mutex());
        if (users()++ == 0)
        {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = &trace;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &assertFailed;)
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
    }

    ~JoltLibrary()
    {
        const std::lock_guard lock(mutex());
        if (--users() == 0)
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    JoltLibrary(const JoltLibrary&) = delete;
    JoltLibrary& operator=(const JoltLibrary&) = delete;

private:
    static std::mutex& mutex()
    {
        static std::mutex instance;
        return instance;
    }

    static int& users()
    {
        static int count = 0;
        return count;
    }
};

// ---- Layers ------------------------------------------------------------------------------------

// An object layer is a collision layer of the project and whether the body moves: static bodies
// never need to be tested against each other.
[[nodiscard]] JPH::ObjectLayer objectLayer(std::uint32_t layer, bool moving) noexcept
{
    return static_cast<JPH::ObjectLayer>(std::min<std::uint32_t>(layer, asset::physicsLayerCount - 1) * 2 + (moving ? 1 : 0));
}

[[nodiscard]] std::uint32_t projectLayer(JPH::ObjectLayer layer) noexcept
{
    return static_cast<std::uint32_t>(layer) >> 1;
}

[[nodiscard]] bool isMovingLayer(JPH::ObjectLayer layer) noexcept
{
    return (layer & 1) != 0;
}

const JPH::BroadPhaseLayer staticBroadPhase(0);
const JPH::BroadPhaseLayer movingBroadPhase(1);

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return 2;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return isMovingLayer(layer) ? movingBroadPhase : staticBroadPhase;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == movingBroadPhase ? "moving" : "static";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        return isMovingLayer(layer) || broadPhase == movingBroadPhase;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter
{
public:
    explicit ObjectPairs(const asset::PhysicsSettings& settings) noexcept
        : m_settings(settings)
    {
    }

    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
    {
        return (isMovingLayer(first) || isMovingLayer(second)) &&
               m_settings.collides(projectLayer(first), projectLayer(second));
    }

private:
    const asset::PhysicsSettings& m_settings;
};

// Accepts the layers of a mask, for queries.
class LayerMaskFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit LayerMaskFilter(std::uint16_t mask) noexcept
        : m_mask(mask)
    {
    }

    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return (m_mask & (1u << projectLayer(layer))) != 0;
    }

private:
    std::uint16_t m_mask;
};

// Ignores the bodies of one entity, for queries.
class IgnoreEntityFilter final : public JPH::BodyFilter
{
public:
    explicit IgnoreEntityFilter(Entity entity) noexcept
        : m_key(entity.isValid() ? keyOf(entity) : std::numeric_limits<std::uint64_t>::max())
    {
    }

    bool ShouldCollideLocked(const JPH::Body& body) const override
    {
        return body.GetUserData() != m_key;
    }

private:
    std::uint64_t m_key;
};

// ---- Contacts ----------------------------------------------------------------------------------

struct RawContact
{
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    bool added = false;
};

// Collects contact changes, which Jolt reports from its worker threads.
class ContactCollector final : public JPH::ContactListener
{
public:
    void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& /*manifold*/,
                        JPH::ContactSettings& /*settings*/) override
    {
        const std::lock_guard lock(m_mutex);
        m_contacts.push_back({bodyKey(first.GetID()), bodyKey(second.GetID()), true});
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        const std::lock_guard lock(m_mutex);
        m_contacts.push_back({bodyKey(pair.GetBody1ID()), bodyKey(pair.GetBody2ID()), false});
    }

    [[nodiscard]] std::vector<RawContact> take()
    {
        const std::lock_guard lock(m_mutex);
        return std::exchange(m_contacts, {});
    }

private:
    std::mutex m_mutex;
    std::vector<RawContact> m_contacts;
};

// ---- Descriptions of bodies --------------------------------------------------------------------

enum class ShapeKind : std::uint8_t
{
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Mesh,
};

// One collider of a body, placed relative to the body's entity without its scale.
struct ShapePart
{
    ShapeKind kind = ShapeKind::Box;
    // Box size, or radius and height.
    math::Vec3 dimensions{1.0f};
    asset::AssetId mesh;
    bool convex = false;
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec3 scale{1.0f};
    // The entity of the collider, for messages.
    Entity entity;
};

struct BodyDescription
{
    Entity owner;
    bool sensor = false;
    // Colliders without a RigidBody form static bodies.
    bool hasRigidBody = false;
    scene::RigidBody rigidBody;
    std::vector<ShapePart> parts;
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
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

    // Placements are computed from world matrices, whose rounding changes as a body turns: they
    // count to a tenth of a millimeter, so that only real changes rebuild the body.
    void addPlacement(math::Vec3 value) noexcept
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            add(static_cast<std::int64_t>(std::lround(value[axis] * placementSteps)));
        }
    }

    void addPlacement(math::Quat rotation) noexcept
    {
        // q and -q are the same rotation.
        if (rotation.w < 0.0f || (rotation.w == 0.0f && (rotation.x < 0.0f || (rotation.x == 0.0f && (rotation.y < 0.0f ||
                                                                                                    (rotation.y == 0.0f && rotation.z < 0.0f))))))
        {
            rotation = -rotation;
        }
        addPlacement(math::Vec3(rotation.x, rotation.y, rotation.z));
        add(static_cast<std::int64_t>(std::lround(rotation.w * placementSteps)));
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return m_value;
    }

private:
    static constexpr float placementSteps = 10000.0f;

    std::uint64_t m_value = 0x9E3779B97F4A7C15ull;
};

[[nodiscard]] std::uint64_t signatureOf(const BodyDescription& description) noexcept
{
    Signature signature;
    const scene::RigidBody& body = description.rigidBody;
    signature.add(description.sensor);
    signature.add(description.hasRigidBody);
    signature.add(body.type);
    signature.add(body.mass);
    signature.add(body.friction);
    signature.add(body.restitution);
    signature.add(body.linearDamping);
    signature.add(body.angularDamping);
    signature.add(body.gravityScale);
    signature.add(body.layer);
    signature.add(body.lockRotationX);
    signature.add(body.lockRotationY);
    signature.add(body.lockRotationZ);
    signature.add(body.continuousCollision);
    for (const ShapePart& part : description.parts)
    {
        signature.add(part.kind);
        signature.add(part.dimensions);
        signature.add(part.mesh.uuid);
        signature.add(part.convex);
        signature.addPlacement(part.position);
        signature.addPlacement(part.rotation);
        signature.addPlacement(part.scale);
    }
    return signature.value();
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

// The entity whose body a collider belongs to.
[[nodiscard]] Entity bodyOwner(const scene::Scene& scene, Entity collider) noexcept
{
    for (Entity current = collider; current.isValid(); current = scene.parent(current))
    {
        if (scene.has<scene::RigidBody>(current))
        {
            return current;
        }
    }
    return collider;
}

// Jolt refuses velocities above the limits of a body, which the velocities it wrote back can reach
// once rounded: they stay just below.
[[nodiscard]] math::Vec3 limitVelocity(math::Vec3 velocity, float limit) noexcept
{
    const float length = math::length(velocity);
    const float allowed = limit * 0.999f;
    return length > allowed ? velocity * (allowed / length) : velocity;
}

[[nodiscard]] bool rotationsDiffer(math::Quat first, math::Quat second) noexcept
{
    return 1.0f - std::abs(math::dot(first, second)) > rotationTolerance;
}

// Places an entity in the world, keeping its scale, by changing its Transform relative to its
// parent and its world transform.
void setWorldPose(scene::Scene& scene, Entity entity, math::Vec3 position, std::optional<math::Quat> rotation)
{
    scene::Transform* const local = scene.tryGet<scene::Transform>(entity);
    if (local == nullptr)
    {
        return;
    }
    scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    const math::Trs current = math::decomposeTrs(world != nullptr ? world->matrix : math::Mat4{1.0f});
    const math::Mat4 target = math::composeTrs({position, rotation.value_or(current.rotation), current.scale});
    const math::Trs relative = math::decomposeTrs(math::inverse(worldMatrixOf(scene, scene.parent(entity))) * target);
    local->position = relative.translation;
    if (rotation)
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

// ---- Records of simulated objects --------------------------------------------------------------

struct BodyRecord
{
    Entity owner;
    bool sensor = false;
    BodyType type = BodyType::Static;
    JPH::BodyID body;
    std::uint64_t signature = 0;
    // The pose and velocities last read from or written to the scene, to notice game code changing them.
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec3 linearVelocity{0.0f};
    math::Vec3 angularVelocity{0.0f};
    // The pose of the previous step, for interpolation.
    math::Vec3 previousPosition{0.0f};
    math::Quat previousRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool seen = false;
};

struct CharacterRecord
{
    Entity owner;
    JPH::Ref<JPH::CharacterVirtual> character;
    std::uint64_t signature = 0;
    math::Vec3 position{0.0f};
    math::Vec3 previousPosition{0.0f};
    bool seen = false;
};

struct BodyOwner
{
    Entity entity;
    bool sensor = false;
};

} // namespace

struct PhysicsWorld::Implementation
{
    explicit Implementation(PhysicsWorldConfig worldConfig)
        : config(std::move(worldConfig))
        , objectPairs(config.settings)
    {
    }

    ~Implementation()
    {
        clear();
    }

    Implementation(const Implementation&) = delete;
    Implementation& operator=(const Implementation&) = delete;

    // Declared first, so that Jolt stays initialized until everything else is destroyed.
    JoltLibrary library;
    PhysicsWorldConfig config;
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    ContactCollector contactCollector;
    std::unique_ptr<JPH::TempAllocatorImpl> temporaryAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    JPH::PhysicsSystem system;
    JPH::CharacterVsCharacterCollisionSimple characterCollisions;

    std::unordered_map<std::uint64_t, BodyRecord> bodies;
    std::unordered_map<std::uint64_t, CharacterRecord> characters;
    // The entity of each body by BodyID, including bodies removed since the last step, whose contacts
    // end during the next one.
    std::unordered_map<std::uint64_t, BodyOwner> owners;
    std::vector<std::uint64_t> retiredBodies;
    std::unordered_map<std::uint64_t, std::uint32_t> contactCounts;
    std::vector<Contact> contacts;
    std::unordered_set<std::uint64_t> warnings;
    // The scene the bodies come from.
    const scene::Scene* simulatedScene = nullptr;

    [[nodiscard]] JPH::BodyInterface& bodyInterface() noexcept
    {
        return system.GetBodyInterface();
    }

    void warnOnce(Entity entity, std::string_view problem, const std::string& message)
    {
        if (warnings.insert(keyOf(entity) ^ core::hash64(problem)).second)
        {
            DEVEX_LOG_WARNING("Physics: {}", message);
        }
    }

    void retire(const JPH::BodyID& id)
    {
        retiredBodies.push_back(bodyKey(id));
    }

    void clear()
    {
        for (auto& [key, record] : characters)
        {
            characterCollisions.Remove(record.character);
        }
        characters.clear();
        JPH::BodyInterface& interface = bodyInterface();
        for (auto& [key, record] : bodies)
        {
            interface.RemoveBody(record.body);
            interface.DestroyBody(record.body);
        }
        bodies.clear();
        owners.clear();
        retiredBodies.clear();
        contactCounts.clear();
        contacts.clear();
        warnings.clear();
    }

    // ---- Shapes ----

    [[nodiscard]] JPH::RefConst<JPH::Shape> buildPartShape(const ShapePart& part, bool dynamic, scene::Scene& scene)
    {
        const math::Vec3 scale = math::abs(part.scale);
        const auto create = [&](const JPH::ShapeSettings& settings) -> JPH::RefConst<JPH::Shape> {
            JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                warnOnce(part.entity, "shape", std::format("the collider of '{}' is invalid: {}", scene.name(part.entity),
                                                           result.GetError().c_str()));
                return nullptr;
            }
            return result.Get();
        };

        switch (part.kind)
        {
        case ShapeKind::Box: {
            const math::Vec3 half = math::max(part.dimensions * scale * 0.5f, math::Vec3{0.001f});
            const float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min({half.x, half.y, half.z}) * 0.5f);
            return create(JPH::BoxShapeSettings(toJolt(half), convexRadius));
        }
        case ShapeKind::Sphere:
            return create(JPH::SphereShapeSettings(std::max(part.dimensions.x * std::max({scale.x, scale.y, scale.z}), 0.001f)));
        case ShapeKind::Capsule: {
            const float radius = std::max(part.dimensions.x * std::max(scale.x, scale.z), 0.001f);
            const float halfCylinder = part.dimensions.y * scale.y * 0.5f - radius;
            if (halfCylinder <= 0.001f)
            {
                return create(JPH::SphereShapeSettings(radius));
            }
            return create(JPH::CapsuleShapeSettings(halfCylinder, radius));
        }
        case ShapeKind::Cylinder: {
            const float radius = std::max(part.dimensions.x * std::max(scale.x, scale.z), 0.001f);
            const float halfHeight = std::max(part.dimensions.y * scale.y * 0.5f, 0.001f);
            return create(JPH::CylinderShapeSettings(halfHeight, radius, std::min(JPH::cDefaultConvexRadius, std::min(halfHeight, radius) * 0.5f)));
        }
        case ShapeKind::Mesh: {
            const asset::MeshData* const mesh = config.meshes ? config.meshes(part.mesh) : nullptr;
            if (mesh == nullptr || mesh->indices.size() < 3)
            {
                warnOnce(part.entity, "mesh", std::format("the mesh of the collider of '{}' cannot be loaded", scene.name(part.entity)));
                return nullptr;
            }
            if (part.convex || dynamic)
            {
                if (!part.convex)
                {
                    warnOnce(part.entity, "convex", std::format("the mesh collider of '{}' is convex: dynamic bodies need convex shapes",
                                                                scene.name(part.entity)));
                }
                JPH::Array<JPH::Vec3> points;
                points.reserve(mesh->vertices.size());
                for (const asset::Vertex& vertex : mesh->vertices)
                {
                    points.push_back(toJolt(vertex.position * part.scale));
                }
                return create(JPH::ConvexHullShapeSettings(points));
            }
            JPH::VertexList vertices;
            vertices.reserve(mesh->vertices.size());
            for (const asset::Vertex& vertex : mesh->vertices)
            {
                const math::Vec3 position = vertex.position * part.scale;
                vertices.push_back(JPH::Float3(position.x, position.y, position.z));
            }
            // A mirroring scale turns triangles inside out.
            const bool mirrored = part.scale.x * part.scale.y * part.scale.z < 0.0f;
            JPH::IndexedTriangleList triangles;
            triangles.reserve(mesh->indices.size() / 3);
            for (std::size_t index = 0; index + 2 < mesh->indices.size(); index += 3)
            {
                triangles.push_back(mirrored ? JPH::IndexedTriangle(mesh->indices[index], mesh->indices[index + 2], mesh->indices[index + 1])
                                             : JPH::IndexedTriangle(mesh->indices[index], mesh->indices[index + 1], mesh->indices[index + 2]));
            }
            return create(JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)));
        }
        }
        return nullptr;
    }

    [[nodiscard]] JPH::RefConst<JPH::Shape> buildShape(const BodyDescription& description, scene::Scene& scene)
    {
        const bool dynamic = description.hasRigidBody && description.rigidBody.type == BodyType::Dynamic && !description.sensor;
        JPH::StaticCompoundShapeSettings compound;
        JPH::RefConst<JPH::Shape> single;
        math::Vec3 singlePosition{0.0f};
        math::Quat singleRotation{1.0f, 0.0f, 0.0f, 0.0f};
        std::size_t count = 0;
        for (const ShapePart& part : description.parts)
        {
            JPH::RefConst<JPH::Shape> shape = buildPartShape(part, dynamic, scene);
            if (shape == nullptr)
            {
                continue;
            }
            compound.AddShape(toJolt(part.position), toJolt(part.rotation), shape);
            single = shape;
            singlePosition = part.position;
            singleRotation = part.rotation;
            ++count;
        }
        if (count == 0)
        {
            return nullptr;
        }
        if (count == 1)
        {
            const bool offset = math::length(singlePosition) > 1.0e-6f || rotationsDiffer(singleRotation, math::Quat{1.0f, 0.0f, 0.0f, 0.0f});
            if (!offset)
            {
                return single;
            }
            JPH::ShapeSettings::ShapeResult result =
                JPH::RotatedTranslatedShapeSettings(toJolt(singlePosition), toJolt(singleRotation), single).Create();
            return result.HasError() ? nullptr : result.Get();
        }
        JPH::ShapeSettings::ShapeResult result = compound.Create();
        if (result.HasError())
        {
            warnOnce(description.owner, "compound", std::format("the colliders of '{}' cannot be combined: {}",
                                                                scene.name(description.owner), result.GetError().c_str()));
            return nullptr;
        }
        return result.Get();
    }

    // ---- Synchronization with the scene ----

    void describeCollider(scene::Scene& scene, std::unordered_map<std::uint64_t, BodyDescription>& descriptions, Entity entity,
                          bool trigger, ShapePart part)
    {
        if (scene.has<scene::CharacterController>(entity))
        {
            return;
        }
        const Entity owner = bodyOwner(scene, entity);
        const std::uint64_t key = keyOf(owner) * 2 + (trigger ? 1 : 0);
        BodyDescription& description = descriptions[key];
        if (!description.owner.isValid())
        {
            description.owner = owner;
            description.sensor = trigger;
            if (const scene::RigidBody* const rigidBody = scene.tryGet<scene::RigidBody>(owner))
            {
                description.hasRigidBody = true;
                description.rigidBody = *rigidBody;
            }
            const math::Trs ownerWorld = math::decomposeTrs(worldMatrixOf(scene, owner));
            description.position = ownerWorld.translation;
            description.rotation = math::normalize(ownerWorld.rotation);
        }
        // The collider relative to the body's position and rotation, keeping every scale on the way.
        const math::Mat4 ownerFrame = math::composeTrs({description.position, description.rotation, math::Vec3{1.0f}});
        const math::Trs relative = math::decomposeTrs(math::inverse(ownerFrame) * worldMatrixOf(scene, entity));
        part.position = relative.translation + math::normalize(relative.rotation) * (part.position * relative.scale);
        part.rotation = math::normalize(relative.rotation);
        part.scale = relative.scale;
        part.entity = entity;
        description.parts.push_back(part);
    }

    [[nodiscard]] std::unordered_map<std::uint64_t, BodyDescription> describeBodies(scene::Scene& scene)
    {
        std::unordered_map<std::uint64_t, BodyDescription> descriptions;
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::BoxCollider>())
        {
            describeCollider(scene, descriptions, entity, collider.trigger,
                             {.kind = ShapeKind::Box, .dimensions = collider.size, .position = collider.center});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::SphereCollider>())
        {
            describeCollider(scene, descriptions, entity, collider.trigger,
                             {.kind = ShapeKind::Sphere, .dimensions = {collider.radius, 0.0f, 0.0f}, .position = collider.center});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::CapsuleCollider>())
        {
            describeCollider(scene, descriptions, entity, collider.trigger,
                             {.kind = ShapeKind::Capsule, .dimensions = {collider.radius, collider.height, 0.0f}, .position = collider.center});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::CylinderCollider>())
        {
            describeCollider(scene, descriptions, entity, collider.trigger,
                             {.kind = ShapeKind::Cylinder, .dimensions = {collider.radius, collider.height, 0.0f}, .position = collider.center});
        }
        for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::MeshCollider>())
        {
            asset::AssetId mesh = collider.mesh;
            if (!mesh.isValid())
            {
                const scene::MeshRenderer* const renderer = scene.tryGet<scene::MeshRenderer>(entity);
                mesh = renderer != nullptr ? renderer->mesh : asset::AssetId{};
            }
            if (!mesh.isValid())
            {
                warnOnce(entity, "no mesh", std::format("the mesh collider of '{}' has no mesh", scene.name(entity)));
                continue;
            }
            describeCollider(scene, descriptions, entity, collider.trigger,
                             {.kind = ShapeKind::Mesh, .mesh = mesh, .convex = collider.convex});
        }
        for ([[maybe_unused]] auto [entity, rigidBody] : scene.view<scene::RigidBody>())
        {
            if (!descriptions.contains(keyOf(entity) * 2) && !descriptions.contains(keyOf(entity) * 2 + 1))
            {
                warnOnce(entity, "no collider", std::format("the RigidBody of '{}' has no collider", scene.name(entity)));
            }
        }
        for (auto& [key, description] : descriptions)
        {
            // Pools change order as components come and go: the colliders of a body keep theirs.
            std::ranges::sort(description.parts, {}, [](const ShapePart& part) { return keyOf(part.entity); });
            description.signature = signatureOf(description);
        }
        return descriptions;
    }

    void createBody(scene::Scene& scene, std::uint64_t key, const BodyDescription& description)
    {
        JPH::RefConst<JPH::Shape> shape = buildShape(description, scene);
        if (shape == nullptr)
        {
            return;
        }
        const scene::RigidBody& rigidBody = description.rigidBody;
        const BodyType bodyType = description.hasRigidBody ? rigidBody.type : BodyType::Static;
        // A trigger that moves with its body follows it as a kinematic sensor.
        const JPH::EMotionType motion = description.sensor
                                            ? (bodyType == BodyType::Static ? JPH::EMotionType::Static : JPH::EMotionType::Kinematic)
                                        : bodyType == BodyType::Dynamic   ? JPH::EMotionType::Dynamic
                                        : bodyType == BodyType::Kinematic ? JPH::EMotionType::Kinematic
                                                                          : JPH::EMotionType::Static;
        const bool moving = motion != JPH::EMotionType::Static;
        JPH::BodyCreationSettings settings(shape, toJoltPosition(description.position), toJolt(description.rotation), motion,
                                           objectLayer(rigidBody.layer, moving));
        settings.mUserData = keyOf(description.owner);
        settings.mIsSensor = description.sensor;
        // Triggers also notice kinematic and static bodies, such as characters.
        settings.mCollideKinematicVsNonDynamic = description.sensor;
        settings.mFriction = rigidBody.friction;
        settings.mRestitution = rigidBody.restitution;
        settings.mLinearDamping = rigidBody.linearDamping;
        settings.mAngularDamping = rigidBody.angularDamping;
        settings.mGravityFactor = rigidBody.gravityScale;
        settings.mMotionQuality = rigidBody.continuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        if (motion == JPH::EMotionType::Dynamic)
        {
            auto degrees = static_cast<std::uint8_t>(JPH::EAllowedDOFs::All);
            degrees &= static_cast<std::uint8_t>(~((rigidBody.lockRotationX ? static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationX) : 0) |
                                                   (rigidBody.lockRotationY ? static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationY) : 0) |
                                                   (rigidBody.lockRotationZ ? static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationZ) : 0)));
            settings.mAllowedDOFs = static_cast<JPH::EAllowedDOFs>(degrees);
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = std::max(rigidBody.mass, 0.001f);
            settings.mLinearVelocity = toJolt(limitVelocity(rigidBody.linearVelocity, settings.mMaxLinearVelocity));
            settings.mAngularVelocity = toJolt(limitVelocity(rigidBody.angularVelocity, settings.mMaxAngularVelocity));
        }

        const JPH::BodyID id = bodyInterface().CreateAndAddBody(
            settings, motion == JPH::EMotionType::Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (id.IsInvalid())
        {
            warnOnce(description.owner, "limit", std::format("'{}' has no body: the world holds {} bodies at most",
                                                             scene.name(description.owner), config.maxBodies));
            return;
        }
        owners[bodyKey(id)] = {description.owner, description.sensor};
        BodyRecord record{
            .owner = description.owner,
            .sensor = description.sensor,
            .type = description.sensor && bodyType != BodyType::Static ? BodyType::Kinematic : bodyType,
            .body = id,
            .signature = description.signature,
            .position = description.position,
            .rotation = description.rotation,
            .linearVelocity = rigidBody.linearVelocity,
            .angularVelocity = rigidBody.angularVelocity,
            .previousPosition = description.position,
            .previousRotation = description.rotation,
            .seen = true,
        };
        bodies[key] = record;
    }

    void removeBody(std::unordered_map<std::uint64_t, BodyRecord>::iterator record)
    {
        bodyInterface().RemoveBody(record->second.body);
        bodyInterface().DestroyBody(record->second.body);
        retire(record->second.body);
        bodies.erase(record);
    }

    void syncBodies(scene::Scene& scene, float seconds)
    {
        for (auto& [key, record] : bodies)
        {
            record.seen = false;
        }
        std::size_t created = 0;
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
                ++created;
                continue;
            }

            BodyRecord& body = record->second;
            body.seen = true;
            JPH::BodyInterface& interface = bodyInterface();
            const bool moved = math::length(description.position - body.position) > positionTolerance ||
                               rotationsDiffer(description.rotation, body.rotation);
            if (body.type == BodyType::Kinematic)
            {
                interface.MoveKinematic(body.body, toJoltPosition(description.position), toJolt(description.rotation), seconds);
                body.previousPosition = body.position;
                body.previousRotation = body.rotation;
                body.position = description.position;
                body.rotation = description.rotation;
            }
            else if (moved)
            {
                // Game code moved the entity: the body jumps there.
                interface.SetPositionAndRotation(body.body, toJoltPosition(description.position), toJolt(description.rotation),
                                                 JPH::EActivation::Activate);
                body.position = body.previousPosition = description.position;
                body.rotation = body.previousRotation = description.rotation;
            }
            if (body.type == BodyType::Dynamic && !body.sensor)
            {
                const scene::RigidBody& rigidBody = description.rigidBody;
                if (math::length(rigidBody.linearVelocity - body.linearVelocity) > positionTolerance ||
                    math::length(rigidBody.angularVelocity - body.angularVelocity) > positionTolerance)
                {
                    // Bodies keep the limits of Jolt's default settings.
                    static const JPH::BodyCreationSettings defaults;
                    interface.SetLinearAndAngularVelocity(
                        body.body, toJolt(limitVelocity(rigidBody.linearVelocity, defaults.mMaxLinearVelocity)),
                        toJolt(limitVelocity(rigidBody.angularVelocity, defaults.mMaxAngularVelocity)));
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
        if (created > 16)
        {
            system.OptimizeBroadPhase();
        }
    }

    void createCharacter(scene::Scene& scene, std::uint64_t key, const scene::CharacterController& controller,
                         math::Vec3 position, std::uint64_t signature)
    {
        const float radius = std::max(controller.radius, 0.01f);
        const float height = std::max(controller.height, radius * 2.0f + 0.01f);
        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        // The capsule stands on the position of the entity.
        JPH::ShapeSettings::ShapeResult shape =
            JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, height * 0.5f, 0.0f), JPH::Quat::sIdentity(),
                                                new JPH::CapsuleShape(height * 0.5f - radius, radius))
                .Create();
        if (shape.HasError())
        {
            return;
        }
        settings->mShape = shape.Get();
        settings->mInnerBodyShape = shape.Get();
        settings->mInnerBodyLayer = objectLayer(controller.layer, true);
        settings->mMaxSlopeAngle = controller.maxSlope;
        settings->mMass = controller.mass;
        settings->mMaxStrength = controller.pushForce;
        // Only contacts with the lower half sphere support the character.
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

        CharacterRecord record{.owner = scene::Entity{}, .signature = signature, .position = position, .previousPosition = position};
        record.owner = entityOf(key);
        record.character = new JPH::CharacterVirtual(settings, toJoltPosition(position), JPH::Quat::sIdentity(), key, &system);
        record.character->SetCharacterVsCharacterCollision(&characterCollisions);
        characterCollisions.Add(record.character);
        owners[bodyKey(record.character->GetInnerBodyID())] = {record.owner, false};
        record.seen = true;
        characters[key] = std::move(record);
        static_cast<void>(scene);
    }

    void syncCharacters(scene::Scene& scene)
    {
        for (auto& [key, record] : characters)
        {
            record.seen = false;
        }
        for ([[maybe_unused]] auto [entity, controller] : scene.view<scene::CharacterController>())
        {
            const std::uint64_t key = keyOf(entity);
            Signature signature;
            signature.add(controller.radius);
            signature.add(controller.height);
            signature.add(controller.maxSlope);
            signature.add(controller.stepHeight);
            signature.add(controller.mass);
            signature.add(controller.pushForce);
            signature.add(controller.layer);
            const math::Vec3 position = math::Vec3(worldMatrixOf(scene, entity)[3]);

            auto record = characters.find(key);
            if (record != characters.end() && record->second.signature != signature.value())
            {
                removeCharacter(record);
                record = characters.end();
            }
            if (record == characters.end())
            {
                createCharacter(scene, key, controller, position, signature.value());
                continue;
            }
            record->second.seen = true;
            if (math::length(position - record->second.position) > positionTolerance)
            {
                record->second.character->SetPosition(toJoltPosition(position));
                record->second.position = record->second.previousPosition = position;
            }
        }
        for (auto record = characters.begin(); record != characters.end();)
        {
            if (!record->second.seen)
            {
                const auto next = std::next(record);
                removeCharacter(record);
                record = next;
            }
            else
            {
                ++record;
            }
        }
    }

    void removeCharacter(std::unordered_map<std::uint64_t, CharacterRecord>::iterator record)
    {
        characterCollisions.Remove(record->second.character);
        retire(record->second.character->GetInnerBodyID());
        characters.erase(record);
    }

    void moveCharacters(scene::Scene& scene, float seconds)
    {
        for (auto& [key, record] : characters)
        {
            scene::CharacterController* const controller = scene.tryGet<scene::CharacterController>(record.owner);
            if (controller == nullptr)
            {
                continue;
            }
            JPH::CharacterVirtual& character = *record.character;
            const JPH::Vec3 gravity = system.GetGravity() * controller->gravityScale;
            character.UpdateGroundVelocity();
            JPH::Vec3 velocity = toJolt(controller->velocity);
            const bool grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            if (grounded && velocity.GetY() <= 0.0f)
            {
                // Standing: carried by the ground, and not accumulating gravity.
                velocity.SetY(0.0f);
                velocity += character.GetGroundVelocity();
            }
            else
            {
                velocity += gravity * seconds;
            }
            character.SetLinearVelocity(velocity);

            JPH::CharacterVirtual::ExtendedUpdateSettings update;
            update.mStickToFloorStepDown = JPH::Vec3(0.0f, -controller->stepHeight, 0.0f);
            update.mWalkStairsStepUp = JPH::Vec3(0.0f, controller->stepHeight, 0.0f);
            const JPH::ObjectLayer layer = objectLayer(controller->layer, true);
            const JPH::IgnoreSingleBodyFilter ignoreItself(character.GetInnerBodyID());
            character.ExtendedUpdate(seconds, gravity, update, system.GetDefaultBroadPhaseLayerFilter(layer),
                                     system.GetDefaultLayerFilter(layer), ignoreItself, {}, *temporaryAllocator);

            record.previousPosition = record.position;
            record.position = fromJoltPosition(character.GetPosition());
            setWorldPose(scene, record.owner, record.position, std::nullopt);
            controller->grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            controller->groundNormal = controller->grounded ? fromJolt(character.GetGroundNormal()) : math::Vec3{0.0f, 1.0f, 0.0f};
            // The velocity game code sees is relative to the ground it stands on.
            JPH::Vec3 result = character.GetLinearVelocity();
            if (controller->grounded)
            {
                result -= character.GetGroundVelocity();
            }
            controller->velocity = fromJolt(result);
        }
    }

    void writeBodies(scene::Scene& scene)
    {
        JPH::BodyInterface& interface = bodyInterface();
        for (auto& [key, record] : bodies)
        {
            if (record.type != BodyType::Dynamic || record.sensor)
            {
                continue;
            }
            JPH::RVec3 position;
            JPH::Quat rotation;
            interface.GetPositionAndRotation(record.body, position, rotation);
            record.previousPosition = record.position;
            record.previousRotation = record.rotation;
            record.position = fromJoltPosition(position);
            record.rotation = math::normalize(fromJolt(rotation));
            setWorldPose(scene, record.owner, record.position, record.rotation);

            JPH::Vec3 linear;
            JPH::Vec3 angular;
            interface.GetLinearAndAngularVelocity(record.body, linear, angular);
            record.linearVelocity = fromJolt(linear);
            record.angularVelocity = fromJolt(angular);
            if (scene::RigidBody* const rigidBody = scene.tryGet<scene::RigidBody>(record.owner))
            {
                rigidBody->linearVelocity = record.linearVelocity;
                rigidBody->angularVelocity = record.angularVelocity;
            }
        }
    }

    void gatherContacts()
    {
        for (const RawContact& raw : contactCollector.take())
        {
            const auto [low, high] = std::minmax(raw.first, raw.second);
            const std::uint64_t pair = core::hash64(std::as_bytes(std::span(&high, 1)), low);
            const auto first = owners.find(low);
            const auto second = owners.find(high);
            if (first == owners.end() || second == owners.end() || first->second.entity == second->second.entity)
            {
                continue;
            }
            const bool trigger = first->second.sensor || second->second.sensor;
            if (raw.added)
            {
                if (++contactCounts[pair] == 1)
                {
                    contacts.push_back({ContactPhase::Begin, first->second.entity, second->second.entity, trigger});
                }
            }
            else if (const auto count = contactCounts.find(pair); count != contactCounts.end() && --count->second == 0)
            {
                contactCounts.erase(count);
                contacts.push_back({ContactPhase::End, first->second.entity, second->second.entity, trigger});
            }
        }
        // Removed bodies have reported their last contacts.
        for (const std::uint64_t body : std::exchange(retiredBodies, {}))
        {
            owners.erase(body);
        }
    }

    [[nodiscard]] const BodyRecord* dynamicBody(Entity entity) const
    {
        const auto record = bodies.find(keyOf(entity) * 2);
        return record != bodies.end() && record->second.type == BodyType::Dynamic ? &record->second : nullptr;
    }
};

core::Result<std::unique_ptr<PhysicsWorld>> PhysicsWorld::create(PhysicsWorldConfig config)
{
    auto implementation = std::make_unique<Implementation>(std::move(config));
    if (!JPH::VerifyJoltVersionID())
    {
        return core::makeError(core::ErrorCode::Unsupported, "Jolt Physics was built with incompatible settings");
    }
    const std::uint32_t hardware = std::max(std::thread::hardware_concurrency(), 2u);
    const std::uint32_t threads =
        implementation->config.threads > 0 ? implementation->config.threads : std::min(hardware - 1, 8u);
    implementation->temporaryAllocator = std::make_unique<JPH::TempAllocatorImpl>(temporaryMemory);
    implementation->jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                                      static_cast<int>(threads));
    implementation->system.Init(implementation->config.maxBodies, 0, maxBodyPairs, maxContactConstraints,
                                implementation->broadPhaseLayers, implementation->objectVsBroadPhase,
                                implementation->objectPairs);
    implementation->system.SetGravity(toJolt(implementation->config.settings.gravity));
    implementation->system.SetContactListener(&implementation->contactCollector);
    return std::unique_ptr<PhysicsWorld>(new PhysicsWorld(std::move(implementation)));
}

PhysicsWorld::PhysicsWorld(std::unique_ptr<Implementation> implementation) noexcept
    : m_implementation(std::move(implementation))
{
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::step(scene::Scene& scene, core::Duration delta)
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
    world.moveCharacters(scene, seconds);
    if (const JPH::EPhysicsUpdateError error = world.system.Update(seconds, 1, world.temporaryAllocator.get(), world.jobs.get());
        error != JPH::EPhysicsUpdateError::None)
    {
        world.warnOnce(Entity{}, "update", "the simulation ran out of room for contacts; some collisions were missed");
    }
    world.gatherContacts();
    world.writeBodies(scene);
}

void PhysicsWorld::interpolate(scene::Scene& scene, float alpha)
{
    Implementation& world = *m_implementation;
    if (world.simulatedScene != &scene)
    {
        return;
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    for (const auto& [key, record] : world.bodies)
    {
        if (record.type != BodyType::Dynamic || record.sensor || !scene.isAlive(record.owner))
        {
            continue;
        }
        scene::WorldTransform* const transform = scene.tryGet<scene::WorldTransform>(record.owner);
        if (transform == nullptr)
        {
            continue;
        }
        const math::Trs current = math::decomposeTrs(transform->matrix);
        const math::Vec3 position = math::mix(record.previousPosition, record.position, alpha);
        const math::Quat rotation = math::slerp(record.previousRotation, record.rotation, alpha);
        transform->matrix = math::composeTrs({position, rotation, current.scale});
        updateDescendants(scene, record.owner, transform->matrix);
    }
    for (const auto& [key, record] : world.characters)
    {
        scene::WorldTransform* const transform = scene.isAlive(record.owner) ? scene.tryGet<scene::WorldTransform>(record.owner) : nullptr;
        if (transform == nullptr)
        {
            continue;
        }
        transform->matrix[3] = math::Vec4(math::mix(record.previousPosition, record.position, alpha), 1.0f);
        updateDescendants(scene, record.owner, transform->matrix);
    }
}

std::span<const Contact> PhysicsWorld::contacts() const noexcept
{
    return m_implementation->contacts;
}

void PhysicsWorld::clearContacts() noexcept
{
    m_implementation->contacts.clear();
}

std::optional<RayHit> PhysicsWorld::raycast(math::Vec3 origin, math::Vec3 direction, float maxDistance, std::uint16_t layers,
                                            Entity ignore) const
{
    const Implementation& world = *m_implementation;
    const float length = math::length(direction);
    if (length <= 0.0f || maxDistance <= 0.0f)
    {
        return std::nullopt;
    }
    const JPH::RRayCast ray{toJoltPosition(origin), toJolt(direction / length * maxDistance)};
    JPH::RayCastResult result;
    const LayerMaskFilter layerFilter(layers);
    const IgnoreEntityFilter bodyFilter(ignore);
    if (!world.system.GetNarrowPhaseQuery().CastRay(ray, result, {}, layerFilter, bodyFilter))
    {
        return std::nullopt;
    }
    RayHit hit;
    hit.distance = result.mFraction * maxDistance;
    hit.point = fromJoltPosition(ray.GetPointOnRay(result.mFraction));
    const JPH::BodyLockRead lock(world.system.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
    {
        const JPH::Body& body = lock.GetBody();
        hit.entity = entityOf(body.GetUserData());
        hit.normal = fromJolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
    }
    return hit;
}

std::optional<RayHit> PhysicsWorld::sphereCast(math::Vec3 origin, float radius, math::Vec3 direction, float maxDistance,
                                               std::uint16_t layers, Entity ignore) const
{
    const Implementation& world = *m_implementation;
    const float length = math::length(direction);
    if (length <= 0.0f || maxDistance <= 0.0f || radius <= 0.0f)
    {
        return std::nullopt;
    }
    const JPH::SphereShape sphere(radius);
    const JPH::RShapeCast cast(&sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(toJoltPosition(origin)),
                               toJolt(direction / length * maxDistance));
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const LayerMaskFilter layerFilter(layers);
    const IgnoreEntityFilter bodyFilter(ignore);
    world.system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    const JPH::ShapeCastResult& result = collector.mHit;
    RayHit hit;
    hit.distance = result.mFraction * maxDistance;
    hit.point = fromJolt(result.mContactPointOn2);
    hit.normal = fromJolt(-result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()));
    const JPH::BodyLockRead lock(world.system.GetBodyLockInterface(), result.mBodyID2);
    if (lock.Succeeded())
    {
        hit.entity = entityOf(lock.GetBody().GetUserData());
    }
    return hit;
}

std::vector<Entity> PhysicsWorld::overlapSphere(math::Vec3 center, float radius, std::uint16_t layers, Entity ignore) const
{
    const Implementation& world = *m_implementation;
    std::vector<Entity> entities;
    if (radius <= 0.0f)
    {
        return entities;
    }
    const JPH::SphereShape sphere(radius);
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const LayerMaskFilter layerFilter(layers);
    const IgnoreEntityFilter bodyFilter(ignore);
    world.system.GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(toJoltPosition(center)),
                                                    settings, JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);
    std::unordered_set<std::uint64_t> seen;
    for (const JPH::CollideShapeResult& result : collector.mHits)
    {
        const JPH::BodyLockRead lock(world.system.GetBodyLockInterface(), result.mBodyID2);
        if (lock.Succeeded() && seen.insert(lock.GetBody().GetUserData()).second)
        {
            entities.push_back(entityOf(lock.GetBody().GetUserData()));
        }
    }
    return entities;
}

void PhysicsWorld::addForce(Entity entity, math::Vec3 force)
{
    if (const BodyRecord* const body = m_implementation->dynamicBody(entity))
    {
        m_implementation->bodyInterface().AddForce(body->body, toJolt(force));
    }
}

void PhysicsWorld::addTorque(Entity entity, math::Vec3 torque)
{
    if (const BodyRecord* const body = m_implementation->dynamicBody(entity))
    {
        m_implementation->bodyInterface().AddTorque(body->body, toJolt(torque));
    }
}

void PhysicsWorld::addImpulse(Entity entity, math::Vec3 impulse)
{
    if (const BodyRecord* const body = m_implementation->dynamicBody(entity))
    {
        m_implementation->bodyInterface().AddImpulse(body->body, toJolt(impulse));
    }
}

void PhysicsWorld::addImpulseAt(Entity entity, math::Vec3 impulse, math::Vec3 point)
{
    if (const BodyRecord* const body = m_implementation->dynamicBody(entity))
    {
        m_implementation->bodyInterface().AddImpulse(body->body, toJolt(impulse), toJoltPosition(point));
    }
}

std::size_t PhysicsWorld::bodyCount() const noexcept
{
    return m_implementation->system.GetNumBodies();
}

const asset::PhysicsSettings& PhysicsWorld::settings() const noexcept
{
    return m_implementation->config.settings;
}

} // namespace devex::physics
