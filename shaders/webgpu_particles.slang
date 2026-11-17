// What particles and ribbons are drawn with for WebGPU, as particles.slang is for Vulkan: the particles of the frame,
// the points of the trails, the texture of a draw, the depth of the scene they fade into, and their shading.
// webgpu_particle.slang draws them, and the particles shaders of projects build on it. The scene is group 0, without
// the decals, which particles never read; the material of a shader of the project is group 1, empty for the others;
// the particles group 2; the draw group 3, at a dynamic offset.
module webgpu_particles;

__exported import particle_shape;
import surface_shading;
import webgpu_scene;

public struct ParticleDraw
{
    // The first particle or segment of the draw: instances count from it.
    public uint first;
    public uint facing;
    public uint flags;
    // 1 when the draw reads its texture.
    public uint textured;
    // Columns and rows of frames in the texture.
    public float2 sheet;
    // Meters over which particles fade into the surfaces they meet; 0 for none.
    public float softness;
    public uint padding0;
};

[[vk::binding(0, 2)]]
public StructuredBuffer<Particle> particles;
[[vk::binding(1, 2)]]
public StructuredBuffer<TrailPoint> points;
// Ribbons: the index of the point each segment starts at; it ends at the next one.
[[vk::binding(2, 2)]]
public StructuredBuffer<uint> segments;
[[vk::binding(3, 2)]]
public Texture2D particleTexture;
[[vk::binding(4, 2)]]
public SamplerState particleSampler;
[[vk::binding(5, 2)]]
public DepthTexture2D sceneDepth;
[[vk::binding(0, 3)]]
public ConstantBuffer<ParticleDraw> particleDraw;

// The corner of the quad of a particle, facing the camera as the draw asks.
public ParticleCorner particleCorner(Particle particle, uint vertexIndex)
{
    // The rows of the view are the axes of the camera in the world.
    return particleQuad(particle, vertexIndex, scene.view[0].xyz, scene.view[1].xyz, scene.cameraPosition, particleDraw.facing,
                        particleDraw.sheet);
}

// The colour of a particle or a ribbon at a fragment, straight alpha: its own times its texture, or a soft disc for a
// particle without one; ribbons soften along their edges. The texture is read by every pixel, as WGSL wants it, and
// kept by those that have one.
public float4 particleColor(float4 color, float2 uv, float2 local, bool ribbon)
{
    const float4 sampled = particleTexture.Sample(particleSampler, uv);
    return particleTint(color, sampled, particleDraw.textured != 0, local, ribbon);
}

// What leaves a fragment of colour `color`, premultiplied: faded where it meets a surface, and lit as a matte surface
// when the draw is lit unless `unshaded`. Additive ones leave an alpha of zero.
public float4 shadeParticle(float4 color, float3 worldPosition, float viewDepth, float4 fragmentPosition, bool unshaded,
                            bool additive)
{
    if (particleDraw.softness > 0.0)
    {
        const float depth = sceneDepth.Load(int3(int2(fragmentPosition.xy), 0));
        if (depth > 0.0)
        {
            // The surface behind, from the depth of the scene: WebGPU's clip space points Y up.
            uint width;
            uint height;
            sceneDepth.GetDimensions(width, height);
            const float2 uv = fragmentPosition.xy / float2(float(width), float(height));
            const float4 world = mul(scene.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
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
        luminance = surfaceLight(surface) * scene.exposure;
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, additive || (particleDraw.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
