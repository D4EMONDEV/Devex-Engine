// Draws meshes for OpenGL, translated into GLSL ES 3.00 and GLSL 3.30, as webgpu_mesh.slang does for WebGPU: the surfaces
// of geometry.slang lit by the sun, the local lights the object was given and the environment, with their shadows, and
// veiled by the fog. Each instance arrives as attributes, since OpenGL ES 3.0 has no storage buffers: the columns of its
// matrix, the bytes of its lights, and where its bones start in the texture that holds those of the frame, four texels
// a bone. The decals lie in a uniform block, at most 64 a frame and 8 an object, a byte each in its attributes, as the
// local lights; their textures in two atlases, colours and data, as WebGPU's.

import decal_layer;
import gl_scene;
import surface_shading;

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

ConstantBuffer<MaterialData> material;
Sampler2D baseColorTexture;
Sampler2D metallicRoughnessTexture;
Sampler2D normalTexture;
Sampler2D occlusionTexture;
Sampler2D emissiveTexture;
// The bones of the frame, which already reach the world: the four columns of each, one a texel.
Sampler2D boneTexture;

static const uint maxDecals = 64;
static const uint decalsPerObject = 8;
static const uint noDecal = 0xFF;

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

struct DecalData
{
    AtlasDecal decals[maxDecals];
};

ConstantBuffer<DecalData> decalData;
// The albedo and emission of the decals, in sRGB, and their normal and ORM maps.
Sampler2D decalColors;
Sampler2D decalSurfaces;

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
            return decalColors.SampleGrad(inside, slopeX * rect.zw, slopeY * rect.zw);
        }
        return decalSurfaces.SampleGrad(inside, slopeX * rect.zw, slopeY * rect.zw);
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
// How many bones a row of the texture of bones holds.
static const uint bonesPerRow = 256;

struct SurfaceVertex
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    nointerpolation uint2 lights : TEXCOORD1;
    nointerpolation uint2 decals : TEXCOORD2;
};

// Every attribute of a draw in one structure, at the locations the vertex arrays give them: Slang counts the locations
// of each parameter from where the one before ends. The vertex, then the skin of a skinned one, then the instance: the
// columns of its matrix, the bytes of its local lights, its first bone and how many it has, and the bytes of its decals.
struct MeshInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT;
    [[vk::location(6)]] float4 world0 : WORLD0;
    [[vk::location(7)]] float4 world1 : WORLD1;
    [[vk::location(8)]] float4 world2 : WORLD2;
    [[vk::location(9)]] float4 world3 : WORLD3;
    [[vk::location(10)]] uint2 lights : LIGHTS;
    [[vk::location(11)]] uint2 bones : BONES;
    [[vk::location(12)]] uint2 decals : DECALS;
};

struct SkinnedInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT;
    [[vk::location(4)]] uint4 joints : BLENDINDICES;
    [[vk::location(5)]] float4 weights : BLENDWEIGHT;
    [[vk::location(6)]] float4 world0 : WORLD0;
    [[vk::location(7)]] float4 world1 : WORLD1;
    [[vk::location(8)]] float4 world2 : WORLD2;
    [[vk::location(9)]] float4 world3 : WORLD3;
    [[vk::location(10)]] uint2 lights : LIGHTS;
    [[vk::location(11)]] uint2 bones : BONES;
    [[vk::location(12)]] uint2 decals : DECALS;
};

// The matrix of an instance, from its columns.
float4x4 worldOf(float4 world0, float4 world1, float4 world2, float4 world3)
{
    return transpose(float4x4(world0, world1, world2, world3));
}

float4x4 boneAt(uint index)
{
    const int x = int((index % bonesPerRow) * 4);
    const int y = int(index / bonesPerRow);
    return transpose(float4x4(boneTexture.Load(int3(x, y, 0)), boneTexture.Load(int3(x + 1, y, 0)),
                              boneTexture.Load(int3(x + 2, y, 0)), boneTexture.Load(int3(x + 3, y, 0))));
}

