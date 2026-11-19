// The scene of the OpenGL renderer, translated into GLSL with the shaders that import it, as webgpu_scene.slang is for
// WebGPU: what a frame binds, the light the sun, the local lights and the environment give a surface, with their
// shadows, and the height fog. OpenGL ES 3.0 has no storage buffers: the lights and the views of the shadow atlas lie in
// uniform blocks, at most 32 lights a frame and 8 an object, as Godot's Compatibility has them. The matrices of the
// shadows read here are those of Vulkan's clip space, Y down, which OpenGL drew its maps with, from their bottom row.
module gl_scene;

import surface_shading;

public static const uint maxLights = 32;
public static const uint maxShadowViews = 64;
public static const uint lightsPerObject = 8;
public static const uint noLight = 0xFF;

public struct SceneData
{
    public float4x4 viewProjection;
    public float4x4 view;
    public float3 cameraPosition;
    public float exposure;
    // The way the light of the sun goes, and its illuminance; 1 when there is a sun.
    public float3 sunDirection;
    public uint sunEnabled;
    public float3 sunIlluminance;
    public uint lightCount;
    // The colour times the intensity of the environment, and its rotation around Y.
    public float3 sky;
    public float environmentRotation;
    public float specularMipCount;
    public uint padding0;
    // The size of the image in pixels.
    public float viewportWidth;
    public float viewportHeight;
    // The cascades of the sun's shadow: where each ends, how wide its texels are, and how far the shadows go; 1 when
    // the sun casts them.
    public float4x4 cascadeViewProjections[4];
    public float4 cascadeSplits;
    public float4 cascadeTexelSizes;
    public float shadowDistance;
    public uint sunCastsShadows;
    // The time of the game in seconds.
    public float time;
    public float padding1;
    // The sky's directions from the clip space of the image.
    public float4x4 skyInverseViewProjection;
    // The height fog, as render::GpuAtmosphere packs it; a density of 0 for none.
    public float3 fogColor;
    public float fogDensity;
    public float fogHeight;
    public float fogFalloff;
    public float fogStart;
    public float fogMaxOpacity;
    public float fogSunScattering;
    public float fogSunAnisotropy;
    public float fogSkyBlend;
    public float padding2;
    // The world from the clip space of the image.
    public float4x4 inverseViewProjection;
};

// A light as render::GpuLight lays it out.
public struct Light
{
    public float3 position;
    public float range;
    public float3 direction;
    public float spotScale;
    public float3 intensity;
    public float spotOffset;
    public int firstShadowView;
    public float shadowFar;
    public float padding0;
    public float padding1;
};

public struct LightData
{
    public Light lights[maxLights];
};

// One view of the atlas of the local lights' shadows, and where its tile lies in the atlas.
public struct ShadowView
{
    public float4x4 viewProjection;
    public float4 tile;
};

public struct ShadowViewData
{
    public ShadowView views[maxShadowViews];
};

public ConstantBuffer<SceneData> scene;
public ConstantBuffer<LightData> lightData;
public ConstantBuffer<ShadowViewData> shadowViewData;
public SamplerCube irradianceEnvironment;
public SamplerCube specularEnvironment;
public Sampler2D brdfLut;
public Sampler2DArrayShadow cascadeMap;
public Sampler2DShadow atlasMap;

float cascadeShadow(Surface surface, uint cascade)
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
    uint width;
    uint height;
    uint layers;
    cascadeMap.GetDimensions(width, height, layers);
    const float2 texel = 1.0 / float2(float(width), float(height));
    const float reference = clip.z - 0.0005;
    // 3x3 bilinear comparisons.
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            lit += cascadeMap.SampleCmpLevelZero(float3(uv + float2(float(x), float(y)) * texel, float(cascade)), reference);
        }
    }
    return lit / 9.0;
}

public float sunShadow(Surface surface)
{
    if (scene.sunCastsShadows == 0 || surface.viewDistance >= scene.shadowDistance)
    {
        return 1.0;
    }
    for (uint cascade = 0; cascade < 4; ++cascade)
    {
        if (surface.viewDistance < scene.cascadeSplits[cascade])
        {
            const float shadow = cascadeShadow(surface, cascade);
            // Shadows fade out over the last tenth of their distance.
            const float fade = saturate((scene.shadowDistance - surface.viewDistance) / (0.1 * scene.shadowDistance));
            return lerp(1.0, shadow, fade);
        }
    }
    return 1.0;
}

// How much of a local light reaches the surface: 1 in the open, 0 behind something it lights.
public float localShadow(Surface surface, Light light, float3 toSurface)
{
    if (light.firstShadowView < 0)
    {
        return 1.0;
    }
    // A point light looks at the surface through the face of its cube that holds it.
    const uint view = uint(light.firstShadowView) + (light.spotScale == 0.0 ? cubeFace(toSurface) : 0);
    if (view >= maxShadowViews)
    {
        return 1.0;
    }
    const ShadowView shadow = shadowViewData.views[view];
    // Moved along the normal, so that a surface does not shadow itself.
    const float3 offsetPosition = surface.position + surface.normal * light.shadowFar * 0.004;
    const float4 clip = mul(shadow.viewProjection, float4(offsetPosition, 1.0));
    const float3 projected = clip.xyz / max(abs(clip.w), 1e-6);
    const float2 uv = projected.xy * 0.5 + 0.5;
    if (clip.w <= 0.0 || any(uv < 0.0) || any(uv > 1.0) || projected.z > 1.0)
    {
        return 1.0;
    }
    uint width;
    uint height;
    atlasMap.GetDimensions(width, height);
    const float2 atlasTexel = 1.0 / float2(float(width), float(height));
    const float reference = projected.z - 0.0015;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            // Every tap stays inside the tile, so that a shadow never leaks into its neighbour.
            const float2 tileUv = clamp(uv + float2(float(x), float(y)) * atlasTexel / max(shadow.tile.z, 1e-6), 0.001, 0.999);
            lit += atlasMap.SampleCmpLevelZero(shadow.tile.xy + tileUv * shadow.tile.zw, reference);
        }
    }
    return lit / 9.0;
}

