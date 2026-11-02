#include <devex/scene/PhysicsComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(RigidBody)
{
    type.field("type", &RigidBody::type, {.group = "Body"})
        .field("mass", &RigidBody::mass)
        .field("friction", &RigidBody::friction, {.group = "Material"})
        .field("restitution", &RigidBody::restitution)
        .field("linear_damping", &RigidBody::linearDamping, {.group = "Forces"})
        .field("angular_damping", &RigidBody::angularDamping)
        .field("gravity_scale", &RigidBody::gravityScale)
        .field("layer", &RigidBody::layer, {.physicsLayer = true, .group = "Collision"})
        .field("lock_rotation_x", &RigidBody::lockRotationX, {.group = "Constraints"})
        .field("lock_rotation_y", &RigidBody::lockRotationY)
        .field("lock_rotation_z", &RigidBody::lockRotationZ)
        .field("continuous_collision", &RigidBody::continuousCollision,
               {.group = "Continuous Collision"})
        .field("linear_velocity", &RigidBody::linearVelocity, {.group = "Velocity"})
        .field("angular_velocity", &RigidBody::angularVelocity);
}

DEVEX_REFLECT(BoxCollider)
{
    type.field("size", &BoxCollider::size, {.group = "Shape"})
        .field("center", &BoxCollider::center)
        .field("trigger", &BoxCollider::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(SphereCollider)
{
    type.field("radius", &SphereCollider::radius, {.group = "Shape"})
        .field("center", &SphereCollider::center)
        .field("trigger", &SphereCollider::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(CapsuleCollider)
{
    type.field("radius", &CapsuleCollider::radius, {.group = "Shape"})
        .field("height", &CapsuleCollider::height)
        .field("center", &CapsuleCollider::center)
        .field("trigger", &CapsuleCollider::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(CylinderCollider)
{
    type.field("radius", &CylinderCollider::radius, {.group = "Shape"})
        .field("height", &CylinderCollider::height)
        .field("center", &CylinderCollider::center)
        .field("trigger", &CylinderCollider::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(MeshCollider)
{
    type.field("mesh", &MeshCollider::mesh, {.assetType = "mesh", .group = "Shape"})
        .field("convex", &MeshCollider::convex)
        .field("trigger", &MeshCollider::trigger, {.group = "Collision"});
}

DEVEX_REFLECT(CharacterController)
{
    type.field("radius", &CharacterController::radius, {.group = "Shape"})
        .field("height", &CharacterController::height)
        .field("max_slope", &CharacterController::maxSlope, {.angle = true, .group = "Movement"})
        .field("step_height", &CharacterController::stepHeight)
        .field("mass", &CharacterController::mass, {.group = "Forces"})
        .field("push_force", &CharacterController::pushForce)
        .field("gravity_scale", &CharacterController::gravityScale)
        .field("layer", &CharacterController::layer, {.physicsLayer = true, .group = "Collision"});
}

} // namespace devex::scene
