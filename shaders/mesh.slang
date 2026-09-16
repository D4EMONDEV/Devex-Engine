// Draws meshes with their materials. Vertices, scene data and materials are read through buffer
// device addresses passed in push constants (vertex pulling), so the pipeline has no vertex input
// state; textures come from one bindless array.
// Layouts must match engine/src/render/vulkan/GpuData.hpp.

struct Vertex
{
    float3 position;
    float u;
    float3 normal;
    float v;
};

struct Material
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float alphaCutoff;
    uint baseColorTexture;
    uint metallicRoughnessTexture;
    uint normalTexture;
    uint occlusionTexture;
    uint emissiveTexture;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    // 0 opaque, 1 mask, 2 blend.
    uint alphaMode;
    uint doubleSided;
    float padding;
};

struct SceneData
{
    float4x4 viewProjection;
    // Direction in which the light travels, normalized.
    float3 lightDirection;
    float ambient;
    Material* materials;
};

struct DrawData
{
    SceneData* scene;
    Vertex* vertices;
    float4x4 world;
    uint material;
    uint padding;
};

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

[[vk::binding(0, 0)]]
Texture2D textures[];

[[vk::binding(1, 0)]]
SamplerState materialSampler;

static const uint alphaModeMask = 1;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    const float4 worldPosition = mul(draw.world, float4(vertex.position, 1.0));

    // Normals transform with the cofactor matrix, which stays correct under non-uniform scales.
    // Its sign follows the determinant so that mirrored transforms keep outward normals.
    const float3x3 columns = transpose(float3x3(draw.world));
    const float3 normal = cross(columns[1], columns[2]) * vertex.normal.x +
                          cross(columns[2], columns[0]) * vertex.normal.y +
                          cross(columns[0], columns[1]) * vertex.normal.z;
    const float determinant = dot(columns[0], cross(columns[1], columns[2]));

    VertexOutput output;
    output.position = mul(draw.scene->viewProjection, worldPosition);
    output.normal = determinant < 0.0 ? -normal : normal;
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

float4 sampleTexture(uint index, float2 uv)
{
    return textures[NonUniformResourceIndex(index)].Sample(materialSampler, uv);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    let scene = draw.scene;
    let material = scene->materials[draw.material];

    const float4 baseColor =
        material.baseColorFactor * sampleTexture(material.baseColorTexture, input.uv);
    if (material.alphaMode == alphaModeMask && baseColor.a < material.alphaCutoff)
    {
        discard;
    }

    float3 normal = normalize(input.normal);
    // Back faces of double-sided materials are lit as seen from their side.
    if (!frontFacing)
    {
        normal = -normal;
    }
    const float diffuse = saturate(dot(normal, -scene->lightDirection));
    const float occlusion = lerp(1.0, sampleTexture(material.occlusionTexture, input.uv).r,
                                 material.occlusionStrength);
    const float3 emission =
        material.emissiveFactor * sampleTexture(material.emissiveTexture, input.uv).rgb;

    const float lighting = scene->ambient * occlusion + (1.0 - scene->ambient) * diffuse;
    return float4(baseColor.rgb * lighting + emission, 1.0);
}
