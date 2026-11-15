// Draws meshes for WebGPU, compiled to WGSL: the surfaces of geometry.slang, lit by the sun, the local lights and
// the environment as pbr.slang lights them, without the shadows, decals, fog and air yet (web steps 3b and 3c). The
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
        const float attenuation = localAttenuation(toLight, light.range, light.direction, light.spotScale, light.spotOffset);
        if (attenuation > 0.0)
        {
            luminance += shadeLight(surface, direction, light.intensity * attenuation);
        }
    }
    if (scene.sunEnabled != 0)
    {
        luminance += shadeLight(surface, -scene.sunDirection, scene.sunIlluminance);
    }

    // Emission is relative to the exposure: an emissive colour of 1 shows as white-ish on screen.
    const float3 emission = material.emissiveFactor * emissiveSample;
    // A blended surface lets (1 - alpha) of what is behind it through; everything else covers it.
    const float alpha = material.alphaMode == alphaModeBlend ? saturate(baseColor.a) : 1.0;
    return float4(luminance * scene.exposure + emission, alpha);
}
