#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <array>
#include <cstdint>
#include <string_view>

// Physics components: what the physics world simulates. They hold data only; the Physics module
// turns them into bodies while a game runs.
namespace devex::scene {

enum class BodyType : std::uint8_t
{
    // Never moves by itself, such as the ground and walls.
    Static,
    // Moved by game code through its Transform, pushing dynamic bodies without being pushed.
    Kinematic,
    // Moved by forces, gravity and collisions.
    Dynamic,
};

// Makes its entity a body of the physics world, shaped by the colliders of the entity and of its
// descendants that have no RigidBody of their own. Colliders without any RigidBody above them form
// static bodies. The simulation writes the position, rotation and velocities of dynamic bodies
// back each fixed step; setting them from game code moves the body.
struct RigidBody
{
    BodyType type = BodyType::Dynamic;
    // In kilograms, for dynamic bodies.
    float mass = 1.0f;
    float friction = 0.5f;
    // Bounciness: 0 absorbs impacts, 1 keeps all the energy.
    float restitution = 0.0f;
    // Fraction of the linear and angular velocity lost per second.
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    // Multiplies the gravity of the project for this body.
    float gravityScale = 1.0f;
    // The collision layer of the project the body belongs to.
    std::uint32_t layer = 0;
    // Prevents rotations around world axes, as for upright objects.
    bool lockRotationX = false;
    bool lockRotationY = false;
    bool lockRotationZ = false;
    // Checks the motion between steps, so that fast small bodies do not pass through thin ones.
    bool continuousCollision = false;
    // In meters per second, and radians per second around each axis, in world space.
    math::Vec3 linearVelocity{0.0f};
    math::Vec3 angularVelocity{0.0f};
};
DEVEX_DECLARE_REFLECTION(RigidBody);

// Colliders are sized in the space of their entity, whose scale they follow. A trigger detects
// what enters and leaves it without blocking anything.
struct BoxCollider
{
    // Full extents.
    math::Vec3 size{1.0f};
    math::Vec3 center{0.0f};
    bool trigger = false;
};
DEVEX_DECLARE_REFLECTION(BoxCollider);

struct SphereCollider
{
    float radius = 0.5f;
    math::Vec3 center{0.0f};
    bool trigger = false;
};
DEVEX_DECLARE_REFLECTION(SphereCollider);

// A capsule along the Y axis.
struct CapsuleCollider
{
    float radius = 0.5f;
    // Total height, including the rounded ends.
    float height = 2.0f;
    math::Vec3 center{0.0f};
    bool trigger = false;
};
DEVEX_DECLARE_REFLECTION(CapsuleCollider);

// A cylinder along the Y axis.
struct CylinderCollider
{
    float radius = 0.5f;
    float height = 1.0f;
    math::Vec3 center{0.0f};
    bool trigger = false;
};
DEVEX_DECLARE_REFLECTION(CylinderCollider);

// The triangles of a mesh, for static scenery, or their convex hull, which dynamic bodies need.
struct MeshCollider
{
    // Without a mesh, the mesh of the entity's MeshRenderer.
    asset::AssetId mesh;
    bool convex = false;
    bool trigger = false;
};
DEVEX_DECLARE_REFLECTION(MeshCollider);

// A character moved by game code: it walks up slopes and steps, stays on the ground, is carried by
// moving platforms and pushes dynamic bodies. Its entity's position is at the bottom of its capsule.
// Game code sets the velocity each frame; the simulation adds gravity, moves the character and
// writes back the velocity and whether it stands on the ground.
struct CharacterController
{
    float radius = 0.35f;
    float height = 1.8f;
    // Steepest slope the character walks up.
    float maxSlope = math::radians(45.0f);
    // Highest step the character climbs without jumping.
    float stepHeight = 0.35f;
    float mass = 70.0f;
    // Largest force, in newtons, the character pushes dynamic bodies with.
    float pushForce = 300.0f;
    float gravityScale = 1.0f;
    std::uint32_t layer = 0;

    // Runtime state, not saved.
    math::Vec3 velocity{0.0f};
    bool grounded = false;
    math::Vec3 groundNormal{0.0f, 1.0f, 0.0f};
};
DEVEX_DECLARE_REFLECTION(CharacterController);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::BodyType>
{
    static constexpr std::array<std::string_view, 3> names{"static", "kinematic", "dynamic"};
};
