// Renders the object identifier of the surface under one pixel, for picking in the editor, and the
// silhouettes of selected objects, which the overlay outlines.

import common;
import geometry;

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

// The pick projection maps the requested pixel to the whole 1x1 target.
[shader("vertex")]
ShadowVertex pickVertex(uint vertexIndex : SV_VertexID)
{
    return viewVertex(draw, draw.scene->pickViewProjection, vertexIndex);
}

// Cut-out materials are only picked and outlined where they are opaque.
[shader("fragment")]
uint pickFragment(ShadowVertex input) : SV_Target
{
    discardCutout(draw.scene, draw.material, input.uv);
    return input.objectId;
}

[shader("vertex")]
ShadowVertex maskVertex(uint vertexIndex : SV_VertexID)
{
    // The outline is composited after TAA, so its silhouette must not inherit the scene jitter.
    return viewVertex(draw, draw.scene->unjitteredViewProjection, vertexIndex);
}

[shader("fragment")]
float maskFragment(ShadowVertex input) : SV_Target
{
    discardCutout(draw.scene, draw.material, input.uv);
    return 1.0;
}
