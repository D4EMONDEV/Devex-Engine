// Draws particles and ribbons over the scene, among its blended surfaces. Particles are quads that
// face the camera, lengthen along their motion or lie flat; ribbons follow the points of a trail
// and turn their width towards the camera. Their colours are relative to the exposure, as the
// emission of materials, unless they are lit as a matte surface. Where they meet a surface they
// fade instead of cutting through it, from the depth of the scene the pass tests without writing.
// Colours leave premultiplied: additive ones leave an alpha of zero, which the blending adds.

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

static const uint facingBillboard = 0;
static const uint facingStretched = 1;
static const uint facingHorizontal = 2;
static const uint facingVertical = 3;
static const uint flagAdditive = 1;
static const uint flagLit = 2;
static const uint noTexture = 0xFFFFFFFF;

struct ParticleData
{
    SceneData* scene;
    Particle* particles;
    TrailPoint* points;
    // Ribbons: the index of the point each segment starts at; it ends at the next one.
    uint* segments;
    // The first particle or segment of the batch: instances count from it.
    uint first;
    uint facing;
    uint flags;
    // Slot of the texture in the bindless array, or noTexture.
    uint texture;
    // Columns and rows of frames in the texture.
    float2 sheet;
    // Meters over which particles fade into the surfaces they meet; 0 for none.
    float softness;
    float padding;
};

[[vk::push_constant]]
ConstantBuffer<ParticleData> data;

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

// The two triangles of a quad.
static const float2 corners[6] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                                   float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0) };

VertexOutput finish(float3 world, float4 color, float2 uv, float2 local)
{
    let scene = data.scene;
    VertexOutput output;
    output.position = mul(scene->viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.color = color;
    output.uv = uv;
    output.local = local;
    output.viewDepth = -mul(scene->view, float4(world, 1.0)).z;
    return output;
}

// A direction across `along` as seen from the camera, or any other when the camera looks along it.
float3 across(float3 along, float3 toCamera, float3 fallback)
{
    const float3 side = cross(along, toCamera);
    const float length2 = dot(side, side);
    return length2 > 1e-10 ? side * rsqrt(length2) : fallback;
}

[shader("vertex")]
VertexOutput particleVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    let scene = data.scene;
    const Particle particle = data.particles[data.first + instanceIndex];
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
    const float3 world = center + axisX * turned.x + axisY * turned.y;

    // The frame of the sheet, the top-left one first.
    const uint columns = max(uint(data.sheet.x), 1u);
    const uint frame = uint(max(particle.frame, 0.0));
    const float2 cell = float2(float(frame % columns), float(frame / columns));
    const float2 uv = (cell + float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5)) / max(data.sheet, float2(1.0));
    return finish(world, particle.color, uv, corner);
}

[shader("vertex")]
VertexOutput ribbonVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    let scene = data.scene;
    const float2 corner = corners[vertexIndex];
    // The segment runs from its point to the next one.
    const uint index = data.segments[data.first + instanceIndex] + (corner.y > 0.0 ? 1u : 0u);
    const TrailPoint point = data.points[index];
    const float3 toCamera = normalize(scene->cameraPosition - point.position);
    const float3 side = across(point.direction, toCamera, scene->view[0].xyz);
    const float3 world = point.position + side * (point.width * 0.5 * corner.x);
    return finish(world, point.color, float2(point.u, corner.x * 0.5 + 0.5), float2(0.0, corner.x));
}

float4 shade(VertexOutput input, bool ribbon)
{
    let scene = data.scene;
    float4 color = input.color;
    if (data.texture != noTexture)
    {
        color *= sampleTexture(data.texture, input.uv);
    }
    else if (!ribbon)
    {
        // A soft disc, brightest in its middle.
        const float r2 = dot(input.local, input.local);
        const float fade = saturate(1.0 - r2);
        color.a *= fade * fade;
    }
    if (ribbon)
    {
        // Ribbons soften along their edges.
        color.a *= smoothstep(1.0, 0.6, abs(input.local.y));
    }

    if (data.softness > 0.0)
    {
        const float depth = sceneDepth.Load(int3(int2(input.position.xy), 0)).r;
        if (depth > 0.0)
        {
            const float2 uv = input.position.xy / float2(scene->viewportWidth, scene->viewportHeight);
            const float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
            const float4 surface = mul(scene->inverseViewProjection, clip);
            const float surfaceDepth = -mul(scene->view, float4(surface.xyz / surface.w, 1.0)).z;
            color.a *= saturate((surfaceDepth - input.viewDepth) / data.softness);
        }
    }

    float3 luminance = color.rgb;
    if ((data.flags & flagLit) != 0)
    {
        // A matte surface turned towards the viewer.
        const float3 toCamera = normalize(scene->cameraPosition - input.worldPosition);
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
        luminance = shadeSurface(scene, surface) * scene->exposure;
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, (data.flags & flagAdditive) != 0 ? 0.0 : alpha);
}

[shader("fragment")]
float4 particleFragment(VertexOutput input) : SV_Target
{
    return shade(input, false);
}

[shader("fragment")]
float4 ribbonFragment(VertexOutput input) : SV_Target
{
    return shade(input, true);
}
