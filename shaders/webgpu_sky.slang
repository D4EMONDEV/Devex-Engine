// Draws the environment behind the scene for WebGPU, compiled to WGSL, as sky.slang does without the air and the
// fog (web step 3c): the sky texture, or white, times the colour and the intensity of the environment, exposed. It
// covers the whole image before the meshes and the sprites are drawn over it.

import surface_shading;

struct SkyData
{
    float4x4 skyInverseViewProjection;
    float3 sky;
    float exposure;
    float environmentRotation;
    float padding0;
    float padding1;
    float padding2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SkyData> sky;
[[vk::binding(1, 0)]]
Texture2D skyTexture;
[[vk::binding(2, 0)]]
SamplerState skySampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
VertexOutput skyVertex(uint vertexIndex : SV_VertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.ndc = uv * 2.0 - 1.0;
    // Depth 0 is infinitely far away with reversed depth.
    output.position = float4(output.ndc, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 skyFragment(VertexOutput input) : SV_Target
{
    const float4 world = mul(sky.skyInverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(world.xyz / world.w);
    const float3 lookup = rotateAroundY(direction, -sky.environmentRotation);
    const float3 radiance = skyTexture.SampleLevel(skySampler, equirectangularUv(lookup), 0.0).rgb * sky.sky;
    // Half floats end at 65504: the disk of the sun stays below.
    return float4(min(radiance * sky.exposure, float3(60000.0)), 1.0);
}
