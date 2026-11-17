// Draws particles and ribbons for WebGPU, compiled to WGSL, as particle.slang does for Vulkan: quads that face the
// camera, lengthen along their motion or lie flat, and ribbons that follow the points of a trail, among the blended
// surfaces. Their colours are relative to the exposure, unless they are lit as a matte surface by the light of the
// scene. Where they meet a surface they fade, from the depth of the scene, which the pass reads without writing it.
// What they read is in webgpu_particles.slang. Colours leave premultiplied: additive ones leave an alpha of zero, which
// the blending adds.

import webgpu_scene;
import webgpu_particles;

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
VertexOutput particleVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const Particle particle = particles[particleDraw.first + instanceIndex];
    const ParticleCorner corner = particleCorner(particle, vertexIndex);
    return finish(corner.world, particle.color, corner.uv, corner.local);
}

[shader("vertex")]
VertexOutput ribbonVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const float2 corner = corners[vertexIndex];
    // The segment runs from its point to the next one.
    const uint index = segments[particleDraw.first + instanceIndex] + (corner.y > 0.0 ? 1u : 0u);
    const TrailPoint point = points[index];
    const float3 toCamera = normalize(scene.cameraPosition - point.position);
    const float3 side = across(point.direction, toCamera, scene.view[0].xyz);
    const float3 world = point.position + side * (point.width * 0.5 * corner.x);
    return finish(world, point.color, float2(point.u, corner.x * 0.5 + 0.5), float2(0.0, corner.x));
}

[shader("fragment")]
float4 particleFragment(VertexOutput input) : SV_Target
{
    return shadeParticle(particleColor(input.color, input.uv, input.local, false), input.worldPosition, input.viewDepth,
                         input.position, false, false);
}

[shader("fragment")]
float4 ribbonFragment(VertexOutput input) : SV_Target
{
    return shadeParticle(particleColor(input.color, input.uv, input.local, true), input.worldPosition, input.viewDepth,
                         input.position, false, false);
}
