// One workgroup prepares one mesh pose, reused by every submesh and render pass. Its reduction
// computes the bounds of the actual pose, including vertices which left the bind-pose box.
import common;
import geometry;

struct SkinJob
{
    Vertex* vertices;
    VertexSkin* skin;
    float4x4* bones;
    float4x4* previousBones;
    SkinnedVertex* output;
    SkinBounds* bounds;
    float4x4 world;
    float4x4 previousWorld;
    uint vertexCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct SkinningData
{
    SkinJob* jobs;
    uint jobCount;
    uint dispatchWidth;
};
[[vk::push_constant]]
ConstantBuffer<SkinningData> skinning;

groupshared float3 minima[64];
groupshared float3 maxima[64];
groupshared uint uncertain[64];

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint index = group.x + group.y * skinning.dispatchWidth;
    if (index >= skinning.jobCount) return;
    const SkinJob job = skinning.jobs[index];
    DrawData draw;
    draw.vertices = job.vertices;
    draw.skin = job.skin;
    draw.bones = job.bones;
    draw.world = job.world;
    draw.skinned = 1;
    float3 low = float3(3.402823466e+38);
    float3 high = -low;
    uint invalid = 0;
    for (uint vertexIndex = lane; vertexIndex < job.vertexCount; vertexIndex += 64)
    {
        const Vertex source = job.vertices[vertexIndex];
        const float4x4 current = vertexTransform(draw, vertexIndex);
        const float4x4 previous = previousVertexTransform(draw, job.previousWorld,
                                                         job.previousBones, vertexIndex);
        SkinnedVertex posed;
        posed.position = mul(current, float4(source.position, 1.0));
        posed.previous = mul(previous, float4(source.position, 1.0));
        posed.normal = transformNormal(current, source.normal);
        posed.padding = 0;
        posed.tangent = float4(mul(float3x3(current), source.tangent.xyz),
                               source.tangent.w * handedness(current));
        job.output[vertexIndex] = posed;
        if (all(isfinite(posed.position)) && posed.position.w > 0.0)
        {
            const float3 position = posed.position.xyz / posed.position.w;
            low = min(low, position);
            high = max(high, position);
        }
        else
            invalid = 1;
    }
    minima[lane] = low;
    maxima[lane] = high;
    uncertain[lane] = invalid;
    GroupMemoryBarrierWithGroupSync();
    for (uint step = 32; step > 0; step /= 2)
    {
        if (lane < step)
        {
            minima[lane] = min(minima[lane], minima[lane + step]);
            maxima[lane] = max(maxima[lane], maxima[lane + step]);
            uncertain[lane] |= uncertain[lane + step];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0)
    {
        SkinBounds bounds;
        // An empty box is kept by the frustum and occlusion tests.
        bounds.minimum = uncertain[0] != 0 ? float3(1.0) : minima[0];
        bounds.maximum = uncertain[0] != 0 ? float3(-1.0) : maxima[0];
        bounds.padding0 = bounds.padding1 = 0;
        job.bounds[0] = bounds;
    }
}
