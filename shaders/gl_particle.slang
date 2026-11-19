// Draws particles and ribbons for OpenGL, translated into GLSL ES 3.00 and GLSL 3.30, as webgpu_particle.slang does for
// WebGPU: quads that face the camera, lengthen along their motion or lie flat, and ribbons that follow the points of a
// trail, among the blended surfaces. OpenGL ES 3.0 has no storage buffers: each particle is an instance whose fields
// arrive as attributes, and each segment of a ribbon an instance that brings both its points. Their colours are relative
// to the exposure, unless they are lit as a matte surface by the light of the scene. Where they meet a surface they fade,
// from a copy of the depth of the scene, which OpenGL cannot read while it draws into it. Colours leave premultiplied:
// additive ones leave an alpha of zero, which the blending adds.

import gl_scene;
import particle_shape;
import surface_shading;

struct ParticleDraw
{
    uint facing;
    uint flags;
    // 1 when the draw reads its texture.
    uint textured;
    uint padding0;
    // Columns and rows of frames in the texture.
    float2 sheet;
    // Meters over which particles fade into the surfaces they meet; 0 for none.
    float softness;
    uint padding1;
    // The local lights that shine on a lit draw, a byte each, as an object has them.
    uint2 lights;
    uint padding2;
    uint padding3;
};

ConstantBuffer<ParticleDraw> particleDraw;
Sampler2D particleTexture;
// The depth of the opaque surfaces, read texel by texel.
Sampler2D sceneDepth;

// A render::GpuParticle, read as attributes.
struct ParticleInput
{
    [[vk::location(0)]] float4 positionAndSize : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float4 stretchAndRotation : TEXCOORD0;
    [[vk::location(3)]] float4 frame : TEXCOORD1;
};

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
    Particle particle;
    particle.position = input.positionAndSize.xyz;
    particle.size = input.positionAndSize.w;
    particle.color = input.color;
    particle.stretch = input.stretchAndRotation.xyz;
    particle.rotation = input.stretchAndRotation.w;
    particle.frame = input.frame.x;
    particle.padding0 = 0.0;
    particle.padding1 = 0.0;
    particle.padding2 = 0.0;
    // The rows of the view are the axes of the camera in the world.
    const ParticleCorner corner = particleQuad(particle, vertexIndex, scene.view[0].xyz, scene.view[1].xyz, scene.cameraPosition,
                                               particleDraw.facing, particleDraw.sheet);
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

// What leaves a fragment of colour `color`, premultiplied: faded where it meets a surface, and lit as a matte surface when
// the draw is lit. Additive ones leave an alpha of zero.
float4 shadeParticle(float4 color, float3 worldPosition, float viewDepth, float4 fragmentPosition)
{
    if (particleDraw.softness > 0.0)
    {
        const float depth = sceneDepth.Load(int3(int2(fragmentPosition.xy), 0)).r;
        if (depth > 0.0)
        {
            // The surface behind, from the depth of the scene: the clip space of OpenGL points Y up from the bottom row, and
            // its depth runs from -1 to 1.
            uint width;
            uint height;
            sceneDepth.GetDimensions(width, height);
            const float2 uv = fragmentPosition.xy / float2(float(width), float(height));
            const float4 world = mul(scene.inverseViewProjection, float4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0));
            const float3 surface = world.xyz / world.w;
            const float surfaceDepth = -mul(scene.view, float4(surface, 1.0)).z;
            color.a *= saturate((surfaceDepth - viewDepth) / particleDraw.softness);
        }
    }
    float3 luminance = color.rgb;
    if ((particleDraw.flags & flagLit) != 0)
    {
        // A matte surface turned towards the viewer.
        const float3 toCamera = normalize(scene.cameraPosition - worldPosition);
        Surface surface;
        surface.position = worldPosition;
        surface.normal = toCamera;
        surface.view = toCamera;
        surface.diffuseColor = color.rgb;
        surface.f0 = float3(0.0);
        surface.roughness = 1.0;
        surface.alpha = 1.0;
        surface.occlusion = 1.0;
        surface.viewDistance = viewDepth;
        surface.fragmentCoordinate = fragmentPosition.xy;
        luminance = surfaceLight(surface, particleDraw.lights) * scene.exposure;
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, (particleDraw.flags & flagAdditive) != 0 ? 0.0 : alpha);
}

[shader("fragment")]
float4 particleFragment(VertexOutput input) : SV_Target
{
    const float4 sampled = particleTexture.Sample(input.uv);
    const float4 color = particleTint(input.color, sampled, particleDraw.textured != 0, input.local, false);
    return shadeParticle(color, input.worldPosition, input.viewDepth, input.position);
}

[shader("fragment")]
float4 ribbonFragment(VertexOutput input) : SV_Target
{
    const float4 sampled = particleTexture.Sample(input.uv);
    const float4 color = particleTint(input.color, sampled, particleDraw.textured != 0, input.local, true);
    return shadeParticle(color, input.worldPosition, input.viewDepth, input.position);
}
