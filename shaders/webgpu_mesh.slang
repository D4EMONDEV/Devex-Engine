// Draws meshes for WebGPU, compiled to WGSL: the surfaces of geometry.slang, with the decals of decal_layer.slang laid
// on them, lit by the sun, the local lights and the environment as pbr.slang lights them, with their shadows, and
// veiled by the fog, without the air (after the 1.0). The scene and its light come from webgpu_scene.slang. The
// decals read their textures from two atlases, colours and data, as Godot's mobile renderer does. The
// instances of a draw lie in a storage buffer from the first its draw names; a skinned one blends the bones of its
// pose, which already reach the world, in the vertex shader. Each material binds its textures with their samplers.

import decal_layer;
import surface_shading;
import webgpu_scene;

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

[[vk::binding(2, 0)]]
StructuredBuffer<Instance> instances;
[[vk::binding(3, 0)]]
StructuredBuffer<float4x4> bones;

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

// A decal of the frame, as the renderer packs it: its box and fades, and where each texture lies in its atlas, its
// corner and its size from 0 to 1; a size of 0 for none.
struct AtlasDecal
{
    float4x4 worldToDecal;
    float4 axisX;
    float4 axisZ;
    float4 axisY;
    float4 modulate;
    float albedoMix;
    float emissionEnergy;
    float normalFade;
    float upperFade;
    float lowerFade;
    float fadeBegin;
    float fadeLength;
    float padding0;
    float4 rects[4];
};

[[vk::binding(14, 0)]]
StructuredBuffer<AtlasDecal> decals;
// The albedo and emission of the decals, in sRGB, and their normal and ORM maps.
[[vk::binding(15, 0)]]
Texture2D decalColors;
[[vk::binding(16, 0)]]
Texture2D decalData;
[[vk::binding(17, 0)]]
SamplerState decalSampler;

static const float decalAtlasSize = 2048.0;

struct AtlasTextures : IDecalTextures
{
    float4 rects[4];

    bool has(uint kind)
    {
        return rects[kind].z > 0.0;
    }

    float4 read(uint kind, float2 uv, float2 slopeX, float2 slopeY)
    {
        const float4 rect = rects[kind];
        // Half a texel inside its rectangle, so that its neighbours never bleed in.
        const float2 margin = 0.5 / (rect.zw * decalAtlasSize);
        const float2 inside = rect.xy + clamp(uv, margin, 1.0 - margin) * rect.zw;
        if (kind == decalAlbedo || kind == decalEmission)
        {
            return decalColors.SampleGrad(decalSampler, inside, slopeX * rect.zw, slopeY * rect.zw);
        }
        return decalData.SampleGrad(decalSampler, inside, slopeX * rect.zw, slopeY * rect.zw);
    }
};

DecalShape shapeOf(AtlasDecal decal)
{
    DecalShape shape;
    shape.worldToDecal = decal.worldToDecal;
    shape.axisX = decal.axisX;
    shape.axisZ = decal.axisZ;
    shape.axisY = decal.axisY;
    shape.modulate = decal.modulate;
    shape.albedoMix = decal.albedoMix;
    shape.emissionEnergy = decal.emissionEnergy;
    shape.normalFade = decal.normalFade;
    shape.upperFade = decal.upperFade;
    shape.lowerFade = decal.lowerFade;
    shape.fadeBegin = decal.fadeBegin;
    shape.fadeLength = decal.fadeLength;
    return shape;
}

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
    // The slopes of the position, which the decals read their textures with.
    const float3 positionDx = ddx(input.worldPosition);
    const float3 positionDy = ddy(input.worldPosition);

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
    // Which way the surface itself faces, before its normal map: decals fade by it.
    const float3 facing = normal;
    normal = normalMapped(normal, tangent, input.tangent.w, mappedNormal, material.normalScale);
    const float viewDistance = -mul(scene.view, float4(input.worldPosition, 1.0)).z;

    // The material, then the decals laid on it before the light falls on them.
    DecalLayer layer;
    layer.baseColor = baseColor.rgb;
    layer.normal = normal;
    layer.metallic = saturate(material.metallicFactor * metallicRoughness.b);
    // Very low roughness makes highlights too small to sample: clamp it.
    layer.roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    layer.occlusion = lerp(1.0, occlusionSample, material.occlusionStrength);
    // Emission is relative to the exposure: an emissive colour of 1 shows as white-ish on screen.
    layer.emission = material.emissiveFactor * emissiveSample;
    for (uint index = 0; index < scene.decalCount; ++index)
    {
        const AtlasDecal decal = decals[index];
        AtlasTextures textures;
        textures.rects = decal.rects;
        layDecal(shapeOf(decal), textures, input.worldPosition, facing, viewDistance, positionDx, positionDy, layer);
    }

    Surface surface;
    surface.position = input.worldPosition;
    surface.normal = layer.normal;
    surface.view = normalize(scene.cameraPosition - input.worldPosition);
    surface.diffuseColor = layer.baseColor * (1.0 - layer.metallic);
    surface.f0 = lerp(float3(0.04), layer.baseColor, layer.metallic);
    surface.roughness = layer.roughness;
    surface.alpha = layer.roughness * layer.roughness;
    surface.occlusion = layer.occlusion;
    surface.viewDistance = viewDistance;
    surface.fragmentCoordinate = input.position.xy;

    const float3 luminance = surfaceLight(surface);
    const float3 emission = layer.emission;
    // A blended surface lets (1 - alpha) of what is behind it through; everything else covers it.
    const float alpha = material.alphaMode == alphaModeBlend ? saturate(baseColor.a) : 1.0;
    // Veiled by the fog between it and the camera.
    return float4(applyFog(luminance * scene.exposure + emission, surface.position), alpha);
}
