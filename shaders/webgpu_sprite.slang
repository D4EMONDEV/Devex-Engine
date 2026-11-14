// Draws sprites and the tiles of tilemaps for WebGPU, compiled to WGSL: those of sprite.slang, without
// the 2D lights, the 3D lights of lit sprites and the canvas_item shaders of projects yet. The sprites
// of the frame lie in a storage buffer; each draw covers a run of them that reads one texture, bound
// with its sampler, and finds where its run starts at a dynamic offset of the draw buffer. They leave
// premultiplied: additive sprites leave an alpha of zero, which the blending adds.

import sprite_shape;

struct FrameData
{
    float4x4 viewProjection;
    // The tint of the canvas.
    float4 canvasModulate;
};

struct DrawData
{
    // The first sprite of the draw: instances count from it.
    uint first;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]]
StructuredBuffer<Sprite> sprites;
[[vk::binding(2, 0)]]
ConstantBuffer<DrawData> draw;
[[vk::binding(0, 1)]]
Texture2D spriteTexture;
[[vk::binding(1, 1)]]
SamplerState spriteSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    // Meters from the bottom-left corner of the rectangle, before any flip.
    float2 local : TEXCOORD0;
    nointerpolation uint sprite : TEXCOORD1;
};

[shader("vertex")]
VertexOutput spriteVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const uint index = draw.first + instanceIndex;
    const Sprite sprite = sprites[index];
    const float2 local = corners[vertexIndex] * sprite.size;
    const float3 world = mul(sprite.world, float4(spritePlanePosition(sprite, local), 0.0, 1.0)).xyz;
    VertexOutput output;
    output.position = mul(frame.viewProjection, float4(world, 1.0));
    output.local = local;
    output.sprite = index;
    return output;
}

[shader("fragment")]
float4 spriteFragment(VertexOutput input) : SV_Target
{
    const Sprite sprite = sprites[input.sprite];
    // The slopes come first: WGSL takes them only where every pixel of a quad goes the same way.
    const float2 uvPerMeter = spriteUvPerMeter(sprite);
    const float2 slopeX = ddx(input.local) * uvPerMeter;
    const float2 slopeY = ddy(input.local) * uvPerMeter;
    float4 color = sprite.color;
    if (sprite.texture != noTexture)
    {
        uint width;
        uint height;
        spriteTexture.GetDimensions(width, height);
        const float2 uv = spriteUv(sprite, input.local, float2(float(width), float(height)));
        color *= spriteTexture.SampleGrad(spriteSampler, uv, slopeX, slopeY);
    }
    float3 luminance = color.rgb;
    if ((sprite.flags & flagUnshaded) == 0)
    {
        luminance *= frame.canvasModulate.rgb;
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, (sprite.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
