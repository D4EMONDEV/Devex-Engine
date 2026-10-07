#include <devex/scene/JointComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(HingeJoint)
{
    type.field("body_a", &HingeJoint::bodyA)
        .field("body_b", &HingeJoint::bodyB)
        .field("collide_connected", &HingeJoint::collideConnected)
        .field("use_limits", &HingeJoint::useLimits, {.group = "Limits"})
        .field("lower_angle", &HingeJoint::lowerAngle, {.angle = true})
        .field("upper_angle", &HingeJoint::upperAngle, {.angle = true})
        .field("use_motor", &HingeJoint::useMotor, {.group = "Motor"})
        .field("motor_speed", &HingeJoint::motorSpeed, {.angle = true})
        .field("max_motor_torque", &HingeJoint::maxMotorTorque)
        .field("break_force", &HingeJoint::breakForce, {.group = "Breaking"})
        .field("break_torque", &HingeJoint::breakTorque)
        .field("broken", &HingeJoint::broken, {.runtime = true});
}

DEVEX_REFLECT(SliderJoint)
{
    type.field("body_a", &SliderJoint::bodyA)
        .field("body_b", &SliderJoint::bodyB)
        .field("collide_connected", &SliderJoint::collideConnected)
        .field("use_limits", &SliderJoint::useLimits, {.group = "Limits"})
        .field("lower_limit", &SliderJoint::lowerLimit)
        .field("upper_limit", &SliderJoint::upperLimit)
        .field("use_motor", &SliderJoint::useMotor, {.group = "Motor"})
        .field("motor_speed", &SliderJoint::motorSpeed)
        .field("max_motor_force", &SliderJoint::maxMotorForce)
        .field("break_force", &SliderJoint::breakForce, {.group = "Breaking"})
        .field("break_torque", &SliderJoint::breakTorque)
        .field("broken", &SliderJoint::broken, {.runtime = true});
}

DEVEX_REFLECT(DistanceJoint)
{
    type.field("body_a", &DistanceJoint::bodyA)
        .field("body_b", &DistanceJoint::bodyB)
        .field("collide_connected", &DistanceJoint::collideConnected)
        .field("anchor", &DistanceJoint::anchor)
        .field("length", &DistanceJoint::length)
        .field("rope", &DistanceJoint::rope)
        .field("spring", &DistanceJoint::spring, {.group = "Spring"})
        .field("damping", &DistanceJoint::damping)
        .field("break_force", &DistanceJoint::breakForce, {.group = "Breaking"})
        .field("broken", &DistanceJoint::broken, {.runtime = true});
}

DEVEX_REFLECT(FixedJoint)
{
    type.field("body_a", &FixedJoint::bodyA)
        .field("body_b", &FixedJoint::bodyB)
        .field("collide_connected", &FixedJoint::collideConnected)
        .field("break_force", &FixedJoint::breakForce, {.group = "Breaking"})
        .field("break_torque", &FixedJoint::breakTorque)
        .field("broken", &FixedJoint::broken, {.runtime = true});
}

DEVEX_REFLECT(HingeJoint2D)
{
    type.field("body_a", &HingeJoint2D::bodyA)
        .field("body_b", &HingeJoint2D::bodyB)
        .field("collide_connected", &HingeJoint2D::collideConnected)
        .field("use_limits", &HingeJoint2D::useLimits, {.group = "Limits"})
        .field("lower_angle", &HingeJoint2D::lowerAngle, {.angle = true})
        .field("upper_angle", &HingeJoint2D::upperAngle, {.angle = true})
        .field("use_motor", &HingeJoint2D::useMotor, {.group = "Motor"})
        .field("motor_speed", &HingeJoint2D::motorSpeed, {.angle = true})
        .field("max_motor_torque", &HingeJoint2D::maxMotorTorque)
        .field("break_force", &HingeJoint2D::breakForce, {.group = "Breaking"})
        .field("break_torque", &HingeJoint2D::breakTorque)
        .field("broken", &HingeJoint2D::broken, {.runtime = true});
}

DEVEX_REFLECT(SliderJoint2D)
{
    type.field("body_a", &SliderJoint2D::bodyA)
        .field("body_b", &SliderJoint2D::bodyB)
        .field("collide_connected", &SliderJoint2D::collideConnected)
        .field("use_limits", &SliderJoint2D::useLimits, {.group = "Limits"})
        .field("lower_limit", &SliderJoint2D::lowerLimit)
        .field("upper_limit", &SliderJoint2D::upperLimit)
        .field("use_motor", &SliderJoint2D::useMotor, {.group = "Motor"})
        .field("motor_speed", &SliderJoint2D::motorSpeed)
        .field("max_motor_force", &SliderJoint2D::maxMotorForce)
        .field("break_force", &SliderJoint2D::breakForce, {.group = "Breaking"})
        .field("break_torque", &SliderJoint2D::breakTorque)
        .field("broken", &SliderJoint2D::broken, {.runtime = true});
}

DEVEX_REFLECT(DistanceJoint2D)
{
    type.field("body_a", &DistanceJoint2D::bodyA)
        .field("body_b", &DistanceJoint2D::bodyB)
        .field("collide_connected", &DistanceJoint2D::collideConnected)
        .field("anchor", &DistanceJoint2D::anchor)
        .field("length", &DistanceJoint2D::length)
        .field("rope", &DistanceJoint2D::rope)
        .field("spring", &DistanceJoint2D::spring, {.group = "Spring"})
        .field("damping", &DistanceJoint2D::damping)
        .field("break_force", &DistanceJoint2D::breakForce, {.group = "Breaking"})
        .field("broken", &DistanceJoint2D::broken, {.runtime = true});
}

DEVEX_REFLECT(FixedJoint2D)
{
    type.field("body_a", &FixedJoint2D::bodyA)
        .field("body_b", &FixedJoint2D::bodyB)
        .field("collide_connected", &FixedJoint2D::collideConnected)
        .field("break_force", &FixedJoint2D::breakForce, {.group = "Breaking"})
        .field("break_torque", &FixedJoint2D::breakTorque)
        .field("broken", &FixedJoint2D::broken, {.runtime = true});
}

} // namespace devex::scene
