#include <devex/scene/Physics2DComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(RigidBody2D)
{
    type.field("type", &RigidBody2D::type, {.group = "Body"})
        .field("mass", &RigidBody2D::mass)
        .field("friction", &RigidBody2D::friction, {.group = "Material"})
        .field("restitution", &RigidBody2D::restitution)
        .field("linear_damping", &RigidBody2D::linearDamping, {.group = "Forces"})
        .field("angular_damping", &RigidBody2D::angularDamping)
        .field("gravity_scale", &RigidBody2D::gravityScale)
        .field("layer", &RigidBody2D::layer, {.physicsLayer = true, .group = "Collision"})
        .field("fixed_rotation", &RigidBody2D::fixedRotation, {.group = "Constraints"})
        .field("continuous_collision", &RigidBody2D::continuousCollision,
               {.group = "Continuous Collision"})
        .field("linear_velocity", &RigidBody2D::linearVelocity, {.group = "Velocity"})
        .field("angular_velocity", &RigidBody2D::angularVelocity);
}

DEVEX_REFLECT(BoxCollider2D)
{
    type.field("size", &BoxCollider2D::size, {.group = "Shape"})
        .field("center", &BoxCollider2D::center)
        .field("trigger", &BoxCollider2D::trigger, {.group = "Collision"})
        .field("one_way", &BoxCollider2D::oneWay);
}

DEVEX_REFLECT(CircleCollider2D)
{
    type.field("radius", &CircleCollider2D::radius, {.group = "Shape"})
        .field("center", &CircleCollider2D::center)
        .field("trigger", &CircleCollider2D::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(CapsuleCollider2D)
{
    type.field("radius", &CapsuleCollider2D::radius, {.group = "Shape"})
        .field("height", &CapsuleCollider2D::height)
        .field("center", &CapsuleCollider2D::center)
        .field("trigger", &CapsuleCollider2D::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(PolygonCollider2D)
{
    type.field("points", &PolygonCollider2D::points, {.group = "Shape"})
        .field("trigger", &PolygonCollider2D::trigger, {.group = "Collision"})
        .field("one_way", &PolygonCollider2D::oneWay);
}

DEVEX_REFLECT(TilemapCollider2D)
{
    type.field("trigger", &TilemapCollider2D::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(CharacterController2D)
{
    type.field("radius", &CharacterController2D::radius, {.group = "Shape"})
        .field("height", &CharacterController2D::height)
        .field("max_slope", &CharacterController2D::maxSlope, {.angle = true, .group = "Movement"})
        .field("step_height", &CharacterController2D::stepHeight)
        .field("gravity_scale", &CharacterController2D::gravityScale, {.group = "Forces"})
        .field("push_force", &CharacterController2D::pushForce)
        .field("layer", &CharacterController2D::layer, {.physicsLayer = true, .group = "Collision"})
        .field("velocity", &CharacterController2D::velocity, {.runtime = true})
        .field("grounded", &CharacterController2D::grounded, {.runtime = true})
        .field("ground_normal", &CharacterController2D::groundNormal, {.runtime = true});
}

} // namespace devex::scene
