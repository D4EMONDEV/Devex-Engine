// Draws the lines and triangles of the tools over the tonemapped scene, such as grids, light shapes
// and gizmos, and outlines the selected objects. Colors are linear, blended over the display image.

import common;

public struct OverlayVertex
{
    public float3 position;
    public float padding;
    public float4 color;
};

struct OverlayData
{
    SceneData* scene;
    OverlayVertex* vertices;
    // Color of the outlines.
    float4 outlineColor;
    // Multiplies clip space depth: above 1, lines lying on a surface stay in front of it.
    float depthScale;
    float padding0;
    float padding1;
    float padding2;
};

[[vk::push_constant]]
ConstantBuffer<OverlayData> overlay;

struct ColorVertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
ColorVertexOutput colorVertex(uint vertexIndex : SV_VertexID)
{
    const OverlayVertex vertex = overlay.vertices[vertexIndex];
    ColorVertexOutput output;
    output.position = mul(overlay.scene->viewProjection, float4(vertex.position, 1.0));
    output.position.z *= overlay.depthScale;
    output.color = vertex.color;
    return output;
}

[shader("fragment")]
float4 colorFragment(ColorVertexOutput input) : SV_Target
{
    return input.color;
}

struct FullscreenOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
FullscreenOutput outlineVertex(uint vertexIndex : SV_VertexID)
{
    FullscreenOutput output;
    output.position = float4(fullscreenUv(vertexIndex) * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Pixels outside the selection mask within two pixels of it form the outline.
[shader("fragment")]
float4 outlineFragment(FullscreenOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    if (selectionMask.Load(int3(pixel, 0)).r > 0.5)
    {
        discard;
    }

    uint width;
    uint height;
    selectionMask.GetDimensions(width, height);
    const int2 last = int2(width, height) - 1;
    float coverage = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const int2 neighbor = clamp(pixel + int2(x, y), int2(0), last);
            coverage = max(coverage, selectionMask.Load(int3(neighbor, 0)).r);
        }
    }
    if (coverage < 0.5)
    {
        discard;
    }
    return overlay.outlineColor;
}
