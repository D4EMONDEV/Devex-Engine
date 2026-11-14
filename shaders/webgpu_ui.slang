// Draws the interface over the tonemapped scene for WebGPU, compiled to WGSL: the shapes of ui.slang,
// from real vertex and index buffers, each batch finding its shape at a dynamic offset of the draw
// buffer and its texture in the group bound with it. Positions arrive in pixels of the image, and
// colours are blended over it with straight alpha.

import ui_shape;

struct DrawData
{
    // The rectangle whose corners are rounded, in pixels.
    float4 rect;
    // Turns pixels into clip space: 2 / width and 2 / height.
    float2 inverseViewport;
    float radius;
    // How much of the stored distances one pixel of the screen covers, for the letters.
    float sharpness;
    uint kind;
    // 1 when the image keeps the colours of the display rather than light.
    uint displaySpace;
    uint padding0;
    uint padding1;
};

[[vk::binding(0, 0)]]
ConstantBuffer<DrawData> draw;
[[vk::binding(0, 1)]]
Texture2D uiTexture;
[[vk::binding(1, 1)]]
SamplerState uiSampler;

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    // Where the pixel is in the image, which the rounded corners measure from.
    float2 pixel : TEXCOORD1;
};

[shader("vertex")]
VertexOutput uiVertex(VertexInput vertex)
{
    VertexOutput output;
    // Pixels go down from the top; WebGPU's clip space goes up.
    const float2 clip = vertex.position * draw.inverseViewport - 1.0;
    output.position = float4(clip.x, -clip.y, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = vertex.color;
    output.pixel = vertex.position;
    return output;
}

[shader("fragment")]
float4 uiFragment(VertexOutput input) : SV_Target
{
    // Every pixel samples, where WGSL lets a sample take its slopes; the shapes that read nothing
    // leave it aside.
    const float4 texel = uiTexture.Sample(uiSampler, input.uv);
    const float4 sampled = uiSamples(draw.kind) ? texel : float4(1.0);
    float4 color = shadeUi(draw.kind, draw.rect, draw.radius, draw.sharpness, input.color, input.pixel, sampled);
    if (color.a <= 0.0)
    {
        discard;
    }
    if (draw.displaySpace != 0)
    {
        color.rgb = encodeDisplay(color.rgb);
    }
    return color;
}
