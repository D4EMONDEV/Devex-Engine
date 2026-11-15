// Renders the depth of shadow casters for WebGPU, compiled to WGSL, as shadow.slang does for Vulkan: into one cascade
// of the sun's shadow map, or into the tile of the atlas that a local light was given. The instances of a draw lie in
// a storage buffer from the first its draw names, and the view at a dynamic offset of its own.

struct Instance
{
    float4x4 world;
    uint firstBone;
    uint boneCount;
    uint padding0;
    uint padding1;
};

struct ViewData
{
    float4x4 viewProjection;
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
StructuredBuffer<Instance> instances;
[[vk::binding(1, 0)]]
StructuredBuffer<float4x4> bones;
[[vk::binding(2, 0)]]
ConstantBuffer<ViewData> view;

[[vk::binding(0, 1)]]
ConstantBuffer<MaterialData> material;
[[vk::binding(1, 1)]]
Texture2D baseColorTexture;
[[vk::binding(6, 1)]]
SamplerState baseColorSampler;

[[vk::binding(0, 2)]]
ConstantBuffer<DrawData> draw;

static const uint alphaModeMask = 1;

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

struct ShadowVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
ShadowVertex shadowVertex(VertexInput vertex, uint instanceIndex : SV_InstanceID)
{
    const Instance instance = instances[draw.first + instanceIndex];
    ShadowVertex output;
    output.position = mul(view.viewProjection, mul(instance.world, float4(vertex.position, 1.0)));
    output.uv = vertex.uv;
    return output;
}

[shader("vertex")]
ShadowVertex skinnedShadowVertex(SkinnedInput vertex, uint instanceIndex : SV_InstanceID)
{
    const Instance instance = instances[draw.first + instanceIndex];
    const uint first = instance.firstBone;
    float4x4 blended = bones[first + vertex.joints.x] * vertex.weights.x;
    blended += bones[first + vertex.joints.y] * vertex.weights.y;
    blended += bones[first + vertex.joints.z] * vertex.weights.z;
    blended += bones[first + vertex.joints.w] * vertex.weights.w;
    const float total = vertex.weights.x + vertex.weights.y + vertex.weights.z + vertex.weights.w;
    const float4x4 world = total > 0.0001 ? blended : instance.world;
    ShadowVertex output;
    output.position = mul(view.viewProjection, mul(world, float4(vertex.position, 1.0)));
    output.uv = vertex.uv;
    return output;
}

[shader("fragment")]
void shadowFragment(ShadowVertex input)
{
    // Cut-out materials cast shadows only where they are opaque.
    const float alpha = material.baseColorFactor.a * baseColorTexture.Sample(baseColorSampler, input.uv).a;
    if (material.alphaMode == alphaModeMask && alpha < material.alphaCutoff)
    {
        discard;
    }
}
