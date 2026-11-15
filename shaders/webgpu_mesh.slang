// Draws meshes for WebGPU, compiled to WGSL: the surfaces of geometry.slang, lit by the sun, the local lights and
// the environment as pbr.slang lights them, with their shadows, without the decals, the fog and the air yet (web step
// 3c). The matrices of the shadows read here are those of Vulkan's clip space, Y down, whose texture coordinates go
// down too: the shadow maps were drawn with the same matrices turned to WebGPU's. The
// instances of a draw lie in a storage buffer from the first its draw names; a skinned one blends the bones of its
// pose, which already reach the world, in the vertex shader. Each material binds its textures with their samplers.

import surface_shading;

struct SceneData
{
    float4x4 viewProjection;
    float4x4 view;
    float3 cameraPosition;
    float exposure;
    // The way the light of the sun goes, and its illuminance; 1 when there is a sun.
    float3 sunDirection;
    uint sunEnabled;
    float3 sunIlluminance;
    uint lightCount;
    // The colour times the intensity of the environment, and its rotation around Y.
    float3 sky;
    float environmentRotation;
    float specularMipCount;
    float padding0;
    float padding1;
    float padding2;
    // The cascades of the sun's shadow: where each ends, how wide its texels are, and how far the shadows go; 1 when
    // the sun casts them.
    float4x4 cascadeViewProjections[4];
    float4 cascadeSplits;
    float4 cascadeTexelSizes;
    float shadowDistance;
    uint sunCastsShadows;
    float padding3;
    float padding4;
};

// One view of the atlas of the local lights' shadows, and where its tile lies in the atlas.
struct ShadowView
{
    float4x4 viewProjection;
    float4 tile;
};

// A light as render::GpuLight lays it out.
struct Light
{
    float3 position;
    float range;
    float3 direction;
    float spotScale;
    float3 intensity;
    float spotOffset;
    int firstShadowView;
    float shadowFar;
    float padding0;
    float padding1;
};

struct Instance
{
    float4x4 world;
    // The bones of a skinned instance among those of the frame; 0 bones for a rigid one.
    uint firstBone;
    uint boneCount;
    uint padding0;
    uint padding1;
};

struct MaterialData
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float alphaCutoff;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    // 0 opaque, 1 mask, 2 blend.
    uint alphaMode;
    uint doubleSided;
    uint padding0;
    uint padding1;
};

