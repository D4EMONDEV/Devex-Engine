#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/EntityRef.hpp>

#include <numbers>

// Joints, as Godot's: a joint is an entity of its own, placed where the bodies are tied, which names
// body A and body B, the bodies of their RigidBody or RigidBody2D. Without body B, A is tied to the
// world. The angles, the positions along an axis and the motor are those of A against B. A joint
// with a break force or torque comes undone beyond them: it stays broken, and game code hears of it,
// until its component changes.
namespace devex::scene {

// Turns around the Z axis of its entity: doors, wheels, levers.
struct DEVEX_API HingeJoint
{
    EntityRef bodyA;
    EntityRef bodyB;
    // Whether the two bodies still collide with each other.
    bool collideConnected = false;
    bool useLimits = false;
    float lowerAngle = -std::numbers::pi_v<float> * 0.5f;
    float upperAngle = std::numbers::pi_v<float> * 0.5f;
    // Turns A at this speed, in radians a second, with at most this torque.
    bool useMotor = false;
    float motorSpeed = 0.0f;
    float maxMotorTorque = 1000.0f;
    // Newtons and newton meters past which the joint breaks; 0 never breaks it.
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    // Set by the physics once the joint broke. Never saved.
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(HingeJoint);

// Slides along the X axis of its entity: lifts, pistons, drawers.
struct DEVEX_API SliderJoint
{
    EntityRef bodyA;
    EntityRef bodyB;
    bool collideConnected = false;
    bool useLimits = false;
    float lowerLimit = -1.0f;
    float upperLimit = 1.0f;
    // Moves A at this speed, in meters a second, with at most this force.
    bool useMotor = false;
    float motorSpeed = 0.0f;
    float maxMotorForce = 1000.0f;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(SliderJoint);

// Keeps two points at a distance: the origin of its entity on A, and `anchor` on B, in the space of
// its entity. Rigid as a rod, soft as a spring, or a rope that only keeps them from parting farther.
struct DEVEX_API DistanceJoint
{
    EntityRef bodyA;
    EntityRef bodyB;
    bool collideConnected = false;
    math::Vec3 anchor{0.0f, -1.0f, 0.0f};
    // The distance kept; 0 keeps the one between the two points when the joint is made.
    float length = 0.0f;
    bool rope = false;
    // How often the spring swings, in hertz, and how much it calms down; 0 keeps the distance rigid.
    float spring = 0.0f;
    float damping = 0.5f;
    float breakForce = 0.0f;
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(DistanceJoint);

// Welds the two bodies as they stand.
struct DEVEX_API FixedJoint
{
    EntityRef bodyA;
    EntityRef bodyB;
    bool collideConnected = false;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(FixedJoint);

// The same in the XY plane, for the bodies of 2D physics.
struct DEVEX_API HingeJoint2D
{
    EntityRef bodyA;
    EntityRef bodyB;
    bool collideConnected = false;
    bool useLimits = false;
    float lowerAngle = -std::numbers::pi_v<float> * 0.5f;
    float upperAngle = std::numbers::pi_v<float> * 0.5f;
    bool useMotor = false;
    float motorSpeed = 0.0f;
    float maxMotorTorque = 1000.0f;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(HingeJoint2D);

struct DEVEX_API SliderJoint2D
{
    EntityRef bodyA;
    EntityRef bodyB;
    bool collideConnected = false;
    bool useLimits = false;
    float lowerLimit = -1.0f;
    float upperLimit = 1.0f;
    bool useMotor = false;
    float motorSpeed = 0.0f;
    float maxMotorForce = 1000.0f;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(SliderJoint2D);

struct DEVEX_API DistanceJoint2D
{
    EntityRef bodyA;
    EntityRef bodyB;
    bool collideConnected = false;
    math::Vec2 anchor{0.0f, -1.0f};
    float length = 0.0f;
    bool rope = false;
    float spring = 0.0f;
    float damping = 0.5f;
    float breakForce = 0.0f;
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(DistanceJoint2D);

struct DEVEX_API FixedJoint2D
{
    EntityRef bodyA;
    EntityRef bodyB;
    bool collideConnected = false;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    bool broken = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(FixedJoint2D);

} // namespace devex::scene
