// Draws the depth of shadow casters for OpenGL, translated into GLSL, as webgpu_shadow.slang does for WebGPU: into one
// cascade of the sun's shadow map, or into the tile of the atlas that a local light was given. Each instance arrives as
// attributes, laid out as those of gl_mesh.slang, and the bones from the same texture.

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

ConstantBuffer<ViewData> shadowView;
ConstantBuffer<MaterialData> material;
Sampler2D baseColorTexture;
Sampler2D boneTexture;

static const uint alphaModeMask = 1;
static const uint bonesPerRow = 256;

struct ShadowVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Every attribute of a draw in one structure, at the locations the vertex arrays give them: Slang counts the locations
// of each parameter from where the one before ends. The vertex, then the skin of a skinned one, then the instance: the
// columns of its matrix, the bytes of its local lights, and its first bone and how many it has.
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

[shader("vertex")]
ShadowVertex shadowVertex(MeshInput input)
{
    ShadowVertex output;
    const float4x4 world = worldOf(input.world0, input.world1, input.world2, input.world3);
    output.position = mul(shadowView.viewProjection, mul(world, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

[shader("vertex")]
ShadowVertex skinnedShadowVertex(SkinnedInput input)
{
    ShadowVertex output;
    output.position = mul(shadowView.viewProjection, mul(skinnedWorld(input), float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
void shadowFragment(ShadowVertex input)
{
    // Cut-out materials cast shadows only where they are opaque.
    const float alpha = material.baseColorFactor.a * baseColorTexture.Sample(input.uv).a;
    if (material.alphaMode == alphaModeMask && alpha < material.alphaCutoff)
    {
        discard;
    }
}
