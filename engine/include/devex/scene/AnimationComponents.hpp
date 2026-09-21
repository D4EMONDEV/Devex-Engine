#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/EntityRef.hpp>

#include <cstdint>
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

} // namespace devex::scene
