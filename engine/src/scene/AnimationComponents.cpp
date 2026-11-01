#include <devex/scene/AnimationComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(SkinnedMeshRenderer)
{
    type.field("mesh", &SkinnedMeshRenderer::mesh, {.assetType = "mesh"})
        .field("material", &SkinnedMeshRenderer::material, {.assetType = "material"})
        .field("bones", &SkinnedMeshRenderer::bones)
        .field("instance_shader_parameters", &SkinnedMeshRenderer::instanceShaderParameters,
               {.hidden = true})
        .field("instance_shader_values", &SkinnedMeshRenderer::instanceShaderValues,
               {.hidden = true})
        .field("lod_bias", &SkinnedMeshRenderer::lodBias, {.group = "Level of Detail"});
}

DEVEX_REFLECT(Animator)
{
    type.field("controller", &Animator::controller, {.assetType = "animator"})
        .field("clip", &Animator::clip, {.assetType = "animation"})
        .field("speed", &Animator::speed)
        .field("loop", &Animator::loop)
        .field("play_on_start", &Animator::playOnStart)
        .field("blend_time", &Animator::blendTime)
        .field("apply_root_motion", &Animator::applyRootMotion)
        .field("lod_distance", &Animator::lodDistance, {.group = "Level of Detail"})
        .field("lod_frame_rate", &Animator::lodFrameRate)
        .field("lod_far_distance", &Animator::lodFarDistance)
        .field("lod_far_frame_rate", &Animator::lodFarFrameRate);
}

DEVEX_REFLECT(Tweener)
{
    type.field("field", &Tweener::field)
        .field("from", &Tweener::from)
        .field("to", &Tweener::to)
        .field("from_current", &Tweener::fromCurrent)
        .field("relative", &Tweener::relative)
        .field("duration", &Tweener::duration)
        .field("delay", &Tweener::delay)
        .field("ease", &Tweener::ease)
        .field("curve", &Tweener::curve, {.assetType = "curve"})
        .field("loop", &Tweener::loop)
        .field("repeats", &Tweener::repeats)
        .field("play_on_start", &Tweener::playOnStart);
}

} // namespace devex::scene
