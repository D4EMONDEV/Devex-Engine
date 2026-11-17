// Draws a texture into a rectangle of another for WebGPU, compiled to WGSL: the atlas of the decals, filled with their
// textures, each at the mip level that fits its rectangle.

struct BlitData
{
    // Where it goes in the image drawn into, from 0 to 1 across, and the size of that image in pixels.
    float4 rect;
    float targetWidth;
    float targetHeight;
    float padding1;
    float padding2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<BlitData> blit;
[[vk::binding(1, 0)]]
Texture2D sourceImage;
[[vk::binding(2, 0)]]
SamplerState linearSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput blitVertex(uint vertexIndex : SV_VertexID)
{
    // Two triangles over the rectangle.
    const uint corners[6] = { 0, 1, 2, 2, 1, 3 };
    const uint corner = corners[vertexIndex];
    const float2 uv = float2(float(corner & 1), float(corner >> 1));
    const float2 at = blit.rect.xy + uv * blit.rect.zw;
    VertexOutput output;
    output.uv = uv;
    output.position = float4(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 blitFragment(VertexOutput input) : SV_Target
{
    uint width;
    uint height;
    uint levels;
    sourceImage.GetDimensions(0, width, height, levels);
    // The level whose texels are as large as the pixels of the rectangle.
    const float2 drawn = max(blit.rect.zw * float2(blit.targetWidth, blit.targetHeight), float2(1.0));
    const float level = clamp(log2(max(float(width) / drawn.x, float(height) / drawn.y)), 0.0, float(levels) - 1.0);
    return sourceImage.SampleLevel(linearSampler, input.uv, level);
}
