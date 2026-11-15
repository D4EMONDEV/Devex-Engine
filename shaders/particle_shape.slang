// What a particle and a point of a trail are, the quads that face the camera and their colour, shared by the
// particles of Vulkan (particles.slang) and of WebGPU (webgpu_particle.slang): plain arithmetic, without resources.
module particle_shape;

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

// A corner of the quad of a particle, for a camera whose axes in the world are `right` and `up`: facing it, stretched
// along its motion, or lying flat, turned, and over its frame of a sheet of `sheet` columns and rows.
public ParticleCorner particleQuad(Particle particle, uint vertexIndex, float3 right, float3 up, float3 cameraPosition, uint facing,
                                   float2 sheet)
{
    const float2 corner = corners[vertexIndex];
    const float3 toCamera = normalize(cameraPosition - particle.position);

    float3 center = particle.position;
    float3 axisX = right;
    float3 axisY = up;
    float2 halfSize = float2(particle.size * 0.5);
    float rotation = particle.rotation;
    if (facing == facingStretched)
    {
        // From its tail to its head, as wide as it is and as long as its motion.
        const float length = sqrt(dot(particle.stretch, particle.stretch));
        axisY = length > 1e-5 ? particle.stretch / length : up;
        axisX = across(axisY, toCamera, right);
        center -= particle.stretch * 0.5;
        halfSize.y += length * 0.5;
        rotation = 0.0;
    }
    else if (facing == facingHorizontal)
    {
        axisX = float3(1.0, 0.0, 0.0);
        axisY = float3(0.0, 0.0, -1.0);
    }
    else if (facing == facingVertical)
    {
        axisY = float3(0.0, 1.0, 0.0);
        axisX = across(axisY, toCamera, right);
    }

    const float c = cos(rotation);
    const float s = sin(rotation);
    const float2 turned = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c) * halfSize;

    // The frame of the sheet, the top-left one first.
    const uint columns = max(uint(sheet.x), 1u);
    const uint frame = uint(max(particle.frame, 0.0));
    const float2 cell = float2(float(frame % columns), float(frame / columns));
    ParticleCorner result;
    result.world = center + axisX * turned.x + axisY * turned.y;
    result.uv = (cell + float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5)) / max(sheet, float2(1.0));
    result.local = corner;
    return result;
}

// The colour of a particle or a ribbon at a fragment, straight alpha: its own times what its texture holds there
// (`sampled`, when `textured`), or a soft disc for a particle without one; ribbons soften along their edges.
public float4 particleTint(float4 color, float4 sampled, bool textured, float2 local, bool ribbon)
{
    if (textured)
    {
        color *= sampled;
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
