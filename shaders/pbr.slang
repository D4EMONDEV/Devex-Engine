// Physically based shading: GGX microfacets with height-correlated Smith visibility, Schlick
// Fresnel and Lambertian diffuse, lit by the sun, clustered local lights and the environment.
module pbr;

import common;

public struct Surface
{
    public float3 position;
    public float3 normal;
    // Towards the viewer.
    public float3 view;
    public float3 diffuseColor;
    public float3 f0;
    // Perceptual roughness, and its square used by the distribution.
    public float roughness;
    public float alpha;
    public float occlusion;
    public float viewDistance;
    public float2 fragmentCoordinate;
};

float distributionGgx(float NoH, float alpha)
{
    const float a2 = alpha * alpha;
    const float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (pi * f * f);
}

float visibilitySmithGgxCorrelated(float NoV, float NoL, float alpha)
{
    const float a2 = alpha * alpha;
    const float lambdaV = NoL * sqrt((NoV - NoV * a2) * NoV + a2);
    const float lambdaL = NoV * sqrt((NoL - NoL * a2) * NoL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

float3 fresnelSchlick(float3 f0, float VoH)
{
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Reflected luminance for light arriving from `toLight` with an illuminance at normal incidence.
float3 shadeLight(Surface surface, float3 toLight, float3 illuminance)
{
    const float NoL = saturate(dot(surface.normal, toLight));
    if (NoL <= 0.0)
    {
        return float3(0.0);
    }
    const float3 halfway = normalize(surface.view + toLight);
    const float NoV = abs(dot(surface.normal, surface.view)) + 1e-5;
    const float NoH = saturate(dot(surface.normal, halfway));
    const float VoH = saturate(dot(surface.view, halfway));

    const float3 fresnel = fresnelSchlick(surface.f0, VoH);
    const float3 specular = distributionGgx(NoH, surface.alpha) *
                            visibilitySmithGgxCorrelated(NoV, NoL, surface.alpha) * fresnel;
    const float3 diffuse = surface.diffuseColor / pi;
    return (diffuse + specular) * illuminance * NoL;
}

float cascadeShadow(SceneData* scene, Surface surface, uint cascade)
{
    // Moving the point along its normal by a texel keeps surfaces from shadowing themselves.
    const float texelSize = scene.cascadeTexelSizes[cascade];
    const float3 offsetPosition = surface.position + surface.normal * texelSize * 1.5;
    const float4 clip = mul(scene.cascadeViewProjections[cascade], float4(offsetPosition, 1.0));
    const float2 uv = clip.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || clip.z > 1.0)
    {
        return 1.0;
    }

    // 3x3 bilinear comparisons.
    float width;
    float height;
    float layers;
    shadowMap.GetDimensions(width, height, layers);
    const float2 texel = 1.0 / float2(width, height);
    const float reference = clip.z - 0.0005;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            lit += shadowMap.SampleCmpLevelZero(shadowSampler,
                                                float3(uv + float2(x, y) * texel, float(cascade)),
                                                reference);
        }
    }
    return lit / 9.0;
}

float sunShadow(SceneData* scene, Surface surface)
{
    if ((scene.sunFlags & sunCastsShadows) == 0 || surface.viewDistance >= scene.shadowDistance)
    {
        return 1.0;
    }
    for (uint cascade = 0; cascade < cascadeCount; ++cascade)
    {
        if (surface.viewDistance < scene.cascadeSplits[cascade])
        {
            const float shadow = cascadeShadow(scene, surface, cascade);
            // Shadows fade out over the last tenth of their distance.
            const float fade = saturate((scene.shadowDistance - surface.viewDistance) /
                                        (0.1 * scene.shadowDistance));
            return lerp(1.0, shadow, fade);
        }
    }
    return 1.0;
}

// The face of the cube around a point light that a direction falls in, in the order the renderer
// writes them: +X, -X, +Y, -Y, +Z, -Z.
uint cubeFace(float3 direction)
{
    const float3 magnitude = abs(direction);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        return direction.x > 0.0 ? 0 : 1;
    }
    if (magnitude.y >= magnitude.z)
    {
        return direction.y > 0.0 ? 2 : 3;
    }
    return direction.z > 0.0 ? 4 : 5;
}

