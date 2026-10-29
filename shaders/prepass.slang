// Writes the depth of the scene before it is shaded, along with the motion of every pixel and the
// normal of its surface, one instance a draw. The temporal antialiasing follows the motion, the
// ambient occlusion reads the normals, and the shading pass then draws only what is visible.

import common;
import geometry;

struct PrepassData
{
    DrawData draw;
    // Where the instance stood on the previous frame, and the bones it was posed by.
    float4x4 previousWorld;
    float4x4* previousBones;
};

[[vk::push_constant]]
ConstantBuffer<PrepassData> prepass;

[shader("vertex")]
DepthVertex vertexMain(uint vertexIndex : SV_VertexID)
{
    return depthVertex(prepass.draw, prepass.previousWorld, prepass.previousBones, vertexIndex);
}

[shader("fragment")]
DepthTargets fragmentMain(DepthVertex input, bool frontFacing : SV_IsFrontFace)
{
    return depthFragment(prepass.draw.scene, prepass.draw.material, input, frontFacing);
}
