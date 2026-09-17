#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace devex::scene {
class Scene;
}

// Rigid bodies, colliders and characters simulated with Jolt Physics, from the physics components
// of a scene.
namespace devex::physics {

// A mask of collision layers with every layer.
inline constexpr std::uint16_t allLayers = 0xFFFF;

struct RayHit
{
    // The entity that owns the body hit: the entity of its RigidBody, or of its collider.
    scene::Entity entity;
    math::Vec3 point{0.0f};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
};

enum class ContactPhase : std::uint8_t
{
    Begin,
    End,
};

// Two bodies that started or stopped touching. A trigger contact involves at least one trigger:
// nothing blocked, something entered or left.
struct Contact
{
    ContactPhase phase = ContactPhase::Begin;
    scene::Entity first;
    scene::Entity second;
    bool trigger = false;

    // Whether the contact involves the entity.
    [[nodiscard]] bool involves(scene::Entity entity) const noexcept
    {
        return first == entity || second == entity;
    }

    // The entity touching the given one.
    [[nodiscard]] scene::Entity other(scene::Entity entity) const noexcept
    {
        return first == entity ? second : first;
    }
};

// The triangles of a mesh asset, for mesh colliders; null when the mesh cannot be loaded. The data
// must stay valid while the world exists.
using MeshSource = std::function<const asset::MeshData*(asset::AssetId mesh)>;

struct PhysicsWorldConfig
{
    asset::PhysicsSettings settings;
    MeshSource meshes;
    std::uint32_t maxBodies = 65536;
    // Worker threads of the simulation; 0 uses the hardware threads but one, at most 8.
    std::uint32_t threads = 0;
};

// The simulation of one scene. Each step brings the bodies in line with the physics components of
// the scene, so that game code creates, changes, moves and removes bodies by editing components
// and transforms, then advances the simulation and writes the result back to the scene.
class PhysicsWorld
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<PhysicsWorld>> create(PhysicsWorldConfig config);
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Advances the simulation of the scene by a fixed step. Bodies are created for new components,
    // rebuilt when their components change and removed with them; a body whose entity game code
    // moved is teleported, a kinematic body follows its entity. Characters move with the velocity
    // of their CharacterController. Afterwards, dynamic bodies and characters write their
    // Transform, velocities and ground state. World transforms must be up to date. Using the
    // world with another scene starts over from that scene.
    void step(scene::Scene& scene, core::Duration delta);

    // Places dynamic bodies, characters and their descendants between their poses of the last two
    // steps, by alpha from the previous one, in world transforms only, so that motion stays smooth
    // on displays faster than the fixed step. Call after Scene::updateTransforms, before rendering.
    void interpolate(scene::Scene& scene, float alpha);

    // The contacts that began or ended during the steps since clearContacts.
    [[nodiscard]] std::span<const Contact> contacts() const noexcept;
    void clearContacts() noexcept;

    // The closest body along a ray within the distance, among the layers of the mask, ignoring the
    // bodies of an entity. The direction does not need to be normalized.
    [[nodiscard]] std::optional<RayHit> raycast(math::Vec3 origin, math::Vec3 direction, float maxDistance,
                                                std::uint16_t layers = allLayers, scene::Entity ignore = {}) const;
    // The first body a sphere moving along the direction touches.
    [[nodiscard]] std::optional<RayHit> sphereCast(math::Vec3 origin, float radius, math::Vec3 direction,
                                                   float maxDistance, std::uint16_t layers = allLayers,
                                                   scene::Entity ignore = {}) const;
    // The entities whose bodies intersect a sphere, each once.
    [[nodiscard]] std::vector<scene::Entity> overlapSphere(math::Vec3 center, float radius,
                                                           std::uint16_t layers = allLayers,
                                                           scene::Entity ignore = {}) const;

    // Act on the dynamic body of an entity, in world space; ignored for other entities. Forces and
    // torques apply during the next step, impulses change the velocity at once.
    void addForce(scene::Entity entity, math::Vec3 force);
    void addTorque(scene::Entity entity, math::Vec3 torque);
    void addImpulse(scene::Entity entity, math::Vec3 impulse);
    void addImpulseAt(scene::Entity entity, math::Vec3 impulse, math::Vec3 point);

    // Bodies in the simulation, including the ones inside characters.
    [[nodiscard]] std::size_t bodyCount() const noexcept;
    [[nodiscard]] const asset::PhysicsSettings& settings() const noexcept;

private:
    struct Implementation;

    explicit PhysicsWorld(std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> m_implementation;
};

} // namespace devex::physics
