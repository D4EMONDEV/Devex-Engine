// Screen space ambient occlusion: how much of the sky each pixel can see, measured by throwing a
// few points into the hemisphere above its surface and asking the depth buffer whether anything
// stands in front of them. The points move every frame, and the temporal antialiasing averages
// what they measure, which is why so few of them are enough.

import common;

struct AoData
{
    SceneData* scene;
    // How far, in meters, a surface looks for what hides it.
    float radius;
    // How dark a fully hidden pixel becomes, from 0 to 1.
    float intensity;
    // Changes the points every frame.
    float frame;
    float padding;
};

[[vk::push_constant]]
ConstantBuffer<AoData> ao;

static const uint sampleCount = 8;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VertexOutput output;
    output.uv = fullscreenUv(vertexIndex);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Where the pixel of a texture coordinate and a depth stands in the world.
float3 worldPosition(float2 uv, float depth)
{
    const float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    const float4 world = mul(ao.scene->inverseViewProjection, clip);
    return world.xyz / world.w;
}

// A number between 0 and 1 that changes with the pixel and the frame, without any pattern.
float noise(float2 pixel, float offset)
{
    return frac(sin(dot(pixel + offset, float2(12.9898, 78.233))) * 43758.5453);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float depth = depthTexture.Sample(clampSampler, input.uv).r;
    // Nothing was drawn there: the sky is not occluded.
    if (depth <= 0.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const float3 position = worldPosition(input.uv, depth);
    const float3 normal = decodeNormal(normalTexture.Sample(clampSampler, input.uv).rg);
    const float3 toCamera = ao.scene->cameraPosition - position;
    const float distanceToCamera = length(toCamera);

    // An orthonormal frame around the normal, turned differently at every pixel.
    const float angle = noise(input.position.xy, ao.frame) * 6.2831853;
    const float3 aside = abs(normal.y) < 0.99 ? normalize(cross(normal, float3(0.0, 1.0, 0.0)))
                                              : float3(1.0, 0.0, 0.0);
    const float3 other = cross(normal, aside);
    const float2 turn = float2(cos(angle), sin(angle));

    float occlusion = 0.0;
    for (uint index = 0; index < sampleCount; ++index)
    {
        // Points spread over the hemisphere, packed towards the surface where it matters most.
        const float fraction = (float(index) + 0.5) / float(sampleCount);
        const float spin = fraction * 6.2831853 * 2.4 + angle;
        const float height = sqrt(1.0 - fraction);
        const float ring = sqrt(fraction);
        const float3 direction = normalize(normal * height +
                                           (aside * (cos(spin) * turn.x - sin(spin) * turn.y) +
                                            other * (cos(spin) * turn.y + sin(spin) * turn.x)) *
                                               ring);
        const float reach = ao.radius * (0.3 + 0.7 * fraction);
        const float3 point = position + direction * reach;

        const float4 clip = mul(ao.scene->unjitteredViewProjection, float4(point, 1.0));
        if (clip.w <= 0.0)
        {
            continue;
        }
        const float2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(uv < 0.0) || any(uv > 1.0))
        {
            continue;
        }
        const float sampledDepth = depthTexture.Sample(clampSampler, uv).r;
        if (sampledDepth <= 0.0)
        {
            continue;
        }

        const float3 occluder = worldPosition(uv, sampledDepth);
        const float occluderDistance = length(ao.scene->cameraPosition - occluder);
        const float pointDistance = length(ao.scene->cameraPosition - point);
        if (occluderDistance < pointDistance - 0.02)
        {
            // Something far behind the surface hides nothing of it.
            const float difference = pointDistance - occluderDistance;
            occlusion += saturate(ao.radius / max(difference, 1e-4)) *
                         saturate(dot(normalize(occluder - position), normal));
        }
    }

    const float visibility = 1.0 - saturate(occlusion / float(sampleCount)) * ao.intensity;
    // The measure is only worth something near the camera: it fades out with distance.
    const float fade = saturate(1.0 - distanceToCamera / (ao.radius * 200.0));
    return float4(lerp(1.0, visibility, fade).xxx, 1.0);
}
