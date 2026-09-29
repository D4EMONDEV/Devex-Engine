#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/PhysicsComponents.hpp>

#include <cstdint>
#include <vector>

// 2D physics components: what the 2D physics world simulates in the XY plane of the world. Bodies
// move along X and Y and turn around Z; the Z of their entities stays as it is. They share the
// collision layers of the project with 3D physics, but the two worlds do not touch each other.
namespace devex::scene {

// Makes its entity a 2D body, shaped by the 2D colliders of the entity and of its descendants
// that have no RigidBody2D of their own. 2D colliders without any RigidBody2D above them form
// static bodies. The simulation writes the position, the rotation and the velocities of dynamic
// bodies back each fixed step; setting them from game code moves the body.
struct DEVEX_API RigidBody2D
{
    BodyType type = BodyType::Dynamic;
    // In kilograms, for dynamic bodies, spread over the area of its colliders.
    float mass = 1.0f;
    float friction = 0.4f;
    // Bounciness: 0 absorbs impacts, 1 keeps all the energy.
    float restitution = 0.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    // Multiplies the gravity of the project for this body.
    float gravityScale = 1.0f;
    std::uint32_t layer = 0;
    // Prevents the body from turning, as for upright objects.
    bool fixedRotation = false;
    // Checks the motion between steps, so that fast small bodies do not pass through thin ones.
    bool continuousCollision = false;
    // In meters per second, and radians per second around Z.
    math::Vec2 linearVelocity{0.0f};
    float angularVelocity = 0.0f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(RigidBody2D);

// 2D colliders are sized in the space of their entity, whose scale they follow. A trigger detects
// what enters and leaves it without blocking anything. A one-way collider holds up what lands on
// it from above and lets through what comes from below or from the sides: ledges.
struct DEVEX_API BoxCollider2D
{
    // Full extents.
    math::Vec2 size{1.0f};
    math::Vec2 center{0.0f};
    bool trigger = false;
    bool oneWay = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(BoxCollider2D);

struct DEVEX_API CircleCollider2D
{
    float radius = 0.5f;
    math::Vec2 center{0.0f};
    bool trigger = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(CircleCollider2D);

// A capsule standing along Y.
struct DEVEX_API CapsuleCollider2D
{
    float radius = 0.25f;
    // Total height, the rounded ends included.
    float height = 1.0f;
    math::Vec2 center{0.0f};
    bool trigger = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(CapsuleCollider2D);

// A convex polygon of 3 to 8 points; points inside the hull of the others are dropped.
struct DEVEX_API PolygonCollider2D
{
    std::vector<math::Vec2> points{{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f}};
    bool trigger = false;
    bool oneWay = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(PolygonCollider2D);

// Collides with the tiles of the Tilemap of its entity, as its tileset says: full tiles merged into
// rectangles, top tiles into one-way ledges. Rebuilt when the cells or the tileset change.
struct DEVEX_API TilemapCollider2D
{
    bool trigger = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(TilemapCollider2D);

// A character moved by game code, "move and slide": a capsule standing on the position of its
// entity that slides along walls, walks up slopes and small steps, stays on the ground, lands on
// one-way ledges from above, is carried by what it stands on, and pushes dynamic bodies. Game code
// sets the velocity each frame; the simulation adds gravity, moves the character, and writes back
// the velocity it kept and whether it stands on the ground.
struct DEVEX_API CharacterController2D
{
    float radius = 0.3f;
    float height = 1.0f;
    // Steepest slope the character stands and walks on.
    float maxSlope = math::radians(50.0f);
    // Highest step the character climbs without jumping.
    float stepHeight = 0.25f;
    float gravityScale = 1.0f;
    // How hard it pushes dynamic bodies, in newtons.
    float pushForce = 200.0f;
    std::uint32_t layer = 0;

    // Runtime state, neither saved nor shown.
    math::Vec2 velocity{0.0f};
    bool grounded = false;
    math::Vec2 groundNormal{0.0f, 1.0f};
};
DEVEX_DECLARE_ENGINE_REFLECTION(CharacterController2D);

} // namespace devex::scene
