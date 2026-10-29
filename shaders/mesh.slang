// Draws meshes with physically based materials into the high dynamic range scene color, one
// instance a draw. Vertices, scene data and materials are read through buffer device addresses
// passed in push constants (vertex pulling), so the pipeline has no vertex input state. The shading
// itself is in the geometry module, which instanced.slang shares.

import common;
import pbr;
import geometry;

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

[shader("vertex")]
SurfaceVertex vertexMain(uint vertexIndex : SV_VertexID)
{
    return surfaceVertex(draw, vertexIndex);
}

[shader("fragment")]
float4 fragmentMain(SurfaceVertex input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    return surfaceFragment(draw.scene, draw.material, input, frontFacing);
}
