#include <devex/scene/PhysicsComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(RigidBody)
{
    type.field("type", &RigidBody::type)
        .field("mass", &RigidBody::mass)
        .field("friction", &RigidBody::friction)
        .field("restitution", &RigidBody::restitution)
        .field("linear_damping", &RigidBody::linearDamping)
        .field("angular_damping", &RigidBody::angularDamping)
        .field("gravity_scale", &RigidBody::gravityScale)
        .field("layer", &RigidBody::layer, {.physicsLayer = true})
        .field("lock_rotation_x", &RigidBody::lockRotationX)
        .field("lock_rotation_y", &RigidBody::lockRotationY)
        .field("lock_rotation_z", &RigidBody::lockRotationZ)
        .field("continuous_collision", &RigidBody::continuousCollision)
        .field("linear_velocity", &RigidBody::linearVelocity)
        .field("angular_velocity", &RigidBody::angularVelocity);
}

DEVEX_REFLECT(BoxCollider)
{
    type.field("size", &BoxCollider::size)
        .field("center", &BoxCollider::center)
        .field("trigger", &BoxCollider::trigger);
}

DEVEX_REFLECT(SphereCollider)
{
    type.field("radius", &SphereCollider::radius)
        .field("center", &SphereCollider::center)
        .field("trigger", &SphereCollider::trigger);
}

DEVEX_REFLECT(CapsuleCollider)
{
    type.field("radius", &CapsuleCollider::radius)
        .field("height", &CapsuleCollider::height)
        .field("center", &CapsuleCollider::center)
        .field("trigger", &CapsuleCollider::trigger);
}

DEVEX_REFLECT(CylinderCollider)
{
    type.field("radius", &CylinderCollider::radius)
        .field("height", &CylinderCollider::height)
        .field("center", &CylinderCollider::center)
        .field("trigger", &CylinderCollider::trigger);
}

DEVEX_REFLECT(MeshCollider)
{
    type.field("mesh", &MeshCollider::mesh, {.assetType = "mesh"})
        .field("convex", &MeshCollider::convex)
        .field("trigger", &MeshCollider::trigger);
}

DEVEX_REFLECT(CharacterController)
{
    type.field("radius", &CharacterController::radius)
        .field("height", &CharacterController::height)
        .field("max_slope", &CharacterController::maxSlope, {.angle = true})
        .field("step_height", &CharacterController::stepHeight)
        .field("mass", &CharacterController::mass)
        .field("push_force", &CharacterController::pushForce)
        .field("gravity_scale", &CharacterController::gravityScale)
        .field("layer", &CharacterController::layer, {.physicsLayer = true});
}

} // namespace devex::scene
