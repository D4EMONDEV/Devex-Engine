// Renders the object identifier of the surface under one pixel, for picking in the editor, and the
// silhouettes of selected objects, which the overlay outlines.

import common;

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput transformVertex(float4x4 viewProjection, uint vertexIndex)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    VertexOutput output;
    output.position = mul(viewProjection, mul(draw.world, float4(vertex.position, 1.0)));
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

// Cut-out materials are only picked and outlined where they are opaque.
void discardTransparent(float2 uv)
{
    let material = draw.scene->materials[draw.material];
    if (material.alphaMode == alphaModeMask)
    {
        const float alpha = material.baseColorFactor.a * sampleTexture(material.baseColorTexture, uv).a;
        if (alpha < material.alphaCutoff)
        {
            discard;
        }
    }
}

// The pick projection maps the requested pixel to the whole 1x1 target.
[shader("vertex")]
VertexOutput pickVertex(uint vertexIndex : SV_VertexID)
{
    return transformVertex(draw.scene->pickViewProjection, vertexIndex);
}

[shader("fragment")]
uint pickFragment(VertexOutput input) : SV_Target
{
    discardTransparent(input.uv);
    return draw.objectId;
}

[shader("vertex")]
VertexOutput maskVertex(uint vertexIndex : SV_VertexID)
{
    return transformVertex(draw.scene->viewProjection, vertexIndex);
}

[shader("fragment")]
float maskFragment(VertexOutput input) : SV_Target
{
    discardTransparent(input.uv);
    return 1.0;
}
