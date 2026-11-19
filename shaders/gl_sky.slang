// Draws the environment behind the scene for OpenGL, translated to GLSL ES 3.00 and GLSL 3.30, as webgpu_sky.slang does:
// the sky texture, or white, times the colour and the intensity of the environment, exposed. The fog comes with the 3D.
// It covers the whole image before the sprites are drawn over it.

import surface_shading;

struct SkyData
{
    float4x4 skyInverseViewProjection;
    // The colour of the environment times its intensity.
    float4 sky;
    float exposure;
    // How far the environment turns around Y, in radians.
    float rotation;
    float padding0;
    float padding1;
};

ConstantBuffer<SkyData> skyData;
Sampler2D skyTexture;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
VertexOutput skyVertex(uint vertexIndex : SV_VulkanVertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.ndc = uv * 2.0 - 1.0;
    output.position = float4(output.ndc, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 skyFragment(VertexOutput input) : SV_Target
{
    const float4 world = mul(skyData.skyInverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(world.xyz / world.w);
    const float3 lookup = rotateAroundY(direction, -skyData.rotation);
    const float3 radiance = skyTexture.SampleLevel(equirectangularUv(lookup), 0.0).rgb * skyData.sky.rgb;
    // Half floats end at 65504: the disk of the sun stays below.
    return float4(min(radiance * skyData.exposure, float3(60000.0)), 1.0);
}
