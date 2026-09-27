#include <devex/scene/Physics2DComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(RigidBody2D)
{
    type.field("type", &RigidBody2D::type)
        .field("mass", &RigidBody2D::mass)
        .field("friction", &RigidBody2D::friction)
        .field("restitution", &RigidBody2D::restitution)
        .field("linear_damping", &RigidBody2D::linearDamping)
        .field("angular_damping", &RigidBody2D::angularDamping)
        .field("gravity_scale", &RigidBody2D::gravityScale)
        .field("layer", &RigidBody2D::layer, {.physicsLayer = true})
        .field("fixed_rotation", &RigidBody2D::fixedRotation)
        .field("continuous_collision", &RigidBody2D::continuousCollision)
        .field("linear_velocity", &RigidBody2D::linearVelocity)
        .field("angular_velocity", &RigidBody2D::angularVelocity);
}

DEVEX_REFLECT(BoxCollider2D)
{
    type.field("size", &BoxCollider2D::size)
        .field("center", &BoxCollider2D::center)
        .field("trigger", &BoxCollider2D::trigger)
        .field("one_way", &BoxCollider2D::oneWay);
}

DEVEX_REFLECT(CircleCollider2D)
{
    type.field("radius", &CircleCollider2D::radius)
        .field("center", &CircleCollider2D::center)
        .field("trigger", &CircleCollider2D::trigger);
}

DEVEX_REFLECT(CapsuleCollider2D)
{
    type.field("radius", &CapsuleCollider2D::radius)
        .field("height", &CapsuleCollider2D::height)
        .field("center", &CapsuleCollider2D::center)
        .field("trigger", &CapsuleCollider2D::trigger);
}

DEVEX_REFLECT(PolygonCollider2D)
{
    type.field("points", &PolygonCollider2D::points)
        .field("trigger", &PolygonCollider2D::trigger)
        .field("one_way", &PolygonCollider2D::oneWay);
}

DEVEX_REFLECT(TilemapCollider2D)
{
    type.field("trigger", &TilemapCollider2D::trigger);
}

DEVEX_REFLECT(CharacterController2D)
{
    type.field("radius", &CharacterController2D::radius)
        .field("height", &CharacterController2D::height)
        .field("max_slope", &CharacterController2D::maxSlope, {.angle = true})
        .field("step_height", &CharacterController2D::stepHeight)
        .field("gravity_scale", &CharacterController2D::gravityScale)
        .field("push_force", &CharacterController2D::pushForce)
        .field("layer", &CharacterController2D::layer, {.physicsLayer = true})
        .field("velocity", &CharacterController2D::velocity, {.runtime = true})
        .field("grounded", &CharacterController2D::grounded, {.runtime = true})
        .field("ground_normal", &CharacterController2D::groundNormal, {.runtime = true});
}

} // namespace devex::scene
