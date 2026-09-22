// Draws the interface over the tonemapped scene: coloured rectangles, textured ones, panels with
// rounded corners, and the letters of a font read from its atlas of distances. Positions arrive in
// pixels of the image, and colours are blended over it with straight alpha.

import common;

public struct UiVertex
{
    public float2 position;
    public float2 uv;
    public float4 color;
};

static const uint kindQuad = 0;
static const uint kindRoundedQuad = 1;
static const uint kindText = 2;

struct UiData
{
    UiVertex* vertices;
    // The vertex each triangle corner reads, since a draw covers a slice of one shared list.
    uint* indices;
    // Turns pixels into clip space: 2 / width and 2 / height.
    float2 inverseViewport;
    uint kind;
    // Slot of the texture in the bindless array; the white texture leaves the colour alone.
    uint texture;
    // The rectangle whose corners are rounded, in pixels.
    float4 rect;
    float radius;
    // How much of the stored distances one pixel of the screen covers, for the letters.
    float sharpness;
};

[[vk::push_constant]]
ConstantBuffer<UiData> ui;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    // Where the pixel is in the image, which the rounded corners measure from.
    float2 pixel : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    const UiVertex vertex = ui.vertices[ui.indices[vertexIndex]];
    VertexOutput output;
    output.position = float4(vertex.position * ui.inverseViewport - 1.0, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = vertex.color;
    output.pixel = vertex.position;
    return output;
}

// How far a point is from the outline of a rectangle with rounded corners: negative inside.
float roundedBoxDistance(float2 point, float2 halfSize, float radius)
{
    const float2 corner = abs(point) - halfSize + radius;
    return length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - radius;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 color = input.color;
    if (ui.kind == kindText)
    {
        // The atlas holds the distance to the outline, with the outline itself at one half. A
        // pixel of the screen covers 0.5 / sharpness of that range, which is how far the edge is
        // softened.
        const float distance = sampleTexture(ui.texture, input.uv).r;
        const float softness = 0.5 / max(ui.sharpness, 0.0001);
        color.a *= smoothstep(0.5 - softness, 0.5 + softness, distance);
    }
    else
    {
        color *= sampleTexture(ui.texture, input.uv);
        if (ui.kind == kindRoundedQuad)
        {
            const float2 middle = (ui.rect.xy + ui.rect.zw) * 0.5;
            const float2 halfSize = (ui.rect.zw - ui.rect.xy) * 0.5;
            const float radius = min(ui.radius, min(halfSize.x, halfSize.y));
            const float distance = roundedBoxDistance(input.pixel - middle, halfSize, radius);
            // One pixel of softening around the outline.
            color.a *= 1.0 - smoothstep(-0.5, 0.5, distance);
        }
    }

    if (color.a <= 0.0)
    {
        discard;
    }
    return color;
}
