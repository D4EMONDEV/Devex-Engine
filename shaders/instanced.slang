// The meshes the GPU culled, drawn a batch at a time: each instance of a draw is a slot the culling
// filled for a view (cull.slang), naming the instance in the buffer of the frame. The passes are
// those of mesh.slang, prepass.slang, shadow.slang and pick.slang, from the geometry module.

import common;
import pbr;
import geometry;

[[vk::push_constant]]
ConstantBuffer<InstancedData> data;

[shader("vertex")]
SurfaceVertex sceneVertex(uint vertexIndex : SV_VertexID, uint slot : SV_VulkanInstanceID)
{
    GpuInstance instance;
    return surfaceVertex(instanceDraw(data, slot, instance), vertexIndex);
}

[shader("fragment")]
float4 sceneFragment(SurfaceVertex input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    return surfaceFragment(data.scene, data.material, input, frontFacing);
}

[shader("vertex")]
DepthVertex prepassVertex(uint vertexIndex : SV_VertexID, uint slot : SV_VulkanInstanceID)
{
    GpuInstance instance;
    const DrawData draw = instanceDraw(data, slot, instance);
    return movedDepthVertex(draw, instance.previousWorld, vertexIndex);
}

[shader("fragment")]
DepthTargets prepassFragment(DepthVertex input, bool frontFacing : SV_IsFrontFace)
{
    return depthFragment(data.scene, data.material, input, frontFacing);
}

[shader("vertex")]
ShadowVertex shadowVertex(uint vertexIndex : SV_VertexID, uint slot : SV_VulkanInstanceID)
{
    GpuInstance instance;
    return viewVertex(instanceDraw(data, slot, instance), data.scene->cascadeViewProjections[data.cascade], vertexIndex);
}

[shader("vertex")]
ShadowVertex localShadowVertex(uint vertexIndex : SV_VertexID, uint slot : SV_VulkanInstanceID)
{
    GpuInstance instance;
    return viewVertex(instanceDraw(data, slot, instance), data.scene->shadowViews[data.cascade].viewProjection, vertexIndex);
}

[shader("fragment")]
void shadowFragment(ShadowVertex input)
{
    discardCutout(data.scene, data.material, input.uv);
}

[shader("vertex")]
ShadowVertex pickVertex(uint vertexIndex : SV_VertexID, uint slot : SV_VulkanInstanceID)
{
    GpuInstance instance;
    return viewVertex(instanceDraw(data, slot, instance), data.scene->pickViewProjection, vertexIndex);
}

[shader("fragment")]
uint pickFragment(ShadowVertex input) : SV_Target
{
    discardCutout(data.scene, data.material, input.uv);
    return input.objectId;
}
