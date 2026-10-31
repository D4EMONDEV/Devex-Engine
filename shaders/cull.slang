// Which instances each view of the frame sees, on the GPU: the box of the mesh of every instance,
// turned into the world, against the planes of the view. Each instance seen takes the next slot of
// its batch in that view, counted in the indirect draw of the batch, so that a pass then draws each
// batch at once with the instances the view kept.

import common;
import geometry;

// A view culled: the planes whose normals point inside it (none for a view that keeps everything),
// and the flags of the instances it looks for.
struct CullView
{
    float4 planes[6];
    uint planeCount;
    uint flags;
    uint padding0;
    uint padding1;
};

// VkDrawIndexedIndirectCommand.
struct DrawCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct CullData
{
    GpuInstance* instances;
    CullView* views;
    // The draws of the batches, a row of them for each view.
    DrawCommand* commands;
    // The slots of the views, a row of instanceCount of them each.
    uint* visible;
    uint instanceCount;
    uint batchCount;
    uint seedsOnly;
    uint padding;
    uint* states;
};

[[vk::push_constant]]
ConstantBuffer<CullData> cull;

[shader("compute")]
[numthreads(64, 1, 1)]
void cullMain(uint3 id : SV_DispatchThreadID)
{
    const uint index = id.x;
    const uint viewIndex = id.y;
    if (index >= cull.instanceCount)
    {
        return;
    }
    let instance = cull.instances[index];
    let view = cull.views[viewIndex];
    if (viewIndex == 0)
    {
        cull.states[index] = 0; // Outside the camera's frustum until all planes pass.
    }
    if ((instance.flags & view.flags) == 0)
    {
        return;
    }
    // The box in the world: its center moved, its half size turned by the absolute matrix, as the
    // CPU turns it (math::transform).
    float3 worldCenter, worldHalf;
    const bool bounded = instanceBounds(instance, worldCenter, worldHalf);
    // An empty box stands for a mesh kept everywhere.
    const uint planeCount = bounded ? view.planeCount : 0;
    for (uint plane = 0; plane < planeCount; ++plane)
    {
        const float4 equation = view.planes[plane];
        const float distance = dot(equation.xyz, worldCenter) + equation.w;
        const float reach = dot(abs(equation.xyz), worldHalf);
        if (distance + reach < 0.0)
        {
            return;
        }
    }
    if (viewIndex == 0)
    {
        cull.states[index] = 2; // Visible; the occlusion pass may refine this.
        if (cull.seedsOnly != 0 && (instance.flags & 4u) == 0)
        {
            return;
        }
    }
    const uint command = viewIndex * cull.batchCount + instance.batch;
    uint slot;
    InterlockedAdd(cull.commands[command].instanceCount, 1u, slot);
    cull.visible[viewIndex * cull.instanceCount + cull.commands[command].firstInstance + slot] = index;
}
