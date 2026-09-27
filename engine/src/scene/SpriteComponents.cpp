#include <devex/scene/SpriteComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(SpriteRenderer)
{
    type.field("sprite", &SpriteRenderer::sprite, {.assetType = "sprite"})
        .field("color", &SpriteRenderer::color, {.color = true})
        .field("intensity", &SpriteRenderer::intensity)
        .field("flip_x", &SpriteRenderer::flipX)
        .field("flip_y", &SpriteRenderer::flipY)
        .field("draw_mode", &SpriteRenderer::drawMode)
        .field("size", &SpriteRenderer::size)
        .field("sorting_layer", &SpriteRenderer::sortingLayer, {.sortingLayer = true})
        .field("order", &SpriteRenderer::order)
        .field("blend", &SpriteRenderer::blend)
        .field("lit", &SpriteRenderer::lit);
}

DEVEX_REFLECT(SpriteAnimator)
{
    type.field("frames", &SpriteAnimator::frames, {.assetType = "frames"})
        .field("animation", &SpriteAnimator::animation)
        .field("speed", &SpriteAnimator::speed)
        .field("playing", &SpriteAnimator::playing)
        .field("frame", &SpriteAnimator::frame);
}

} // namespace devex::scene
