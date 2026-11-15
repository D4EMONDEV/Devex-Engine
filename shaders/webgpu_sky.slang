// Draws the environment behind the scene for WebGPU, compiled to WGSL, as sky.slang does without the air (after the
// 1.0): the sky texture, or white, times the colour and the intensity of the environment, exposed, and veiled by the fog
// near the horizon. It covers the whole image before the meshes and the sprites are drawn over it, and reads the scene
// of webgpu_scene.slang.

import surface_shading;
import webgpu_scene;

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
    const float4 world = mul(scene.skyInverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(world.xyz / world.w);
    const float3 lookup = rotateAroundY(direction, -scene.environmentRotation);
    const float3 radiance = skyTexture.SampleLevel(skySampler, equirectangularUv(lookup), 0.0).rgb * scene.sky;
    float3 color = radiance * scene.exposure;
    // The fog veils the sky near the horizon as it veils what is far away.
    if (scene.fogDensity > 0.0)
    {
        const float opacity = heightFogOpacity(scene.cameraPosition, direction, skyFogDistance, scene.fogStart, scene.fogDensity,
                                               scene.fogFalloff, scene.fogHeight, scene.fogMaxOpacity);
        color = lerp(color, fogLight(direction) * scene.exposure, opacity);
    }
    // Half floats end at 65504: the disk of the sun stays below.
    return float4(min(color, float3(60000.0)), 1.0);
}
