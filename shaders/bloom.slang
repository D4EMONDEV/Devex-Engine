// Bloom: the halo that very bright things leave around them. The image is halved a few times, each
// step blurring it a little, then added back up from the smallest to the largest. What comes out is
// a wide, soft glow that the tonemapping adds to the image.

import common;

struct BloomData
{
    // The level of the chain this pass reads.
    uint source;
    // One texel of the image being read.
    float texelWidth;
    float texelHeight;
    // Multiplies what an upsampling step adds.
    float strength;
};

[[vk::push_constant]]
ConstantBuffer<BloomData> bloom;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VertexOutput output;
    output.uv = fullscreenUv(vertexIndex);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float3 read(float2 uv)
{
    return bloomChain[bloom.source].Sample(clampSampler, uv).rgb;
}

// The first step reads the image itself and keeps what is brighter than the threshold, easing into
// it so that a surface crossing the threshold does not pop.
[shader("fragment")]
float4 thresholdFragment(VertexOutput input) : SV_Target
{
    const float2 texel = float2(bloom.texelWidth, bloom.texelHeight);
    float3 color = float3(0.0);
    // Four taps halve the image at the same time, which costs nothing here.
    color += resolvedTexture.Sample(clampSampler, input.uv + float2(-0.5, -0.5) * texel).rgb;
    color += resolvedTexture.Sample(clampSampler, input.uv + float2(0.5, -0.5) * texel).rgb;
    color += resolvedTexture.Sample(clampSampler, input.uv + float2(-0.5, 0.5) * texel).rgb;
    color += resolvedTexture.Sample(clampSampler, input.uv + float2(0.5, 0.5) * texel).rgb;
    color *= 0.25;

    const float brightness = max(color.r, max(color.g, color.b));
    const float threshold = bloom.strength;
    const float knee = max(threshold * 0.5, 1e-4);
    // Below the threshold nothing glows; just above it, the glow comes in gently.
    const float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    const float weight = max(max(brightness - threshold, soft * soft / (4.0 * knee)), 0.0) /
                         max(brightness, 1e-4);
    return float4(color * weight, 1.0);
}

// Thirteen taps arranged in a square and a diamond, which halve an image without the shimmer a
// plain box filter leaves behind.
[shader("fragment")]
float4 downsampleFragment(VertexOutput input) : SV_Target
{
    const float2 texel = float2(bloom.texelWidth, bloom.texelHeight);
    const float3 middle = read(input.uv);
    const float3 corners = read(input.uv + float2(-2.0, -2.0) * texel) +
                           read(input.uv + float2(2.0, -2.0) * texel) +
                           read(input.uv + float2(-2.0, 2.0) * texel) +
                           read(input.uv + float2(2.0, 2.0) * texel);
    const float3 sides = read(input.uv + float2(-2.0, 0.0) * texel) +
                         read(input.uv + float2(2.0, 0.0) * texel) +
                         read(input.uv + float2(0.0, -2.0) * texel) +
                         read(input.uv + float2(0.0, 2.0) * texel);
    const float3 inner = read(input.uv + float2(-1.0, -1.0) * texel) +
                         read(input.uv + float2(1.0, -1.0) * texel) +
                         read(input.uv + float2(-1.0, 1.0) * texel) +
                         read(input.uv + float2(1.0, 1.0) * texel);
    const float3 color = middle * 0.125 + corners * 0.03125 + sides * 0.0625 + inner * 0.125;
    return float4(color, 1.0);
}

// A tent of nine taps, added over the level above.
[shader("fragment")]
float4 upsampleFragment(VertexOutput input) : SV_Target
{
    const float2 texel = float2(bloom.texelWidth, bloom.texelHeight);
    float3 color = read(input.uv) * 4.0;
    color += (read(input.uv + float2(-1.0, 0.0) * texel) + read(input.uv + float2(1.0, 0.0) * texel) +
              read(input.uv + float2(0.0, -1.0) * texel) + read(input.uv + float2(0.0, 1.0) * texel)) *
             2.0;
    color += read(input.uv + float2(-1.0, -1.0) * texel) + read(input.uv + float2(1.0, -1.0) * texel) +
             read(input.uv + float2(-1.0, 1.0) * texel) + read(input.uv + float2(1.0, 1.0) * texel);
    return float4(color / 16.0 * bloom.strength, 1.0);
}
