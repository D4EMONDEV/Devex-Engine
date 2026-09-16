// Draws lit meshes. Vertices and scene data are read through buffer device addresses passed in
// push constants (vertex pulling), so the pipeline has no vertex input state.
// Layouts must match engine/src/render/vulkan/GpuData.hpp.

struct Vertex
{
    float3 position;
    float u;
    float3 normal;
    float v;
};

struct SceneData
{
    float4x4 viewProjection;
    // Direction in which the light travels, normalized.
    float3 lightDirection;
    float ambient;
};

struct DrawData
{
    SceneData* scene;
    Vertex* vertices;
    float4x4 world;
};

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

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

    VertexOutput output;
    output.position = mul(draw.scene->viewProjection, worldPosition);
    // Valid for rotations and uniform scales; non-uniform scales will need a normal matrix.
    output.normal = mul(float3x3(draw.world), vertex.normal);
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 normal = normalize(input.normal);
    let scene = draw.scene;
    const float diffuse = saturate(dot(normal, -scene->lightDirection));

    // A subtle checker reveals the texture coordinates until materials exist.
    const float2 cell = floor(input.uv * 8.0);
    const float checker = fmod(cell.x + cell.y, 2.0);
    const float3 albedo = lerp(float3(0.72, 0.74, 0.78), float3(0.58, 0.60, 0.66), checker);

    const float lighting = scene->ambient + (1.0 - scene->ambient) * diffuse;
    return float4(albedo * lighting, 1.0);
}
