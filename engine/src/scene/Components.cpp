#include <devex/scene/Components.hpp>

namespace devex::scene {

DEVEX_REFLECT(Transform)
{
    type.field("position", &Transform::position)
        .field("rotation", &Transform::rotation)
        .field("scale", &Transform::scale);
}

DEVEX_REFLECT(MeshRenderer)
{
    type.field("mesh", &MeshRenderer::mesh, {.assetType = "mesh"})
        .field("material", &MeshRenderer::material, {.assetType = "material"});
}

DEVEX_REFLECT(Camera)
{
    type.field("vertical_fov", &Camera::verticalFov, {.angle = true})
        .field("near_plane", &Camera::nearPlane)
        .field("primary", &Camera::primary)
        .field("auto_exposure", &Camera::autoExposure)
        .field("ev100", &Camera::ev100)
        .field("exposure_compensation", &Camera::exposureCompensation)
        .field("min_ev100", &Camera::minEv100)
        .field("max_ev100", &Camera::maxEv100)
        .field("adaptation_speed", &Camera::adaptationSpeed)
        .field("tonemapper", &Camera::tonemapper)
        .field("antialiasing", &Camera::antialiasing)
        .field("ambient_occlusion", &Camera::ambientOcclusion)
        .field("ambient_occlusion_radius", &Camera::ambientOcclusionRadius)
        .field("bloom", &Camera::bloom)
        .field("bloom_threshold", &Camera::bloomThreshold)
        .field("vignette", &Camera::vignette)
        .field("grain", &Camera::grain)
        .field("chromatic_aberration", &Camera::chromaticAberration)
        .field("color_table", &Camera::colorTable, {.assetType = "texture"});
}

DEVEX_REFLECT(DirectionalLight)
{
    type.field("color", &DirectionalLight::color, {.color = true})
        .field("temperature", &DirectionalLight::temperature)
        .field("illuminance", &DirectionalLight::illuminance)
        .field("cast_shadows", &DirectionalLight::castShadows)
        .field("shadow_distance", &DirectionalLight::shadowDistance);
}

DEVEX_REFLECT(PointLight)
{
    type.field("color", &PointLight::color, {.color = true})
        .field("temperature", &PointLight::temperature)
        .field("intensity", &PointLight::intensity)
        .field("range", &PointLight::range);
}

DEVEX_REFLECT(SpotLight)
{
    type.field("color", &SpotLight::color, {.color = true})
        .field("temperature", &SpotLight::temperature)
        .field("intensity", &SpotLight::intensity)
        .field("range", &SpotLight::range)
        .field("inner_angle", &SpotLight::innerAngle, {.angle = true})
        .field("outer_angle", &SpotLight::outerAngle, {.angle = true});
}

DEVEX_REFLECT(Environment)
{
    type.field("sky", &Environment::sky, {.assetType = "texture"})
        .field("color", &Environment::color, {.color = true})
        .field("intensity", &Environment::intensity)
        .field("rotation", &Environment::rotation, {.angle = true});
}

} // namespace devex::scene
