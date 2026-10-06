#include <devex/scene/Light2DComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(PointLight2D)
{
    type.field("color", &PointLight2D::color, {.color = true})
        .field("energy", &PointLight2D::energy)
        .field("radius", &PointLight2D::radius)
        .field("falloff", &PointLight2D::falloff)
        .field("height", &PointLight2D::height)
        .field("item_mask", &PointLight2D::itemMask, {.bits = true})
        .field("shadows", &PointLight2D::shadows, {.group = "Shadows"})
        .field("shadow_mask", &PointLight2D::shadowMask, {.bits = true})
        .field("shadow_softness", &PointLight2D::shadowSoftness);
}

DEVEX_REFLECT(DirectionalLight2D)
{
    type.field("color", &DirectionalLight2D::color, {.color = true})
        .field("energy", &DirectionalLight2D::energy)
        .field("height", &DirectionalLight2D::height)
        .field("item_mask", &DirectionalLight2D::itemMask, {.bits = true})
        .field("shadows", &DirectionalLight2D::shadows, {.group = "Shadows"})
        .field("shadow_mask", &DirectionalLight2D::shadowMask, {.bits = true})
        .field("shadow_softness", &DirectionalLight2D::shadowSoftness)
        .field("shadow_distance", &DirectionalLight2D::shadowDistance);
}

DEVEX_REFLECT(LightOccluder2D)
{
    type.field("points", &LightOccluder2D::points)
        .field("closed", &LightOccluder2D::closed)
        .field("mask", &LightOccluder2D::mask, {.bits = true});
}

DEVEX_REFLECT(CanvasModulate)
{
    type.field("color", &CanvasModulate::color, {.color = true});
}

} // namespace devex::scene