// The light of the environment, from its baked maps.
public float3 environmentLuminance(Surface surface)
{
    const float NoV = saturate(dot(surface.normal, surface.view));
    const float3 reflected = reflect(-surface.view, surface.normal);
    const float angle = -scene.environmentRotation;
    const float3 irradiance = irradianceEnvironment.SampleLevel(rotateAroundY(surface.normal, angle), 0.0).rgb * scene.sky;
    const float lod = surface.roughness * (scene.specularMipCount - 1.0);
    const float3 prefiltered = specularEnvironment.SampleLevel(rotateAroundY(reflected, angle), lod).rgb * scene.sky;
    const float2 brdf = brdfLut.SampleLevel(float2(NoV, surface.roughness), 0.0).rg;
    return ambientLight(surface, irradiance, prefiltered, brdf);
}

// What a light gives a surface: the physical one, or the light() of a shader of the project in its place, as
// webgpu_scene.slang has it for WebGPU.
public interface IGlLight
{
    // The luminance a light adds: it comes from `toLight`, with `color` the illuminance it brings at normal incidence, of
    // which `attenuation` reaches the surface past its range, its cone and the shadows.
    float3 shade(Surface surface, float3 toLight, float3 color, float attenuation, bool directional);
};

public struct GlPhysicalLight : IGlLight
{
    public float3 shade(Surface surface, float3 toLight, float3 color, float attenuation, bool directional)
    {
        return shadeLight(surface, toLight, color * attenuation);
    }
};

// The light of the local lights the object was given, at most eight, in the bytes of `lights`, and of the sun, through a
// light model.
public float3 directLuminance<Model : IGlLight>(Surface surface, uint2 lights, Model model)
{
    float3 luminance = float3(0.0);
    for (uint slot = 0; slot < lightsPerObject; ++slot)
    {
        const uint index = ((slot < 4 ? lights.x : lights.y) >> ((slot % 4) * 8)) & 0xFF;
        if (index == noLight || index >= scene.lightCount)
        {
            break;
        }
        const Light light = lightData.lights[index];
        const float3 toLight = light.position - surface.position;
        const float3 direction = toLight * rsqrt(max(dot(toLight, toLight), 1e-8));
        const float attenuation = localAttenuation(toLight, light.range, light.direction, light.spotScale, light.spotOffset) *
                                  localShadow(surface, light, -toLight);
        luminance += model.shade(surface, direction, light.intensity, attenuation, false);
    }
    if (scene.sunEnabled != 0)
    {
        luminance += model.shade(surface, -scene.sunDirection, scene.sunIlluminance, sunShadow(surface), true);
    }
    return luminance;
}

// The luminance leaving a surface towards the viewer, before exposure: the environment from its baked maps, the local
// lights the object was given, at most eight, in the bytes of `lights`, and the sun.
public float3 surfaceLight(Surface surface, uint2 lights)
{
    GlPhysicalLight physical;
    return environmentLuminance(surface) + directLuminance(surface, lights, physical);
}

// How far the fog is looked through towards the sky, in meters.
public static const float skyFogDistance = 20000.0;

// The light the fog sends towards the camera, in nits: the sky around it and the sun through it.
public float3 fogLight(float3 direction)
{
    const float3 flat = normalize(float3(direction.x, 0.0, direction.z) + float3(0.0, 1e-4, 0.0));
    const float angle = -scene.environmentRotation;
    const float3 ambient = (irradianceEnvironment.SampleLevel(rotateAroundY(float3(0.0, 1.0, 0.0), angle), 0.0).rgb +
                            irradianceEnvironment.SampleLevel(rotateAroundY(flat, angle), 0.0).rgb) *
                           0.5 * scene.sky;
    float3 sun = float3(0.0);
    if (scene.sunEnabled != 0)
    {
        sun = scene.sunIlluminance * henyeyGreenstein(dot(direction, -scene.sunDirection), scene.fogSunAnisotropy) *
              scene.fogSunScattering;
    }
    const float3 lit = scene.fogColor * (ambient + sun);
    if (scene.fogSkyBlend <= 0.0)
    {
        return lit;
    }
    // The sky at the horizon behind it, which the far fog melts into.
    const float3 level = normalize(float3(direction.x, max(direction.y, 0.02), direction.z));
    const float3 behind = specularEnvironment.SampleLevel(rotateAroundY(level, angle), 2.0).rgb * scene.sky;
    return lerp(lit, behind, saturate(scene.fogSkyBlend));
}

// What the fog makes of a point `exposed` shows: hidden by it, as far as it lies from the camera. Takes and gives
// colours already exposed.
public float3 applyFog(float3 exposed, float3 position)
{
    if (scene.fogDensity <= 0.0)
    {
        return exposed;
    }
    const float3 offset = position - scene.cameraPosition;
    const float distance = length(offset);
    const float3 direction = offset / max(distance, 1e-4);
    const float opacity = heightFogOpacity(scene.cameraPosition, direction, distance, scene.fogStart, scene.fogDensity,
                                           scene.fogFalloff, scene.fogHeight, scene.fogMaxOpacity);
    return lerp(exposed, fogLight(direction) * scene.exposure, opacity);
}