struct DrawData
{
    uint first;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;
[[vk::binding(1, 0)]]
StructuredBuffer<Light> lights;
[[vk::binding(2, 0)]]
StructuredBuffer<Instance> instances;
[[vk::binding(3, 0)]]
StructuredBuffer<float4x4> bones;
[[vk::binding(4, 0)]]
TextureCube irradianceEnvironment;
[[vk::binding(5, 0)]]
TextureCube specularEnvironment;
[[vk::binding(6, 0)]]
Texture2D brdfLut;
[[vk::binding(7, 0)]]
SamplerState clampSampler;
[[vk::binding(8, 0)]]
StructuredBuffer<ShadowView> shadowViews;
[[vk::binding(9, 0)]]
DepthTexture2DArray shadowMap;
[[vk::binding(10, 0)]]
DepthTexture2D localShadowMap;
[[vk::binding(11, 0)]]
SamplerComparisonState shadowSampler;

[[vk::binding(0, 1)]]
ConstantBuffer<MaterialData> material;
[[vk::binding(1, 1)]]
Texture2D baseColorTexture;
[[vk::binding(2, 1)]]
Texture2D metallicRoughnessTexture;
[[vk::binding(3, 1)]]
Texture2D normalTexture;
[[vk::binding(4, 1)]]
Texture2D occlusionTexture;
[[vk::binding(5, 1)]]
Texture2D emissiveTexture;
[[vk::binding(6, 1)]]
SamplerState baseColorSampler;
[[vk::binding(7, 1)]]
SamplerState metallicRoughnessSampler;
[[vk::binding(8, 1)]]
SamplerState normalSampler;
[[vk::binding(9, 1)]]
SamplerState occlusionSampler;
[[vk::binding(10, 1)]]
SamplerState emissiveSampler;

[[vk::binding(0, 2)]]
ConstantBuffer<DrawData> draw;

static const uint alphaModeMask = 1;
static const uint alphaModeBlend = 2;

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct SkinnedInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct SurfaceVertex
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

SurfaceVertex surfaceVertex(float4x4 world, float3 position, float3 normal, float2 uv, float4 tangent)
{
    const float4 worldPosition = mul(world, float4(position, 1.0));
    SurfaceVertex output;
    output.position = mul(scene.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = transformNormal(world, normal);
    output.tangent = float4(mul(float3x3(world), tangent.xyz), tangent.w * handedness(world));
    output.uv = uv;
    return output;
}

[shader("vertex")]
SurfaceVertex meshVertex(VertexInput vertex, uint instanceIndex : SV_InstanceID)
{
    const Instance instance = instances[draw.first + instanceIndex];
    return surfaceVertex(instance.world, vertex.position, vertex.normal, vertex.uv, vertex.tangent);
}

[shader("vertex")]
SurfaceVertex skinnedVertex(SkinnedInput vertex, uint instanceIndex : SV_InstanceID)
{
    const Instance instance = instances[draw.first + instanceIndex];
    const uint first = instance.firstBone;
    float4x4 blended = bones[first + vertex.joints.x] * vertex.weights.x;
    blended += bones[first + vertex.joints.y] * vertex.weights.y;
    blended += bones[first + vertex.joints.z] * vertex.weights.z;
    blended += bones[first + vertex.joints.w] * vertex.weights.w;
    // Vertices left without weights stay where the instance is.
    const float total = vertex.weights.x + vertex.weights.y + vertex.weights.z + vertex.weights.w;
    const float4x4 world = total > 0.0001 ? blended : instance.world;
    return surfaceVertex(world, vertex.position, vertex.normal, vertex.uv, vertex.tangent);
}

float cascadeShadow(Surface surface, uint cascade)
{
    // Moving the point along its normal by a texel keeps surfaces from shadowing themselves.
    const float texelSize = scene.cascadeTexelSizes[cascade];
    const float3 offsetPosition = surface.position + surface.normal * texelSize * 1.5;
    const float4 clip = mul(scene.cascadeViewProjections[cascade], float4(offsetPosition, 1.0));
    const float2 uv = clip.xy * 0.5 + 0.5;
    // 3x3 bilinear comparisons, read before anything leaves: WGSL wants every pixel to read its texture.
    uint width;
    uint height;
    uint layers;
    shadowMap.GetDimensions(width, height, layers);
    const float2 texel = 1.0 / float2(float(width), float(height));
    const float reference = clip.z - 0.0005;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            lit += shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv + float2(float(x), float(y)) * texel, float(cascade)),
                                                reference);
        }
    }
    if (any(uv < 0.0) || any(uv > 1.0) || clip.z > 1.0)
    {
        return 1.0;
    }
    return lit / 9.0;
}

float sunShadow(Surface surface)
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
float localShadow(Surface surface, Light light, float3 toSurface)
{
    if (light.firstShadowView < 0)
    {
        return 1.0;
    }
    // A point light looks at the surface through the face of its cube that holds it.
    const uint view = uint(light.firstShadowView) + (light.spotScale == 0.0 ? cubeFace(toSurface) : 0);
    const ShadowView shadow = shadowViews[view];
    // Moved along the normal, so that a surface does not shadow itself.
    const float3 offsetPosition = surface.position + surface.normal * light.shadowFar * 0.004;
    const float4 clip = mul(shadow.viewProjection, float4(offsetPosition, 1.0));
    const float3 projected = clip.xyz / max(abs(clip.w), 1e-6);
    const float2 uv = projected.xy * 0.5 + 0.5;
    uint width;
    uint height;
    localShadowMap.GetDimensions(width, height);
    const float2 atlasTexel = 1.0 / float2(float(width), float(height));
    const float reference = projected.z - 0.0015;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            // Every tap stays inside the tile, so that a shadow never leaks into its neighbour.
            const float2 tileUv = clamp(uv + float2(float(x), float(y)) * atlasTexel / max(shadow.tile.z, 1e-6), 0.001, 0.999);
            lit += localShadowMap.SampleCmpLevelZero(shadowSampler, shadow.tile.xy + tileUv * shadow.tile.zw, reference);
        }
    }
    if (clip.w <= 0.0 || any(uv < 0.0) || any(uv > 1.0) || projected.z > 1.0)
    {
        return 1.0;
    }
    return lit / 9.0;
}

