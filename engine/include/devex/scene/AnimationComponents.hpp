#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/math/Easing.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/EntityRef.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace devex::scene {

// Draws a mesh whose vertices follow bones instead of one transform. The bones are entities of the
// scene, in the order of the joints of the mesh; a missing bone leaves its vertices at the bind
// pose. Instantiating a model with a skin adds this component and its bones.
struct SkinnedMeshRenderer
{
    asset::AssetId mesh;
    // Overrides the materials of the mesh; invalid keeps them.
    asset::AssetId material;
    std::vector<EntityRef> bones;
};
DEVEX_DECLARE_REFLECTION(SkinnedMeshRenderer);

// Plays animation clips on the bones under its entity. Clips drive bones by name, so a clip
// imported with one model plays on any skeleton whose bones carry the same names.
struct Animator
{
    // The clip that plays; changing it from code or from the inspector starts the new one.
    asset::AssetId clip;
    // Times the speed of the clip; 0 holds the current pose.
    float speed = 1.0f;
    bool loop = true;
    // Starts the clip when the entity appears in a game that plays.
    bool playOnStart = true;
    // Seconds of the crossfade from the previous clip to a new one.
    float blendTime = 0.2f;
    // Moves the entity with the root bone of the clip instead of animating it in place. With a
    // CharacterController, the motion is given to it rather than to the Transform.
    bool applyRootMotion = false;
};
DEVEX_DECLARE_REFLECTION(Animator);

// What a tween does once it reaches its end.
enum class TweenLoop : std::uint8_t
{
    // It stops there.
    None,
    // It starts again from its start.
    Restart,
    // It goes back to its start, then forth again.
    PingPong,
};

// Animates a field of a component of its entity between two values, without code: a light that
// pulses, a crate that floats, a sign that blinks. Code starts the same tweens with the TweenWorld,
// on any entity.
struct Tweener
{
    // The field, as "Component.field": "Transform.position", "UiRect.opacity", "UiImage.color". It
    // holds a number, a vector, a color or a rotation.
    std::string field = "Transform.position";
    // The values, in as many components as the field has: x for a number, xyz for a position, all
    // four for a color. A rotation is written in degrees around x, y and z.
    math::Vec4 from{0.0f};
    math::Vec4 to{0.0f, 1.0f, 0.0f, 0.0f};
    // Starts from the value the field has instead of from.
    bool fromCurrent = true;
    // Adds to to the value it starts from, rather than going to it.
    bool relative = true;
    // In seconds.
    float duration = 1.0f;
    float delay = 0.0f;
    math::Ease ease = math::Ease::InOutSine;
    // A curve drawn by hand, which replaces the ease when set.
    asset::AssetId curve;
    TweenLoop loop = TweenLoop::PingPong;
    // How many times it plays again once over; -1 without end.
    std::int32_t repeats = -1;
    // Starts when the entity appears in a game that plays; otherwise code starts it.
    bool playOnStart = true;
};
DEVEX_DECLARE_REFLECTION(Tweener);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::math::Ease>
{
    static constexpr std::array<std::string_view, devex::math::easeCount> names{
        "linear",      "in_quad",      "out_quad",       "in_out_quad",    "in_cubic",     "out_cubic",
        "in_out_cubic", "in_sine",     "out_sine",       "in_out_sine",    "in_expo",      "out_expo",
        "in_out_expo", "in_back",      "out_back",       "in_out_back",    "in_elastic",   "out_elastic",
        "in_out_elastic", "in_bounce", "out_bounce",     "in_out_bounce"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::TweenLoop>
{
    static constexpr std::array<std::string_view, 3> names{"none", "restart", "ping_pong"};
};
