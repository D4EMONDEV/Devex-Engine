// Bloom for OpenGL, translated to GLSL ES 3.00 and GLSL 3.30, as webgpu_bloom.slang does for WebGPU, with the filters of
// bloom.slang: the image is halved a few times, each step blurring it a little, then added back up from the smallest to
// the largest. Each step reads one image with its settings. Texture coordinates start from the first row stored, which
// OpenGL draws at the bottom, as it reads them.

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

ConstantBuffer<BloomData> bloom;
Sampler2D sourceTexture;

struct BoundImage : IBloomSource
{
    float3 read(float2 uv)
    {
        return sourceTexture.SampleLevel(uv, 0.0).rgb;
    }
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput bloomVertex(uint vertexIndex : SV_VulkanVertexID)
{
    // One triangle over the image.
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.uv = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
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