[shader("fragment")]
float4 meshFragment(SurfaceVertex input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    // Every texture is read first, where every pixel of a quad goes the same way, as WGSL wants it.
    const float4 baseSample = baseColorTexture.Sample(baseColorSampler, input.uv);
    const float4 metallicRoughness = metallicRoughnessTexture.Sample(metallicRoughnessSampler, input.uv);
    const float2 mappedNormal = normalTexture.Sample(normalSampler, input.uv).rg;
    const float occlusionSample = occlusionTexture.Sample(occlusionSampler, input.uv).r;
    const float3 emissiveSample = emissiveTexture.Sample(emissiveSampler, input.uv).rgb;

    const float4 baseColor = material.baseColorFactor * baseSample;
    if (material.alphaMode == alphaModeMask && baseColor.a < material.alphaCutoff)
    {
        discard;
    }
    float3 normal = normalize(input.normal);
    const float3 tangent = input.tangent.xyz - normal * dot(normal, input.tangent.xyz);
    // Back faces of double-sided materials are lit as seen from their side.
    if (!frontFacing)
    {
        normal = -normal;
    }
    normal = normalMapped(normal, tangent, input.tangent.w, mappedNormal, material.normalScale);

    const float metallic = saturate(material.metallicFactor * metallicRoughness.b);
    // Very low roughness makes highlights too small to sample: clamp it.
    const float roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    Surface surface;
    surface.position = input.worldPosition;
    surface.normal = normal;
    surface.view = normalize(scene.cameraPosition - input.worldPosition);
    surface.diffuseColor = baseColor.rgb * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04), baseColor.rgb, metallic);
    surface.roughness = roughness;
    surface.alpha = roughness * roughness;
    surface.occlusion = lerp(1.0, occlusionSample, material.occlusionStrength);
    surface.viewDistance = -mul(scene.view, float4(input.worldPosition, 1.0)).z;
    surface.fragmentCoordinate = input.position.xy;

    // The environment, from its baked maps.
    const float NoV = saturate(dot(surface.normal, surface.view));
    const float3 reflected = reflect(-surface.view, surface.normal);
    const float angle = -scene.environmentRotation;
    const float3 irradiance =
        irradianceEnvironment.SampleLevel(clampSampler, rotateAroundY(surface.normal, angle), 0.0).rgb * scene.sky;
    const float lod = surface.roughness * (scene.specularMipCount - 1.0);
    const float3 prefiltered =
        specularEnvironment.SampleLevel(clampSampler, rotateAroundY(reflected, angle), lod).rgb * scene.sky;
    const float2 brdf = brdfLut.SampleLevel(clampSampler, float2(NoV, surface.roughness), 0.0).rg;
    float3 luminance = ambientLight(surface, irradiance, prefiltered, brdf);

    // The local lights, all of them, and the sun.
    for (uint index = 0; index < scene.lightCount; ++index)
    {
        const Light light = lights[index];
        const float3 toLight = light.position - surface.position;
        const float3 direction = toLight * rsqrt(max(dot(toLight, toLight), 1e-8));
        const float attenuation = localAttenuation(toLight, light.range, light.direction, light.spotScale, light.spotOffset) *
                                  localShadow(surface, light, -toLight);
        luminance += shadeLight(surface, direction, light.intensity * attenuation);
    }
    if (scene.sunEnabled != 0)
    {
        luminance += shadeLight(surface, -scene.sunDirection, scene.sunIlluminance * sunShadow(surface));
    }

    // Emission is relative to the exposure: an emissive colour of 1 shows as white-ish on screen.
    const float3 emission = material.emissiveFactor * emissiveSample;
    // A blended surface lets (1 - alpha) of what is behind it through; everything else covers it.
    const float alpha = material.alphaMode == alphaModeBlend ? saturate(baseColor.a) : 1.0;
    return float4(luminance * scene.exposure + emission, alpha);
}
