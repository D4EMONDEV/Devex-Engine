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

// What a light gives a surface. The physical one is the default; the light() of a shader of the
// project replaces it.
public interface ILightModel
{
    // The luminance a light adds: it comes from `toLight`, with `color` the illuminance it brings at
    // normal incidence, of which `attenuation` reaches the surface past its range, its cone and the
    // shadows.
    float3 shade(Surface surface, float3 toLight, float3 color, float attenuation, bool directional);
};

public struct PhysicalLight : ILightModel
{
    public float3 shade(Surface surface, float3 toLight, float3 color, float attenuation, bool directional)
    {
        return shadeLight(surface, toLight, color * attenuation);
    }
};

float3 localLights<Model : ILightModel>(SceneData* scene, Surface surface, Model model)
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
        luminance += model.shade(surface, direction, light.intensity, attenuation, false);
    }
    return luminance;
}

// What the reflection probes around a surface reflect towards a direction, in nits, and how much of
// the reflection they cover, from 0 to 1. The smallest box comes first; each fades out over its blend
// distance, and lets what it leaves to the next ones and to the sky.
public float4 probeReflection(SceneData* scene, float3 position, float3 reflected, float lod)
{
    float3 light = float3(0.0);
    float covered = 0.0;
    // The loop ends with the list, the same for every pixel: lavapipe samples the maps wrongly in a
    // loop that each pixel leaves at its own time.
    for (uint index = 0; index < scene.reflectionProbeCount; ++index)
    {
        const ReflectionProbe probe = scene.reflectionProbes[index];
        const float3 local = mul(probe.worldToBox, float4(position, 1.0)).xyz;
        const float3 inside = probe.halfSize - abs(local);
        const float edge = min(inside.x, min(inside.y, inside.z));
        if (edge <= 0.0 || covered >= 0.999)
        {
            continue;
        }
        const float weight = probe.blendDistance > 0.0 ? saturate(edge / probe.blendDistance) : 1.0;
        float3 direction = reflected;
        if (probe.boxProjection != 0)
        {
            // Where the reflected ray leaves the box, seen from where the probe captured it.
            const float3x3 rotation = (float3x3)probe.worldToBox;
            const float3 localDirection = mul(rotation, reflected);
            const float3 safe = select(abs(localDirection) < 1e-5, float3(1e-5), localDirection);
            const float3 far = max((probe.halfSize - local) / safe, (-probe.halfSize - local) / safe);
            const float distance = min(far.x, min(far.y, far.z));
            direction = mul(transpose(rotation), local + localDirection * distance - probe.capturePosition);
        }
        const float3 sampled =
            reflectionProbeMaps[NonUniformResourceIndex(probe.map)].SampleLevel(clampSampler, direction, lod).rgb;
        const float share = (1.0 - covered) * weight;
        light += sampled * probe.intensity * share;
        covered += share;
    }
    return float4(light, covered);
}

// What the harmonics of a probe of a volume give a surface facing a direction of unit length.
float3 probeIrradiance(float4* probe, float3 n)
{
    float c[28];
    for (uint group = 0; group < 7; ++group)
    {
        const float4 values = probe[group];
        c[group * 4] = values.x;
        c[group * 4 + 1] = values.y;
        c[group * 4 + 2] = values.z;
        c[group * 4 + 3] = values.w;
    }
    const float basis[9] = {0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
                            1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
                            0.546274 * (n.x * n.x - n.y * n.y)};
    float3 light = float3(0.0);
    for (uint index = 0; index < 9; ++index)
    {
        light += float3(c[index * 3], c[index * 3 + 1], c[index * 3 + 2]) * basis[index];
    }
    return max(light, float3(0.0));
}

// The light the irradiance volumes around a surface give it, divided by pi as the irradiance of the
// sky is, and how much of it they cover, from 0 to 1. Each volume mixes the eight probes around the
// surface, read a little off it along its normal, and gives less weight to those behind it, which may
// stand on the other side of a wall.
public float4 volumeIrradiance(SceneData* scene, float3 position, float3 normal)
{
    float3 light = float3(0.0);
    float covered = 0.0;
    // The loop ends with the list, the same for every pixel, as that of the reflection probes.
    for (uint index = 0; index < scene.irradianceVolumeCount; ++index)
    {
        const IrradianceVolume volume = scene.irradianceVolumes[index];
        const float3 local = mul(volume.worldToBox, float4(position, 1.0)).xyz;
        const float3 inside = volume.halfSize - abs(local);
        const float edge = min(inside.x, min(inside.y, inside.z));
        if (edge <= 0.0 || covered >= 0.999)
        {
            continue;
        }
        const float weight = volume.blendDistance > 0.0 ? saturate(edge / volume.blendDistance) : 1.0;
        const float3 counts = float3(float(volume.countX), float(volume.countY), float(volume.countZ));
        const float3 cell = volume.halfSize * 2.0 / (counts - 1.0);
        const float3 localNormal = mul((float3x3)volume.worldToBox, normal);
        const float3 moved = local + localNormal * volume.normalBias * min(cell.x, min(cell.y, cell.z));
        const float3 grid = clamp((moved + volume.halfSize) / cell, float3(0.0), counts - 1.0);
        const uint3 base = min(uint3(floor(grid)), uint3(volume.countX, volume.countY, volume.countZ) - 2);
        const float3 fraction = grid - float3(base);
        float3 sum = float3(0.0);
        float total = 0.0;
        for (uint corner = 0; corner < 8; ++corner)
        {
            const uint3 offset = uint3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
            const uint3 at = base + offset;
            const float3 trilinear = select(offset == uint3(1), fraction, 1.0 - fraction);
            const float3 toProbe = (-volume.halfSize + float3(at) * cell) - local;
            const float distance = length(toProbe);
            const float facing = distance > 1e-4 ? (dot(toProbe / distance, localNormal) + 1.0) * 0.5 : 1.0;
            const float probeWeight = trilinear.x * trilinear.y * trilinear.z * (facing * facing + 0.05);
            const uint probe = at.x + volume.countX * (at.y + volume.countY * at.z);
            sum += probeIrradiance(volume.probes + probe * 7, normal) * probeWeight;
            total += probeWeight;
        }
        const float share = (1.0 - covered) * weight;
        light += (total > 0.0 ? sum / total : float3(0.0)) * volume.intensity * share;
        covered += share;
    }
    return float4(light, covered);
}

