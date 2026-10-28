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

// How far the fog is looked through towards the sky, in meters.
static const float skyFogDistance = 20000.0;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    let scene = sky.scene;
    const float4 world = mul(scene->skyInverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(world.xyz / world.w);
    float3 radiance;
    if ((scene->atmosphereFlags & atmosphereAir) != 0)
    {
        // The air around the camera and the sun through it, in nits.
        radiance = atmosphereSky(scene->atmosphere, direction) + atmosphereSunDisk(scene->atmosphere, direction);
    }
    else
    {
        const float3 lookup = rotateAroundY(direction, -scene->environmentRotation);
        radiance = skyTexture.SampleLevel(skySampler, equirectangularUv(lookup), 0.0).rgb * scene->environmentColor *
                   scene->environmentIntensity;
    }
    float3 color = radiance * scene->exposure;
    // The fog veils the sky near the horizon as it veils what is far away.
    if ((scene->atmosphereFlags & atmosphereFog) != 0)
    {
        const float opacity = fogOpacity(scene->atmosphere, scene->cameraPosition, direction, skyFogDistance);
        color = lerp(color, fogLight(scene, direction) * scene->exposure, opacity);
    }
    // Half floats end at 65504: the disk of the sun stays below.
    return float4(min(color, float3(60000.0)), 1.0);
}
