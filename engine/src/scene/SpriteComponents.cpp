#include <devex/scene/SpriteComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(SpriteRenderer)
{
    type.field("sprite", &SpriteRenderer::sprite, {.assetType = "sprite", .group = "Sprite"})
        .field("color", &SpriteRenderer::color, {.color = true})
        .field("intensity", &SpriteRenderer::intensity)
        .field("flip_x", &SpriteRenderer::flipX, {.group = "Drawing"})
        .field("flip_y", &SpriteRenderer::flipY)
        .field("draw_mode", &SpriteRenderer::drawMode)
        .field("size", &SpriteRenderer::size)
        .field("sorting_layer", &SpriteRenderer::sortingLayer,
               {.sortingLayer = true, .group = "Ordering"})
        .field("order", &SpriteRenderer::order)
        .field("blend", &SpriteRenderer::blend)
        .field("lit", &SpriteRenderer::lit, {.group = "Lighting"})
        .field("unshaded", &SpriteRenderer::unshaded)
        .field("light_mask", &SpriteRenderer::lightMask, {.bits = true})
        .field("material", &SpriteRenderer::material,
               {.assetType = "material", .group = "Material"})
        .field("instance_shader_parameters", &SpriteRenderer::instanceShaderParameters,
               {.hidden = true})
        .field("instance_shader_values", &SpriteRenderer::instanceShaderValues, {.hidden = true});
}

DEVEX_REFLECT(SpriteAnimator)
{
    type.field("frames", &SpriteAnimator::frames, {.assetType = "frames", .group = "Animation"})
        .field("animation", &SpriteAnimator::animation)
        .field("speed", &SpriteAnimator::speed, {.group = "Playback"})
        .field("playing", &SpriteAnimator::playing)
        .field("frame", &SpriteAnimator::frame);
}

} // namespace devex::scene
