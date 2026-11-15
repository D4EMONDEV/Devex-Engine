// Draws particles and ribbons for WebGPU, compiled to WGSL, as particle.slang does for Vulkan: quads that face the
// camera, lengthen along their motion or lie flat, and ribbons that follow the points of a trail, among the blended
// surfaces. Their colours are relative to the exposure, unless they are lit as a matte surface by the light of the
// scene. Where they meet a surface they fade, from the depth of the scene, which the pass reads without writing it.
// Colours leave premultiplied: additive ones leave an alpha of zero, which the blending adds.

import surface_shading;
import particle_shape;
import webgpu_scene;

struct ParticleDraw
{
    // The first particle or segment of the draw: instances count from it.
    uint first;
    uint facing;
    uint flags;
    // 1 when the draw reads its texture.
    uint textured;
    // Columns and rows of frames in the texture.
    float2 sheet;
    // Meters over which particles fade into the surfaces they meet; 0 for none.
    float softness;
    uint padding0;
};

[[vk::binding(0, 1)]]
StructuredBuffer<Particle> particles;
[[vk::binding(1, 1)]]
StructuredBuffer<TrailPoint> points;
// Ribbons: the index of the point each segment starts at; it ends at the next one.
[[vk::binding(2, 1)]]
StructuredBuffer<uint> segments;
[[vk::binding(3, 1)]]
Texture2D particleTexture;
[[vk::binding(4, 1)]]
SamplerState particleSampler;
[[vk::binding(5, 1)]]
DepthTexture2D sceneDepth;
[[vk::binding(0, 2)]]
ConstantBuffer<ParticleDraw> draw;

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
    const Particle particle = particles[draw.first + instanceIndex];
    // The rows of the view are the axes of the camera in the world.
    const ParticleCorner corner = particleQuad(particle, vertexIndex, scene.view[0].xyz, scene.view[1].xyz, scene.cameraPosition,
                                               draw.facing, draw.sheet);
    return finish(corner.world, particle.color, corner.uv, corner.local);
}

[shader("vertex")]
VertexOutput ribbonVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const float2 corner = corners[vertexIndex];
    // The segment runs from its point to the next one.
    const uint index = segments[draw.first + instanceIndex] + (corner.y > 0.0 ? 1u : 0u);
    const TrailPoint point = points[index];
    const float3 toCamera = normalize(scene.cameraPosition - point.position);
    const float3 side = across(point.direction, toCamera, scene.view[0].xyz);
    const float3 world = point.position + side * (point.width * 0.5 * corner.x);
    return finish(world, point.color, float2(point.u, corner.x * 0.5 + 0.5), float2(0.0, corner.x));
}

// What leaves a fragment of colour `color`, premultiplied: faded where it meets a surface, and lit as a matte surface
// when the draw is lit. Additive ones leave an alpha of zero.
float4 shadeParticle(float4 color, VertexOutput input)
{
    if (draw.softness > 0.0)
    {
        const float depth = sceneDepth.Load(int3(int2(input.position.xy), 0));
        if (depth > 0.0)
        {
            // The surface behind, from the depth of the scene: WebGPU's clip space points Y up.
            uint width;
            uint height;
            sceneDepth.GetDimensions(width, height);
            const float2 uv = input.position.xy / float2(float(width), float(height));
            const float4 world = mul(scene.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
            const float3 surface = world.xyz / world.w;
            const float surfaceDepth = -mul(scene.view, float4(surface, 1.0)).z;
            color.a *= saturate((surfaceDepth - input.viewDepth) / draw.softness);
        }
    }
    float3 luminance = color.rgb;
    if ((draw.flags & flagLit) != 0)
    {
        // A matte surface turned towards the viewer.
        const float3 toCamera = normalize(scene.cameraPosition - input.worldPosition);
        Surface surface;
        surface.position = input.worldPosition;
        surface.normal = toCamera;
        surface.view = toCamera;
        surface.diffuseColor = color.rgb;
        surface.f0 = float3(0.0);
        surface.roughness = 1.0;
        surface.alpha = 1.0;
        surface.occlusion = 1.0;
        surface.viewDistance = input.viewDepth;
        surface.fragmentCoordinate = input.position.xy;
        luminance = surfaceLight(surface) * scene.exposure;
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, (draw.flags & flagAdditive) != 0 ? 0.0 : alpha);
}

[shader("fragment")]
float4 particleFragment(VertexOutput input) : SV_Target
{
    // The texture is read by every pixel, as WGSL wants it, and kept by those that have one.
    const float4 sampled = particleTexture.Sample(particleSampler, input.uv);
    return shadeParticle(particleTint(input.color, sampled, draw.textured != 0, input.local, false), input);
}

[shader("fragment")]
float4 ribbonFragment(VertexOutput input) : SV_Target
{
    const float4 sampled = particleTexture.Sample(particleSampler, input.uv);
    return shadeParticle(particleTint(input.color, sampled, draw.textured != 0, input.local, true), input);
}
