// Writes the depth of the scene before it is shaded, along with the motion of every pixel and the
// normal of its surface. The temporal antialiasing follows the motion, the ambient occlusion reads
// the normals, and the shading pass then draws only what is visible.

import common;

struct PrepassData
{
    DrawData draw;
    // Where the instance stood on the previous frame, and the bones it was posed by.
    float4x4 previousWorld;
    float4x4* previousBones;
};

[[vk::push_constant]]
ConstantBuffer<PrepassData> prepass;

struct VertexOutput
{
    float4 position : SV_Position;
    // Clip positions without the jitter, whose difference is the motion of the pixel.
    float4 current : TEXCOORD0;
    float4 previous : TEXCOORD1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD2;
};

// Where the vertex stood on the previous frame: the same blend of bones, posed as they were.
float4x4 previousVertexTransform(uint vertexIndex)
{
    if (prepass.draw.skinned == 0)
    {
        return prepass.previousWorld;
    }
    const VertexSkin skin = prepass.draw.skin[vertexIndex];
    float4x4 blended = prepass.previousBones[skin.joints.x] * skin.weights.x;
    blended += prepass.previousBones[skin.joints.y] * skin.weights.y;
    blended += prepass.previousBones[skin.joints.z] * skin.weights.z;
    blended += prepass.previousBones[skin.joints.w] * skin.weights.w;
    const float total = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
    return total > 0.0001 ? blended : prepass.previousWorld;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    let draw = prepass.draw;
    const Vertex vertex = draw.vertices[vertexIndex];
    const float4x4 world = vertexTransform(draw, vertexIndex);
    const float4 worldPosition = mul(world, float4(vertex.position, 1.0));
    const float4 previousWorldPosition =
        mul(previousVertexTransform(vertexIndex), float4(vertex.position, 1.0));

    // Normals transform with the cofactor matrix, as they do when the surface is shaded.
    const float3x3 columns = transpose(float3x3(world));
    const float3 normal = cross(columns[1], columns[2]) * vertex.normal.x +
                          cross(columns[2], columns[0]) * vertex.normal.y +
                          cross(columns[0], columns[1]) * vertex.normal.z;
    const float determinant = dot(columns[0], cross(columns[1], columns[2]));

    VertexOutput output;
    output.position = mul(draw.scene->viewProjection, worldPosition);
    output.current = mul(draw.scene->unjitteredViewProjection, worldPosition);
    output.previous = mul(draw.scene->previousViewProjection, previousWorldPosition);
    output.normal = determinant < 0.0 ? -normal : normal;
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

struct FragmentOutput
{
    // Where the pixel was on the previous frame, as a distance in texture coordinates.
    float2 velocity : SV_Target0;
    // The normal of the surface, folded into two numbers.
    float2 normal : SV_Target1;
};

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace)
{
    let material = prepass.draw.scene->materials[prepass.draw.material];
    if (material.alphaMode == alphaModeMask)
    {
        const float alpha =
            material.baseColorFactor.a * sampleTexture(material.baseColorTexture, input.uv).a;
        if (alpha < material.alphaCutoff)
        {
            discard;
        }
    }

    const float2 current = input.current.xy / input.current.w;
    const float2 previous = input.previous.xy / input.previous.w;
    const float3 normal = frontFacing ? input.normal : -input.normal;

    FragmentOutput output;
    // Clip space spans two units where texture coordinates span one.
    output.velocity = (current - previous) * 0.5;
    output.normal = encodeNormal(normalize(normal));
    return output;
}
