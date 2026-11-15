// What particles and ribbons are drawn with: the particles of the frame, the points of the trails,
// the quads that face the camera and their shading. The particle shader draws them, and the particles
// shaders of projects build on it.
module particles;

__exported import particle_shape;

import common;
import pbr;

public struct ParticleData
{
    public SceneData* scene;
    public Particle* particles;
    public TrailPoint* points;
    // Ribbons: the index of the point each segment starts at; it ends at the next one.
    public uint* segments;
    // The first particle or segment of the batch: instances count from it.
    public uint first;
    public uint facing;
    public uint flags;
    // Slot of the texture in the bindless array, or noTexture.
    public uint texture;
    // Columns and rows of frames in the texture.
    public float2 sheet;
    // Meters over which particles fade into the surfaces they meet; 0 for none.
    public float softness;
    // The material whose shader draws the particles; its uniforms, for a shader of the project.
    public uint material;
};

[[vk::push_constant]]
public ConstantBuffer<ParticleData> data;

public ParticleCorner particleCorner(Particle particle, uint vertexIndex)
{
    let scene = data.scene;
    // The rows of the view are the axes of the camera in the world.
    return particleQuad(particle, vertexIndex, scene->view[0].xyz, scene->view[1].xyz, scene->cameraPosition, data.facing,
                        data.sheet);
}

// The colour of a particle or a ribbon at a fragment, straight alpha: its own times its texture, or
// a soft disc for a particle without one; ribbons soften along their edges.
public float4 particleColor(float4 color, float2 uv, float2 local, bool ribbon)
{
    const bool textured = data.texture != noTexture;
    return particleTint(color, textured ? sampleTexture(data.texture, uv) : float4(1.0), textured, local, ribbon);
}

// What leaves a fragment of colour `color`, premultiplied: faded where it meets a surface, and lit
// as a matte surface when the batch is lit unless `unshaded`. Additive ones leave an alpha of zero.
public float4 shadeParticle(float4 color, float3 worldPosition, float viewDepth, float4 fragmentPosition, bool unshaded,
                            bool additive)
{
    let scene = data.scene;
    if (data.softness > 0.0)
    {
        const float depth = sceneDepth.Load(int3(int2(fragmentPosition.xy), 0)).r;
        if (depth > 0.0)
        {
            const float2 uv = fragmentPosition.xy / float2(scene->viewportWidth, scene->viewportHeight);
            const float3 surface = worldFromSceneDepth(scene, uv, depth);
            const float surfaceDepth = -mul(scene->view, float4(surface, 1.0)).z;
            color.a *= saturate((surfaceDepth - viewDepth) / data.softness);
        }
    }

    float3 luminance = color.rgb;
    if ((data.flags & flagLit) != 0 && !unshaded)
    {
        // A matte surface turned towards the viewer.
        const float3 toCamera = normalize(scene->cameraPosition - worldPosition);
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
        luminance = shadeSurface(scene, surface) * scene->exposure;
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, additive || (data.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
