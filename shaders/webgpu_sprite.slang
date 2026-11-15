// Draws sprites and the tiles of tilemaps for WebGPU, compiled to WGSL: those of sprite.slang, lit by the 2D
// lights through their normal maps and hidden by the occluders whose distances their shadows keep, as
// canvas.slang lights them, without the 3D lights of lit sprites and the canvas_item shaders of projects yet. The
// sprites of the frame lie in a storage buffer; each draw covers a run of them that reads one texture and one normal
// map, bound with their samplers, and finds where its run starts at a dynamic offset of the draw buffer. They leave
// premultiplied: additive sprites leave an alpha of zero, which the blending adds.

import sprite_shape;

struct FrameData
{
    float4x4 viewProjection;
    // The tint of the canvas.
    float4 canvasModulate;
    uint lightCount;
    // How many distances each shadowed light keeps.
    uint shadowResolution;
    uint padding0;
    uint padding1;
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
[[vk::binding(3, 0)]]
StructuredBuffer<Light2D> lights2D;
[[vk::binding(4, 0)]]
StructuredBuffer<float> shadows2D;
[[vk::binding(0, 1)]]
Texture2D spriteTexture;
[[vk::binding(1, 1)]]
SamplerState spriteSampler;
[[vk::binding(2, 1)]]
Texture2D normalTexture;
[[vk::binding(3, 1)]]
SamplerState normalSampler;

// The 2D lights of the frame, from its storage buffers.
struct FrameLights2D : ILights2D
{
    uint count()
    {
        return frame.lightCount;
    }
    Light2D light(uint index)
    {
        return lights2D[index];
    }
    float shadow(int index)
    {
        return shadows2D[index];
    }
    uint shadowResolution()
    {
        return frame.shadowResolution;
    }
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
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
    output.worldPosition = world;
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
    float2 uv = float2(0.0);
    if (sprite.texture != noTexture)
    {
        uint width;
        uint height;
        spriteTexture.GetDimensions(width, height);
        uv = spriteUv(sprite, input.local, float2(float(width), float(height)));
        color *= spriteTexture.SampleGrad(spriteSampler, uv, slopeX, slopeY);
    }
    // Its normal map, where it has one, read where its texture is.
    const bool mapped = sprite.normalTexture != noTexture && sprite.texture != noTexture;
    float3 normal = float3(0.0, 0.0, 1.0);
    if (mapped)
    {
        normal = spriteNormalOf(sprite, normalTexture.SampleGrad(normalSampler, uv, slopeX, slopeY).rg * 2.0 - 1.0);
    }
    float3 luminance = color.rgb;
    if ((sprite.flags & flagUnshaded) == 0)
    {
        // The 2D plane: the tint of the canvas, and the light of the 2D lights over it.
        FrameLights2D lights;
        luminance = luminance * frame.canvasModulate.rgb +
                    color.rgb * spriteLight2D(lights, input.worldPosition, normal, mapped, sprite.lightMask);
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, (sprite.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