// The matrix of a skinned vertex: its bones blended by its weights; where the instance is, without weights.
float4x4 skinnedWorld(SkinnedInput input)
{
    const uint first = input.bones.x;
    float4x4 blended = boneAt(first + input.joints.x) * input.weights.x;
    blended += boneAt(first + input.joints.y) * input.weights.y;
    blended += boneAt(first + input.joints.z) * input.weights.z;
    blended += boneAt(first + input.joints.w) * input.weights.w;
    const float total = input.weights.x + input.weights.y + input.weights.z + input.weights.w;
    return total > 0.0001 ? blended : worldOf(input.world0, input.world1, input.world2, input.world3);
}

SurfaceVertex surfaceVertex(float4x4 world, float3 position, float3 normal, float2 uv, float4 tangent, uint2 lights, uint2 decals)
{
    const float4 worldPosition = mul(world, float4(position, 1.0));
    SurfaceVertex output;
    output.position = mul(scene.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = transformNormal(world, normal);
    output.tangent = float4(mul(float3x3(world), tangent.xyz), tangent.w * handedness(world));
    output.uv = uv;
    output.lights = lights;
    output.decals = decals;
    return output;
}

[shader("vertex")]
SurfaceVertex meshVertex(MeshInput input)
{
    return surfaceVertex(worldOf(input.world0, input.world1, input.world2, input.world3), input.position, input.normal, input.uv,
                         input.tangent, input.lights, input.decals);
}

[shader("vertex")]
SurfaceVertex skinnedVertex(SkinnedInput input)
{
    return surfaceVertex(skinnedWorld(input), input.position, input.normal, input.uv, input.tangent, input.lights, input.decals);
}

[shader("fragment")]
float4 meshFragment(SurfaceVertex input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    // The slopes of the position, which the decals read their textures with, taken where every pixel of a quad is.
    const float3 positionDx = ddx(input.worldPosition);
    const float3 positionDy = ddy(input.worldPosition);
    const float4 baseColor = material.baseColorFactor * baseColorTexture.Sample(input.uv);
    if (material.alphaMode == alphaModeMask && baseColor.a < material.alphaCutoff)
    {
        discard;
    }
    const float4 metallicRoughness = metallicRoughnessTexture.Sample(input.uv);
    const float2 mappedNormal = normalTexture.Sample(input.uv).rg;
    const float occlusionSample = occlusionTexture.Sample(input.uv).r;
    const float3 emissiveSample = emissiveTexture.Sample(input.uv).rgb;

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

    // The material, then the decals laid on it before the light falls on them, in the order of the frame.
    DecalLayer layer;
    layer.baseColor = baseColor.rgb;
    layer.normal = normal;
    layer.metallic = saturate(material.metallicFactor * metallicRoughness.b);
    // Very low roughness makes highlights too small to sample: clamp it.
    layer.roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    layer.occlusion = lerp(1.0, occlusionSample, material.occlusionStrength);
    // Emission is relative to the exposure: an emissive colour of 1 shows as white-ish on screen.
    layer.emission = material.emissiveFactor * emissiveSample;
    for (uint slot = 0; slot < decalsPerObject; ++slot)
    {
        const uint index = ((slot < 4 ? input.decals.x : input.decals.y) >> ((slot % 4) * 8)) & 0xFF;
        if (index == noDecal)
        {
            break;
        }
        const AtlasDecal decal = decalData.decals[index];
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

    const float3 luminance = surfaceLight(surface, input.lights);
    // A blended surface lets (1 - alpha) of what is behind it through; everything else covers it.
    const float alpha = material.alphaMode == alphaModeBlend ? saturate(baseColor.a) : 1.0;
    // Veiled by the fog between it and the camera.
    return float4(applyFog(luminance * scene.exposure + layer.emission, surface.position), alpha);
}
