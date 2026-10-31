// The seed prepass used current poses and the current camera. Test every frustum survivor against
// its conservative depth pyramid, then recover newly visible instances in a second depth prepass.
// History is only a scheduling hint: it never decides that an instance may be left out.
import common;
import geometry;

struct CullView
{
    float4 planes[6];
    uint planeCount;
    uint flags;
    uint padding0;
    uint padding1;
};
struct DrawCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
struct OcclusionData
{
    GpuInstance* instances;
    CullView* views;
    DrawCommand* commands;
    uint* visible;
    uint* states;
    float* pyramid;
    SceneData* scene;
    uint instanceCount;
    uint batchCount;
    uint deferredView;
    uint width;
    uint height;
    uint padding;
};
[[vk::push_constant]]
ConstantBuffer<OcclusionData> occlusion;

bool hidden(GpuInstance instance)
{
    float3 worldCenter, worldHalf;
    if (!instanceBounds(instance, worldCenter, worldHalf)) return false;
    float2 low = float2(1.0);
    float2 high = float2(-1.0);
    float closest = 0.0;
    for (uint corner = 0; corner < 8; ++corner)
    {
        const float3 sign = float3((corner & 1u) != 0 ? 1.0 : -1.0,
            (corner & 2u) != 0 ? 1.0 : -1.0, (corner & 4u) != 0 ? 1.0 : -1.0);
        const float4 clip = mul(occlusion.scene.viewProjection, float4(worldCenter + sign * worldHalf, 1.0));
        // A box crossing the eye or the near plane is kept; projecting it could under-bound it.
        if (!all(isfinite(clip)) || clip.w <= 0.0 || clip.z >= clip.w || clip.z < 0.0) return false;
        const float3 ndc = clip.xyz / clip.w;
        low = min(low, ndc.xy);
        high = max(high, ndc.xy);
        closest = max(closest, ndc.z);
    }
    const uint2 imageSize = uint2(occlusion.width, occlusion.height);
    // Expand by a whole pixel for raster coverage, floating point error and subpixel edges.
    const uint2 first = uint2(clamp(floor((low * 0.5 + 0.5) * imageSize) - 1.0, 0.0, float2(imageSize - 1u)));
    const uint2 last = uint2(clamp(ceil((high * 0.5 + 0.5) * imageSize) + 1.0, 0.0, float2(imageSize - 1u)));
    uint2 size = (imageSize + 1u) / 2u;
    uint2 from = first / 2u;
    uint2 to = last / 2u;
    uint offset = 0;
    while (any(to - from > 1u))
    {
        offset += size.x * size.y;
        size = (size + 1u) / 2u;
        from /= 2u;
        to /= 2u;
    }
    float farthest = 1.0;
    for (uint y = from.y; y <= to.y; ++y)
    for (uint x = from.x; x <= to.x; ++x)
        farthest = min(farthest, occlusion.pyramid[offset + y * size.x + x]);
    return closest + max(0.000002, closest * 0.0001) < farthest;
}

void append(uint index, uint view, uint batch)
{
    const uint command = view * occlusion.batchCount + batch;
    uint slot;
    InterlockedAdd(occlusion.commands[command].instanceCount, 1u, slot);
    occlusion.visible[view * occlusion.instanceCount + occlusion.commands[command].firstInstance + slot] = index;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    const uint index = id.x;
    if (index >= occlusion.instanceCount || occlusion.states[index] == 0) return;
    const GpuInstance instance = occlusion.instances[index];
    if (hidden(instance))
    {
        occlusion.states[index] = 1;
        return;
    }
    append(index, 0, instance.batch);
    if ((instance.flags & 4u) == 0)
        append(index, occlusion.deferredView, instance.batch);
}