float luminanceOf(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// The light of the sky, ambient and reflected; the reflection probes around the surface take the
// place of the sky in its reflections, and the irradiance volumes in its ambient light.
// Surfaces this rough reflect nothing of the screen; those a little smoother fade towards it.
static const float screenReflectionRoughness = 0.7;
// How far a ray goes in meters: enough to leave the screen, or to reach what the view can show.
static const float screenReflectionReach = 2000.0;
// How close a ray may come to the camera, where the projection ends.
static const float screenReflectionNearest = 0.01;
// How far from the side of the screen a reflection starts fading out, in texture coordinates.
static const float screenReflectionEdge = 0.08;

// What the screen shows along a ray from a surface: the first thing it meets in the depth of the
// scene, as the previous frame drew it, and how much of the reflection that is. A depth is a straight
// line across the screen, whatever the perspective, so a ray is a segment of the texture coordinates
// and of the depth, walked with steps wider as they go: contacts keep their detail, and the ray still
// crosses the screen.
float4 screenReflection(SceneData* scene, Surface surface, float3 direction)
{
    if (scene.screenReflectionSteps == 0 || surface.roughness >= screenReflectionRoughness)
    {
        return float4(0.0);
    }
    const float4 start = mul(scene.viewProjection, float4(surface.position, 1.0));
    float4 end = mul(scene.viewProjection, float4(surface.position + direction * screenReflectionReach, 1.0));
    if (end.w < screenReflectionNearest)
    {
        end = lerp(start, end, (start.w - screenReflectionNearest) / (start.w - end.w));
    }
    const float3 from = float3(start.xy / start.w * 0.5 + 0.5, start.z / start.w);
    float3 to = float3(end.xy / end.w * 0.5 + 0.5, end.z / end.w);

    // As far as the side of the screen it goes through.
    const float2 across = to.xy - from.xy;
    const float2 sides = float2(across.x > 0.0 ? 1.0 - from.x : from.x, across.y > 0.0 ? 1.0 - from.y : from.y);
    const float2 kept = sides / max(abs(across), float2(1e-6));
    to = lerp(from, to, saturate(min(kept.x, kept.y)));

    const float2 size = float2(scene.viewportWidth, scene.viewportHeight);
    const int2 last = int2(size) - 1;
    const float pixels = length((to.xy - from.xy) * size);
    if (pixels < 2.0)
    {
        return float4(0.0);
    }
    const uint count = min(scene.screenReflectionSteps, uint(pixels));
    // A different start for every pixel and frame, which the antialiasing averages.
    const float2 noisy = floor(from.xy * size) + 5.588238 * float(scene.screenReflectionFrame % 64);
    const float jitter = frac(52.9829189 * frac(dot(noisy, float2(0.06711056, 0.00583715))));

    float before = 0.0;
    for (uint step = 0; step < count; ++step)
    {
        const float walked = (float(step) + jitter + 0.5) / float(count);
        const float along = max(walked * sqrt(walked), 1.5 / pixels);
        const float3 point = lerp(from, to, along);
        const float depth = sceneDepth.Load(int3(min(int2(point.xy * size), last), 0)).r;
        // With reversed depth, what lies nearer than the ray stands between it and the camera. The
        // margin keeps a surface from meeting itself.
        if (depth <= point.z * 1.0005)
        {
            before = along;
            continue;
        }

        // Where it crossed, between this step and the last.
        float low = before;
        float high = along;
        for (uint halving = 0; halving < 4; ++halving)
        {
            const float middle = (low + high) * 0.5;
            const float3 tried = lerp(from, to, middle);
            if (sceneDepth.Load(int3(min(int2(tried.xy * size), last), 0)).r > tried.z)
            {
                high = middle;
            }
            else
            {
                low = middle;
            }
        }
        const float3 hit = lerp(from, to, high);
        const int2 pixel = min(int2(hit.xy * size), last);
        const float hitDepth = sceneDepth.Load(int3(pixel, 0)).r;
        const float3 world = worldFromSceneDepth(scene, hit.xy, hitDepth);
        const float behind = length(worldFromSceneDepth(scene, hit.xy, hit.z) - scene.cameraPosition) - length(world - scene.cameraPosition);
        // The ray passed behind what it crossed, or came at it from behind: it goes on.
        const float3 facing = decodeNormal(normalTexture.Load(int3(pixel, 0)).rg);
        if (behind > scene.screenReflectionTolerance || dot(facing, direction) > 0.0)
        {
            before = along;
            continue;
        }

        const float4 previous = mul(scene.previousViewProjection, float4(world, 1.0));
        const float2 uv = previous.xy / max(previous.w, 1e-6) * 0.5 + 0.5;
        if (previous.w <= 0.0 || any(uv < 0.0) || any(uv > 1.0))
        {
            return float4(0.0);
        }
        // As wide as the lobe of the surface has grown on its way, in pixels of the first level.
        const float wide = surface.roughness * surface.roughness * high * pixels * 0.5;
        const float level = min(log2(1.0 + wide), float(scene.screenReflectionLevels - 1));
        const float3 color = screenReflectionImage.SampleLevel(clampSampler, uv, level).rgb / scene.screenReflectionExposure;

        const float2 margin = min(hit.xy, 1.0 - hit.xy) / screenReflectionEdge;
        const float grown = (float(step) + 1.0) / float(count);
        const float fade = pow(grown, scene.screenReflectionFadeIn) * pow(1.0 - grown, scene.screenReflectionFadeOut);
        const float smooth = 1.0 - smoothstep(screenReflectionRoughness - 0.2, screenReflectionRoughness, surface.roughness);
        return float4(color, scene.screenReflectionIntensity * smoothstep(0.0, 1.0, saturate(min(margin.x, margin.y))) * fade * smooth);
    }
    return float4(0.0);
}

// `screen` is what the screen reflects there, with how much of the reflection it is.
public float3 environmentLight(SceneData* scene, Surface surface, float4 screen)
{
    const float NoV = saturate(dot(surface.normal, surface.view));
    const float3 reflected = reflect(-surface.view, surface.normal);
    const float angle = -scene.environmentRotation;
    const float3 sky = scene.environmentColor * scene.environmentIntensity;

    const float3 skyIrradiance =
        irradianceEnvironment.SampleLevel(clampSampler, rotateAroundY(surface.normal, angle), 0.0).rgb * sky;
    const float4 volumes = volumeIrradiance(scene, surface.position, surface.normal);
    const float3 irradiance = volumes.rgb + skyIrradiance * (1.0 - volumes.a);
    const float lod = surface.roughness * (scene.specularMipCount - 1.0);
    const float4 probes = probeReflection(scene, surface.position, reflected, lod);
    float3 prefiltered = probes.rgb;
    if (probes.a < 0.999)
    {
        // Where a volume darkens the ambient light and no probe reflects the room, the sky it still
        // reflects darkens as much: a closed room does not mirror a bright sky.
        const float darkened = volumes.a > 0.0 ? saturate(luminanceOf(irradiance) / max(luminanceOf(skyIrradiance), 1e-4)) : 1.0;
        prefiltered += specularEnvironment.SampleLevel(clampSampler, rotateAroundY(reflected, angle), lod).rgb * sky *
                       (1.0 - probes.a) * darkened;
    }
    // What the screen shows takes the place of what the probes and the sky would reflect.
    prefiltered = lerp(prefiltered, screen.rgb, screen.a);
    const float2 brdf = brdfLut.SampleLevel(clampSampler, float2(NoV, surface.roughness), 0.0).rg;

    const float3 diffuse = surface.diffuseColor * irradiance;
    const float3 specular = prefiltered * (surface.f0 * brdf.x + brdf.y);
    return (diffuse + specular) * surface.occlusion;
}

// The light around a surface, with what the screen reflects there.
public float3 environmentLight(SceneData* scene, Surface surface)
{
    return environmentLight(scene, surface, screenReflection(scene, surface, reflect(-surface.view, surface.normal)));
}

// The luminance the sun and the local lights give the surface through a light model.
public float3 shadeLights<Model : ILightModel>(SceneData* scene, Surface surface, Model model)
{
    float3 luminance = localLights(scene, surface, model);
    if ((scene.sunFlags & sunEnabled) != 0)
    {
        luminance += model.shade(surface, -scene.sunDirection, scene.sunIlluminance, sunShadow(scene, surface), true);
    }
    return luminance;
}

// Luminance leaving the surface towards the viewer, before exposure.
public float3 shadeSurface(SceneData* scene, Surface surface)
{
    return environmentLight(scene, surface) + shadeLights(scene, surface, PhysicalLight());
}

// The same without reflecting the screen, for what writes the depth as it draws and cannot read it.
public float3 shadeSurfaceOffScreen(SceneData* scene, Surface surface)
{
    return environmentLight(scene, surface, float4(0.0)) + shadeLights(scene, surface, PhysicalLight());
}
