// What particles and ribbons are drawn with for OpenGL, translated into GLSL with the shaders that import it, as
// webgpu_particles.slang is for WebGPU: the draw, its texture, the depth of the scene they fade into, the fields of a
// particle as attributes, and their colour and light. gl_particle.slang draws them, and the particles shaders of projects
// build on it. OpenGL ES 3.0 has no storage buffers: each particle is an instance whose fields arrive as attributes, and
// each segment of a ribbon an instance that brings both its points. Where they meet a surface they fade, from a copy of
// the depth of the scene, which OpenGL cannot read while it draws into it.
module gl_particles;

__exported import particle_shape;
import gl_scene;
import surface_shading;

public struct ParticleDraw
{
    public uint facing;
    public uint flags;
    // 1 when the draw reads its texture.
    public uint textured;
    public uint padding0;
    // Columns and rows of frames in the texture.
    public float2 sheet;
    // Meters over which particles fade into the surfaces they meet; 0 for none.
    public float softness;
    public uint padding1;
    // The local lights that shine on a lit draw, a byte each, as an object has them.
    public uint2 lights;
    public uint padding2;
    public uint padding3;
};

public ConstantBuffer<ParticleDraw> particleDraw;
public Sampler2D particleTexture;
// The depth of the opaque surfaces, read texel by texel.
public Sampler2D sceneDepth;

// A render::GpuParticle, read as attributes.
public struct ParticleInput
{
    [[vk::location(0)]] public float4 positionAndSize : POSITION;
    [[vk::location(1)]] public float4 color : COLOR0;
    [[vk::location(2)]] public float4 stretchAndRotation : TEXCOORD0;
    [[vk::location(3)]] public float4 frame : TEXCOORD1;
};

// The particle the attributes of an instance hold.
public Particle particleOf(ParticleInput input)
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
    return particle;
}

// The corner of the quad of a particle, facing the camera as the draw asks.
public ParticleCorner particleCorner(Particle particle, uint vertexIndex)
{
    // The rows of the view are the axes of the camera in the world.
    return particleQuad(particle, vertexIndex, scene.view[0].xyz, scene.view[1].xyz, scene.cameraPosition, particleDraw.facing,
                        particleDraw.sheet);
}

// The colour of a particle or a ribbon at a fragment, straight alpha: its own times its texture, or a soft disc for a
// particle without one; ribbons soften along their edges.
public float4 particleColor(float4 color, float2 uv, float2 local, bool ribbon)
{
    const float4 sampled = particleTexture.Sample(uv);
    return particleTint(color, sampled, particleDraw.textured != 0, local, ribbon);
}

// What leaves a fragment of colour `color`, premultiplied: faded where it meets a surface, and lit as a matte surface when
// the draw is lit unless `unshaded`. Additive ones leave an alpha of zero.
public float4 shadeParticle(float4 color, float3 worldPosition, float viewDepth, float4 fragmentPosition, bool unshaded, bool additive)
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
    if ((particleDraw.flags & flagLit) != 0 && !unshaded)
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
    return float4(luminance * alpha, additive || (particleDraw.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
