#include <devex/scene/Components.hpp>

#include <algorithm>
#include <cmath>

namespace devex::scene {

DEVEX_REFLECT(Transform)
{
    type.field("position", &Transform::position, {.group = "Local Transform"})
        .field("rotation", &Transform::rotation)
        .field("scale", &Transform::scale);
}

DEVEX_REFLECT(MeshRenderer)
{
    type.field("mesh", &MeshRenderer::mesh, {.assetType = "mesh", .group = "Geometry"})
        .field("material", &MeshRenderer::material, {.assetType = "material"})
        .field("instance_shader_parameters", &MeshRenderer::instanceShaderParameters,
               {.hidden = true})
        .field("instance_shader_values", &MeshRenderer::instanceShaderValues, {.hidden = true})
        .field("lod_bias", &MeshRenderer::lodBias, {.group = "Level of Detail"});
}

DEVEX_REFLECT(Camera)
{
    type.field("projection", &Camera::projection, {.group = "Projection"})
        .field("vertical_fov", &Camera::verticalFov, {.angle = true})
        .field("orthographic_size", &Camera::orthographicSize)
        .field("near_plane", &Camera::nearPlane)
        .field("far_plane", &Camera::farPlane)
        .field("primary", &Camera::primary, {.group = "Camera"})
        .field("auto_exposure", &Camera::autoExposure, {.group = "Exposure"})
        .field("ev100", &Camera::ev100)
        .field("exposure_compensation", &Camera::exposureCompensation)
        .field("min_ev100", &Camera::minEv100)
        .field("max_ev100", &Camera::maxEv100)
        .field("adaptation_speed", &Camera::adaptationSpeed)
        .field("tonemapper", &Camera::tonemapper, {.group = "Tonemapping"})
        .field("antialiasing", &Camera::antialiasing, {.group = "Antialiasing"})
        .field("ambient_occlusion", &Camera::ambientOcclusion, {.group = "Ambient Occlusion"})
        .field("ambient_occlusion_radius", &Camera::ambientOcclusionRadius)
        .field("screen_reflections", &Camera::screenReflections, {.group = "Screen Reflections"})
        .field("screen_reflection_steps", &Camera::screenReflectionSteps)
        .field("screen_reflection_fade_in", &Camera::screenReflectionFadeIn)
        .field("screen_reflection_fade_out", &Camera::screenReflectionFadeOut)
        .field("screen_reflection_depth_tolerance", &Camera::screenReflectionTolerance)
        .field("bloom", &Camera::bloom, {.group = "Bloom"})
        .field("bloom_threshold", &Camera::bloomThreshold)
        .field("vignette", &Camera::vignette, {.group = "Vignette"})
        .field("grain", &Camera::grain, {.group = "Grain"})
        .field("chromatic_aberration", &Camera::chromaticAberration,
               {.group = "Chromatic Aberration"})
        .field("color_table", &Camera::colorTable, {.assetType = "texture", .group = "Color Table"})
        .field("depth_of_field", &Camera::depthOfField, {.group = "Lens"})
        .field("focus_distance", &Camera::focusDistance)
        .field("aperture", &Camera::aperture)
        .field("auto_focus", &Camera::autoFocus)
        .field("motion_blur", &Camera::motionBlur, {.group = "Motion Blur"})
        .field("saturation", &Camera::saturation, {.group = "Color grading"})
        .field("contrast", &Camera::contrast)
        .field("color_filter", &Camera::colorFilter, {.color = true})
        .field("temperature", &Camera::temperature)
        .field("tint", &Camera::tint);
}

DEVEX_REFLECT(DirectionalLight)
{
    type.field("color", &DirectionalLight::color, {.color = true, .group = "Light"})
        .field("temperature", &DirectionalLight::temperature)
        .field("illuminance", &DirectionalLight::illuminance)
        .field("cast_shadows", &DirectionalLight::castShadows, {.group = "Shadows"})
        .field("shadow_distance", &DirectionalLight::shadowDistance)
        .field("follow_atmosphere", &DirectionalLight::followAtmosphere, {.group = "Atmosphere"});
}

DEVEX_REFLECT(PointLight)
{
    type.field("color", &PointLight::color, {.color = true, .group = "Light"})
        .field("temperature", &PointLight::temperature)
        .field("intensity", &PointLight::intensity)
        .field("range", &PointLight::range)
        .field("cast_shadows", &PointLight::castShadows, {.group = "Shadows"});
}

DEVEX_REFLECT(SpotLight)
{
    type.field("color", &SpotLight::color, {.color = true, .group = "Light"})
        .field("temperature", &SpotLight::temperature)
        .field("intensity", &SpotLight::intensity)
        .field("range", &SpotLight::range)
        .field("inner_angle", &SpotLight::innerAngle, {.angle = true, .group = "Spot"})
        .field("outer_angle", &SpotLight::outerAngle, {.angle = true})
        .field("cast_shadows", &SpotLight::castShadows, {.group = "Shadows"});
}

DEVEX_REFLECT(Environment)
{
    type.field("sky", &Environment::sky, {.assetType = "texture", .group = "Sky"})
        .field("sky_material", &Environment::skyMaterial, {.assetType = "material"})
        .field("color", &Environment::color, {.color = true, .group = "Appearance"})
        .field("intensity", &Environment::intensity)
        .field("rotation", &Environment::rotation, {.angle = true, .group = "Orientation"});
}

DEVEX_REFLECT(Atmosphere)
{
    type.field("planet_radius", &Atmosphere::planetRadius, {.group = "Planet"})
        .field("height", &Atmosphere::height)
        .field("rayleigh_scattering", &Atmosphere::rayleighScattering,
               {.group = "Rayleigh Scattering"})
        .field("rayleigh_height", &Atmosphere::rayleighHeight)
        .field("mie_scattering", &Atmosphere::mieScattering, {.group = "Mie Scattering"})
        .field("mie_absorption", &Atmosphere::mieAbsorption)
        .field("mie_anisotropy", &Atmosphere::mieAnisotropy)
        .field("mie_height", &Atmosphere::mieHeight)
        .field("ozone_absorption", &Atmosphere::ozoneAbsorption, {.group = "Ozone"})
        .field("ground_albedo", &Atmosphere::groundAlbedo, {.color = true, .group = "Ground"})
        .field("sun_disk_size", &Atmosphere::sunDiskSize, {.angle = true, .group = "Sun Disk"})
        .field("sun_disk_intensity", &Atmosphere::sunDiskIntensity)
        .field("aerial_perspective", &Atmosphere::aerialPerspective, {.group = "Rendering"})
        .field("intensity", &Atmosphere::intensity);
}

DEVEX_REFLECT(Fog)
{
    type.field("color", &Fog::color, {.color = true, .group = "Fog"})
        .field("density", &Fog::density)
        .field("height", &Fog::height, {.group = "Height"})
        .field("falloff", &Fog::falloff)
        .field("start_distance", &Fog::startDistance, {.group = "Distance"})
        .field("max_opacity", &Fog::maxOpacity)
        .field("sun_scattering", &Fog::sunScattering, {.group = "Sun Scattering"})
        .field("sun_anisotropy", &Fog::sunAnisotropy)
        .field("sky_blend", &Fog::skyBlend, {.group = "Sky"});
}

DEVEX_REFLECT(Decal)
{
    type.field("size", &Decal::size, {.group = "Decal"})
        .field("albedo", &Decal::albedo, {.assetType = "texture", .group = "Textures"})
        .field("normal", &Decal::normal, {.assetType = "texture"})
        .field("orm", &Decal::orm, {.assetType = "texture"})
        .field("emission", &Decal::emission, {.assetType = "texture"})
        .field("modulate", &Decal::modulate, {.color = true, .group = "Parameters"})
        .field("albedo_mix", &Decal::albedoMix)
        .field("emission_energy", &Decal::emissionEnergy)
        .field("normal_fade", &Decal::normalFade)
        .field("upper_fade", &Decal::upperFade, {.group = "Vertical Fade"})
        .field("lower_fade", &Decal::lowerFade)
        .field("distance_fade", &Decal::distanceFade, {.group = "Distance Fade"})
        .field("distance_fade_begin", &Decal::distanceFadeBegin)
        .field("distance_fade_length", &Decal::distanceFadeLength);
}

DEVEX_REFLECT(VolumetricFog)
{
    type.field("density", &VolumetricFog::density, {.group = "Fog"})
        .field("albedo", &VolumetricFog::albedo, {.color = true})
        .field("emission", &VolumetricFog::emission, {.color = true})
        .field("emission_energy", &VolumetricFog::emissionEnergy)
        .field("anisotropy", &VolumetricFog::anisotropy)
        .field("length", &VolumetricFog::length, {.group = "Extent"})
        .field("detail_spread", &VolumetricFog::detailSpread)
        .field("ambient_inject", &VolumetricFog::ambientInject, {.group = "Light"})
        .field("sky_affect", &VolumetricFog::skyAffect, {.group = "Sky"})
        .field("temporal_reprojection", &VolumetricFog::temporalReprojection, {.group = "Temporal Reprojection"})
        .field("temporal_amount", &VolumetricFog::temporalAmount);
}

DEVEX_REFLECT(FogVolume)
{
    type.field("shape", &FogVolume::shape, {.group = "Volume"})
        .field("size", &FogVolume::size)
        .field("density", &FogVolume::density, {.group = "Fog"})
        .field("albedo", &FogVolume::albedo, {.color = true})
        .field("emission", &FogVolume::emission, {.color = true})
        .field("height_falloff", &FogVolume::heightFalloff)
        .field("edge_fade", &FogVolume::edgeFade);
}

DEVEX_REFLECT(PostProcessVolume)
{
    type.field("global", &PostProcessVolume::global, {.group = "Volume"})
        .field("size", &PostProcessVolume::size)
        .field("blend_distance", &PostProcessVolume::blendDistance)
        .field("weight", &PostProcessVolume::weight)
        .field("priority", &PostProcessVolume::priority)
        .field("override_exposure", &PostProcessVolume::overrideExposure, {.group = "Exposure"})
        .field("exposure_compensation", &PostProcessVolume::exposureCompensation)
        .field("override_bloom", &PostProcessVolume::overrideBloom, {.group = "Bloom"})
        .field("bloom", &PostProcessVolume::bloom)
        .field("bloom_threshold", &PostProcessVolume::bloomThreshold)
        .field("override_vignette", &PostProcessVolume::overrideVignette, {.group = "Vignette"})
        .field("vignette", &PostProcessVolume::vignette)
        .field("override_grain", &PostProcessVolume::overrideGrain, {.group = "Grain"})
        .field("grain", &PostProcessVolume::grain)
        .field("override_chromatic_aberration", &PostProcessVolume::overrideChromaticAberration,
               {.group = "Chromatic aberration"})
        .field("chromatic_aberration", &PostProcessVolume::chromaticAberration)
        .field("override_ambient_occlusion", &PostProcessVolume::overrideAmbientOcclusion,
               {.group = "Ambient occlusion"})
        .field("ambient_occlusion", &PostProcessVolume::ambientOcclusion)
        .field("override_screen_reflections", &PostProcessVolume::overrideScreenReflections,
               {.group = "Screen reflections"})
        .field("screen_reflections", &PostProcessVolume::screenReflections)
        .field("override_depth_of_field", &PostProcessVolume::overrideDepthOfField,
               {.group = "Depth of field"})
        .field("depth_of_field", &PostProcessVolume::depthOfField)
        .field("focus_distance", &PostProcessVolume::focusDistance)
        .field("aperture", &PostProcessVolume::aperture)
        .field("auto_focus", &PostProcessVolume::autoFocus)
        .field("override_motion_blur", &PostProcessVolume::overrideMotionBlur,
               {.group = "Motion blur"})
        .field("motion_blur", &PostProcessVolume::motionBlur)
        .field("override_saturation", &PostProcessVolume::overrideSaturation,
               {.group = "Color grading"})
        .field("saturation", &PostProcessVolume::saturation)
        .field("override_contrast", &PostProcessVolume::overrideContrast)
        .field("contrast", &PostProcessVolume::contrast)
        .field("override_color_filter", &PostProcessVolume::overrideColorFilter)
        .field("color_filter", &PostProcessVolume::colorFilter, {.color = true})
        .field("override_white_balance", &PostProcessVolume::overrideWhiteBalance)
        .field("temperature", &PostProcessVolume::temperature)
        .field("tint", &PostProcessVolume::tint);
}

DEVEX_REFLECT(Terrain)
{
    type.field("data", &Terrain::data, {.assetType = "terrain", .group = "Terrain"})
        .field("size", &Terrain::size)
        .field("collision", &Terrain::collision)
        .field("cast_shadows", &Terrain::castShadows)
        .field("layers", &Terrain::layers, {.assetType = "material", .group = "Layers"})
        .field("texture_size", &Terrain::textureSize)
        .field("blend_sharpness", &Terrain::blendSharpness)
        .field("triplanar", &Terrain::triplanar)
        .field("auto_base_layer", &Terrain::autoBaseLayer, {.group = "Automatic layers"})
        .field("auto_slope_layer", &Terrain::autoSlopeLayer)
        .field("auto_slope", &Terrain::autoSlope, {.angle = true})
        .field("auto_height_layer", &Terrain::autoHeightLayer)
        .field("auto_height", &Terrain::autoHeight)
        .field("auto_blend", &Terrain::autoBlend)
        .field("grass_color", &Terrain::grassColor, {.color = true, .group = "Grass"})
        .field("grass_tip_color", &Terrain::grassTipColor, {.color = true})
        .field("grass_height", &Terrain::grassHeight)
        .field("grass_width", &Terrain::grassWidth)
        .field("grass_density", &Terrain::grassDensity)
        .field("grass_distance", &Terrain::grassDistance)
        .field("wind_strength", &Terrain::windStrength, {.group = "Wind"})
        .field("wind_speed", &Terrain::windSpeed);
}

DEVEX_REFLECT(ReflectionProbe)
{
    type.field("size", &ReflectionProbe::size, {.group = "Bounds"})
        .field("capture_offset", &ReflectionProbe::captureOffset)
        .field("blend_distance", &ReflectionProbe::blendDistance)
        .field("intensity", &ReflectionProbe::intensity, {.group = "Reflection"})
        .field("box_projection", &ReflectionProbe::boxProjection)
        .field("resolution", &ReflectionProbe::resolution, {.group = "Capture"})
        .field("capture", &ReflectionProbe::capture, {.assetType = "reflection_probe"});
}

DEVEX_REFLECT(LightProbeVolume)
{
    type.field("size", &LightProbeVolume::size, {.group = "Grid"})
        .field("spacing", &LightProbeVolume::spacing)
        .field("blend_distance", &LightProbeVolume::blendDistance, {.group = "Lighting"})
        .field("intensity", &LightProbeVolume::intensity)
        .field("bounces", &LightProbeVolume::bounces, {.group = "Capture"})
        .field("irradiance", &LightProbeVolume::irradiance, {.assetType = "irradiance_volume"});
}

std::array<std::uint32_t, 3> probeCounts(const LightProbeVolume& volume) noexcept
{
    const float spacing = std::max(volume.spacing, 0.05f);
    std::array<std::uint32_t, 3> counts{};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float probes = std::round(std::max(volume.size[axis], 0.0f) / spacing) + 1.0f;
        counts[static_cast<std::size_t>(axis)] = static_cast<std::uint32_t>(std::clamp(probes, 2.0f, 64.0f));
    }
    return counts;
}

} // namespace devex::scene
