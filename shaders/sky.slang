// Draws the environment behind the scene, where no geometry was written to depth.

import common;

struct SkyData
{
    SceneData* scene;
};

[[vk::push_constant]]
ConstantBuffer<SkyData> sky;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    const float2 uv = fullscreenUv(vertexIndex);
    VertexOutput output;
    output.ndc = uv * 2.0 - 1.0;
    // Depth 0 is infinitely far away with reversed depth.
    output.position = float4(output.ndc, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    let scene = sky.scene;
    const float4 world = mul(scene->skyInverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(world.xyz / world.w);
    const float3 lookup = rotateAroundY(direction, -scene->environmentRotation);
    const float3 radiance = skyTexture.SampleLevel(skySampler, equirectangularUv(lookup), 0.0).rgb;
    return float4(radiance * scene->environmentColor * scene->environmentIntensity * scene->exposure, 1.0);
}
