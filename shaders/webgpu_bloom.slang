// Bloom for WebGPU, compiled to WGSL, with the filters of bloom.slang: the image is halved a few times, each step
// blurring it a little, then added back up from the smallest to the largest. Each step reads one image, bound with
// its settings.

import bloom_filter;

struct BloomData
{
    // One texel of the image being read.
    float texelWidth;
    float texelHeight;
    // The threshold of the first step, or what an upsampling step adds.
    float strength;
    float padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<BloomData> bloom;
[[vk::binding(1, 0)]]
Texture2D sourceTexture;
[[vk::binding(2, 0)]]
SamplerState clampSampler;

struct BoundImage : IBloomSource
{
    float3 read(float2 uv)
    {
        return sourceTexture.Sample(clampSampler, uv).rgb;
    }
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput bloomVertex(uint vertexIndex : SV_VertexID)
{
    // One triangle over the image: texture coordinates go down as WebGPU's clip space goes up.
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.uv = uv;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 thresholdFragment(VertexOutput input) : SV_Target
{
    BoundImage image;
    return float4(bloomThreshold(image, input.uv, float2(bloom.texelWidth, bloom.texelHeight), bloom.strength), 1.0);
}

[shader("fragment")]
float4 downsampleFragment(VertexOutput input) : SV_Target
{
    BoundImage image;
    return float4(bloomDownsample(image, input.uv, float2(bloom.texelWidth, bloom.texelHeight)), 1.0);
}

[shader("fragment")]
float4 upsampleFragment(VertexOutput input) : SV_Target
{
    BoundImage image;
    return float4(bloomUpsample(image, input.uv, float2(bloom.texelWidth, bloom.texelHeight), bloom.strength), 1.0);
}
