// What particles and ribbons are drawn with: the particles of the frame, the points of the trails,
// the quads that face the camera and their shading. The particle shader draws them, and the particles
// shaders of projects build on it.
module particles;

import common;
import pbr;

public struct Particle
{
    public float3 position;
    public float size;
    public float4 color;
    public float3 stretch;
    public float rotation;
    public float frame;
    public float padding0;
    public float padding1;
    public float padding2;
};

public struct TrailPoint
{
    public float3 position;
    public float width;
    public float4 color;
    public float3 direction;
    public float u;
};

public static const uint facingBillboard = 0;
public static const uint facingStretched = 1;
public static const uint facingHorizontal = 2;
public static const uint facingVertical = 3;
public static const uint flagAdditive = 1;
public static const uint flagLit = 2;
public static const uint noTexture = 0xFFFFFFFF;

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

// The two triangles of a quad.
public static const float2 corners[6] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                                          float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0) };

// A direction across `along` as seen from the camera, or any other when the camera looks along it.
public float3 across(float3 along, float3 toCamera, float3 fallback)
{
    const float3 side = cross(along, toCamera);
    const float length2 = dot(side, side);
    return length2 > 1e-10 ? side * rsqrt(length2) : fallback;
}

// A corner of the quad of a particle: where it lies in the world, where it samples the sheet of its
// texture, and where it is in its quad, from -1 to 1.
public struct ParticleCorner
{
    public float3 world;
    public float2 uv;
    public float2 local;
};

public ParticleCorner particleCorner(Particle particle, uint vertexIndex)
{
    let scene = data.scene;
    const float2 corner = corners[vertexIndex];
    // The rows of the view are the axes of the camera in the world.
    const float3 right = scene->view[0].xyz;
    const float3 up = scene->view[1].xyz;
    const float3 toCamera = normalize(scene->cameraPosition - particle.position);

    float3 center = particle.position;
    float3 axisX = right;
    float3 axisY = up;
    float2 halfSize = float2(particle.size * 0.5);
    float rotation = particle.rotation;
    if (data.facing == facingStretched)
    {
        // From its tail to its head, as wide as it is and as long as its motion.
        const float length = sqrt(dot(particle.stretch, particle.stretch));
        axisY = length > 1e-5 ? particle.stretch / length : up;
        axisX = across(axisY, toCamera, right);
        center -= particle.stretch * 0.5;
        halfSize.y += length * 0.5;
        rotation = 0.0;
    }
    else if (data.facing == facingHorizontal)
    {
        axisX = float3(1.0, 0.0, 0.0);
        axisY = float3(0.0, 0.0, -1.0);
    }
    else if (data.facing == facingVertical)
    {
        axisY = float3(0.0, 1.0, 0.0);
        axisX = across(axisY, toCamera, right);
    }

    const float c = cos(rotation);
    const float s = sin(rotation);
    const float2 turned = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c) * halfSize;

    // The frame of the sheet, the top-left one first.
    const uint columns = max(uint(data.sheet.x), 1u);
    const uint frame = uint(max(particle.frame, 0.0));
    const float2 cell = float2(float(frame % columns), float(frame / columns));
    ParticleCorner result;
    result.world = center + axisX * turned.x + axisY * turned.y;
    result.uv = (cell + float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5)) / max(data.sheet, float2(1.0));
    result.local = corner;
    return result;
}

// The colour of a particle or a ribbon at a fragment, straight alpha: its own times its texture, or
// a soft disc for a particle without one; ribbons soften along their edges.
public float4 particleColor(float4 color, float2 uv, float2 local, bool ribbon)
{
    if (data.texture != noTexture)
    {
        color *= sampleTexture(data.texture, uv);
    }
    else if (!ribbon)
    {
        // A soft disc, brightest in its middle.
        const float r2 = dot(local, local);
        const float fade = saturate(1.0 - r2);
        color.a *= fade * fade;
    }
    if (ribbon)
    {
        // Ribbons soften along their edges.
        color.a *= smoothstep(1.0, 0.6, abs(local.y));
    }
    return color;
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
            const float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
            const float4 surface = mul(scene->inverseViewProjection, clip);
            const float surfaceDepth = -mul(scene->view, float4(surface.xyz / surface.w, 1.0)).z;
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
