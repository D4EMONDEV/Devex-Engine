// Bloom: the halo that very bright things leave around them. The image is halved a few times, each
// step blurring it a little, then added back up from the smallest to the largest. What comes out is
// a wide, soft glow that the tonemapping adds to the image.

import common;
import bloom_filter;

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

// The level of the chain this pass reads.
struct ChainLevel : IBloomSource
{
    float3 read(float2 uv)
    {
        return bloomChain[bloom.source].Sample(clampSampler, uv).rgb;
    }
};

// The image the first step reads.
struct ResolvedImage : IBloomSource
{
    float3 read(float2 uv)
    {
        return resolvedTexture.Sample(clampSampler, uv).rgb;
    }
};

[shader("fragment")]
float4 thresholdFragment(VertexOutput input) : SV_Target
{
    ResolvedImage image;
    return float4(bloomThreshold(image, input.uv, float2(bloom.texelWidth, bloom.texelHeight), bloom.strength), 1.0);
}

[shader("fragment")]
float4 downsampleFragment(VertexOutput input) : SV_Target
{
    ChainLevel level;
    return float4(bloomDownsample(level, input.uv, float2(bloom.texelWidth, bloom.texelHeight)), 1.0);
}

[shader("fragment")]
float4 upsampleFragment(VertexOutput input) : SV_Target
{
    ChainLevel level;
    return float4(bloomUpsample(level, input.uv, float2(bloom.texelWidth, bloom.texelHeight), bloom.strength), 1.0);
}