// How much of a local light reaches the surface: 1 in the open, 0 behind something it lights.
float localShadow(SceneData* scene, Surface surface, Light light, float3 toSurface)
{
    if (light.firstShadowView < 0)
    {
        return 1.0;
    }
    // A point light looks at the surface through the face of its cube that holds it.
    const uint view = uint(light.firstShadowView) +
                      (light.spotScale == 0.0 ? cubeFace(toSurface) : 0);
    const ShadowView shadow = scene.shadowViews[view];

    // Moved along the normal, so that a surface does not shadow itself.
    const float3 offsetPosition = surface.position + surface.normal * light.shadowFar * 0.004;
    const float4 clip = mul(shadow.viewProjection, float4(offsetPosition, 1.0));
    if (clip.w <= 0.0)
    {
        return 1.0;
    }
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = projected.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || projected.z > 1.0)
    {
        return 1.0;
    }

    float width;
    float height;
    localShadowMap.GetDimensions(width, height);
    const float2 atlasTexel = 1.0 / float2(width, height);
    const float reference = projected.z - 0.0015;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            // Every tap stays inside the tile, so that a shadow never leaks into its neighbour.
            const float2 tileUv = clamp(uv + float2(x, y) * atlasTexel / max(shadow.tile.z, 1e-6),
                                        0.001, 0.999);
            lit += localShadowMap.SampleCmpLevelZero(
                shadowSampler, shadow.tile.xy + tileUv * shadow.tile.zw, reference);
        }
    }
    return lit / 9.0;
}

float3 localLights(SceneData* scene, Surface surface)
{
    if (scene.lightCount == 0)
    {
        return float3(0.0);
    }
    const uint tileX = min(uint(surface.fragmentCoordinate.x / scene.viewportWidth * scene.clusterCountX),
                           scene.clusterCountX - 1);
    const uint tileY = min(uint(surface.fragmentCoordinate.y / scene.viewportHeight * scene.clusterCountY),
                           scene.clusterCountY - 1);
    const float slice = floor(log2(max(surface.viewDistance, 1e-4)) * scene.clusterSliceScale +
                              scene.clusterSliceBias);
    const uint sliceIndex = uint(clamp(slice, 0.0, float(scene.clusterCountZ - 1)));
    const Cluster cluster =
        scene.clusters[(sliceIndex * scene.clusterCountY + tileY) * scene.clusterCountX + tileX];

    float3 luminance = float3(0.0);
    for (uint entry = 0; entry < cluster.count; ++entry)
    {
        const Light light = scene.lights[scene.clusterLights[cluster.offset + entry]];
        const float3 toLight = light.position - surface.position;
        const float distanceSquared = dot(toLight, toLight);
        const float3 direction = toLight * rsqrt(max(distanceSquared, 1e-8));

        // Inverse square falloff, windowed so that the light ends exactly at its range.
        const float ratio = distanceSquared / (light.range * light.range);
        const float window = saturate(1.0 - ratio * ratio);
        float attenuation = window * window / max(distanceSquared, 1e-4);
        const float spot = saturate(dot(light.direction, -direction) * light.spotScale + light.spotOffset);
        attenuation *= spot * spot;

        if (attenuation <= 0.0)
        {
            continue;
        }
        attenuation *= localShadow(scene, surface, light, -toLight);
        luminance += shadeLight(surface, direction, light.intensity * attenuation);
    }
    return luminance;
}

float3 environmentLight(SceneData* scene, Surface surface)
{
    const float NoV = saturate(dot(surface.normal, surface.view));
    const float3 reflected = reflect(-surface.view, surface.normal);
    const float angle = -scene.environmentRotation;

    const float3 irradiance =
        irradianceEnvironment.SampleLevel(clampSampler, rotateAroundY(surface.normal, angle), 0.0).rgb;
    const float lod = surface.roughness * (scene.specularMipCount - 1.0);
    const float3 prefiltered =
        specularEnvironment.SampleLevel(clampSampler, rotateAroundY(reflected, angle), lod).rgb;
    const float2 brdf = brdfLut.SampleLevel(clampSampler, float2(NoV, surface.roughness), 0.0).rg;

    const float3 diffuse = surface.diffuseColor * irradiance;
    const float3 specular = prefiltered * (surface.f0 * brdf.x + brdf.y);
    return (diffuse + specular) * surface.occlusion * scene.environmentColor * scene.environmentIntensity;
}

// Luminance leaving the surface towards the viewer, before exposure.
public float3 shadeSurface(SceneData* scene, Surface surface)
{
    float3 luminance = environmentLight(scene, surface) + localLights(scene, surface);
    if ((scene.sunFlags & sunEnabled) != 0)
    {
        luminance += shadeLight(surface, -scene.sunDirection, scene.sunIlluminance) *
                     sunShadow(scene, surface);
    }
    return luminance;
}
