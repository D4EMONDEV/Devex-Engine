// Renders the depth of shadow casters into one cascade of the sun's shadow map, or into the tile
// of the atlas that a local light was given.

import common;

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    const float4 worldPosition = mul(vertexTransform(draw, vertexIndex), float4(vertex.position, 1.0));

    VertexOutput output;
    output.position = mul(draw.scene->cascadeViewProjections[draw.cascade], worldPosition);
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

// The same, for a light that owns a view of the shadow atlas: the draw carries the view it writes
// where a cascade carries its number.
[shader("vertex")]
VertexOutput localVertex(uint vertexIndex : SV_VertexID)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    const float4 worldPosition = mul(vertexTransform(draw, vertexIndex), float4(vertex.position, 1.0));

    VertexOutput output;
    output.position = mul(draw.scene->shadowViews[draw.cascade].viewProjection, worldPosition);
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
    // Cut-out materials cast shadows only where they are opaque.
    let material = draw.scene->materials[draw.material];
    if (material.alphaMode == alphaModeMask)
    {
        const float alpha =
            material.baseColorFactor.a * sampleTexture(material.baseColorTexture, input.uv).a;
        if (alpha < material.alphaCutoff)
        {
            discard;
        }
    }
}
