#include <devex/scene/AnimationComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(SkinnedMeshRenderer)
{
    type.field("mesh", &SkinnedMeshRenderer::mesh, {.assetType = "mesh"})
        .field("material", &SkinnedMeshRenderer::material, {.assetType = "material"})
        .field("bones", &SkinnedMeshRenderer::bones);
}

DEVEX_REFLECT(Animator)
{
    type.field("clip", &Animator::clip, {.assetType = "animation"})
        .field("speed", &Animator::speed)
        .field("loop", &Animator::loop)
        .field("play_on_start", &Animator::playOnStart)
        .field("blend_time", &Animator::blendTime)
        .field("apply_root_motion", &Animator::applyRootMotion);
}

} // namespace devex::scene
