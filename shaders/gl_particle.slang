// Draws particles and ribbons for OpenGL, translated into GLSL ES 3.00 and GLSL 3.30, as webgpu_particle.slang does for
// WebGPU: quads that face the camera, lengthen along their motion or lie flat, and ribbons that follow the points of a
// trail, among the blended surfaces. What they read is in gl_particles.slang. Their colours are relative to the exposure,
// unless they are lit as a matte surface by the light of the scene. Colours leave premultiplied: additive ones leave an
// alpha of zero, which the blending adds.

import gl_particles;
import gl_scene;

// A segment of a ribbon: the render::GpuTrailPoint it starts at, then the one it ends at.
struct RibbonInput
{
    [[vk::location(0)]] float4 positionAndWidth0 : POSITION0;
    [[vk::location(1)]] float4 color0 : COLOR0;
    [[vk::location(2)]] float4 directionAndU0 : TEXCOORD0;
    [[vk::location(3)]] float4 positionAndWidth1 : POSITION1;
    [[vk::location(4)]] float4 color1 : COLOR1;
    [[vk::location(5)]] float4 directionAndU1 : TEXCOORD1;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    // Where the fragment is in its quad, from -1 to 1, for the soft disc and the edges of ribbons.
    float2 local : TEXCOORD1;
    float viewDepth : TEXCOORD2;
};

VertexOutput finish(float3 world, float4 color, float2 uv, float2 local)
{
    VertexOutput output;
    output.position = mul(scene.viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.color = color;
    output.uv = uv;
    output.local = local;
    output.viewDepth = -mul(scene.view, float4(world, 1.0)).z;
    return output;
}

[shader("vertex")]
VertexOutput particleVertex(ParticleInput input, uint vertexIndex : SV_VulkanVertexID)
{
    const Particle particle = particleOf(input);
    const ParticleCorner corner = particleCorner(particle, vertexIndex);
    return finish(corner.world, particle.color, corner.uv, corner.local);
}

[shader("vertex")]
VertexOutput ribbonVertex(RibbonInput input, uint vertexIndex : SV_VulkanVertexID)
{
    const float2 corner = corners[vertexIndex];
    // The segment runs from its first point to the next one.
    const bool second = corner.y > 0.0;
    const float4 positionAndWidth = second ? input.positionAndWidth1 : input.positionAndWidth0;
    const float4 directionAndU = second ? input.directionAndU1 : input.directionAndU0;
    const float3 toCamera = normalize(scene.cameraPosition - positionAndWidth.xyz);
    const float3 side = across(directionAndU.xyz, toCamera, scene.view[0].xyz);
    const float3 world = positionAndWidth.xyz + side * (positionAndWidth.w * 0.5 * corner.x);
    return finish(world, second ? input.color1 : input.color0, float2(directionAndU.w, corner.x * 0.5 + 0.5), float2(0.0, corner.x));
}

[shader("fragment")]
float4 particleFragment(VertexOutput input) : SV_Target
{
    return shadeParticle(particleColor(input.color, input.uv, input.local, false), input.worldPosition, input.viewDepth, input.position,
                         false, false);
}

[shader("fragment")]
float4 ribbonFragment(VertexOutput input) : SV_Target
{
    return shadeParticle(particleColor(input.color, input.uv, input.local, true), input.worldPosition, input.viewDepth, input.position,
                         false, false);
}
