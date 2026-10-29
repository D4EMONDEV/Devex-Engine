// Renders the depth of shadow casters into one cascade of the sun's shadow map, or into the tile
// of the atlas that a local light was given, one instance a draw.

import common;
import geometry;

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

[shader("vertex")]
ShadowVertex vertexMain(uint vertexIndex : SV_VertexID)
{
    return viewVertex(draw, draw.scene->cascadeViewProjections[draw.cascade], vertexIndex);
}

// The same, for a light that owns a view of the shadow atlas: the draw carries the view it writes
// where a cascade carries its number.
[shader("vertex")]
ShadowVertex localVertex(uint vertexIndex : SV_VertexID)
{
    return viewVertex(draw, draw.scene->shadowViews[draw.cascade].viewProjection, vertexIndex);
}

[shader("fragment")]
void fragmentMain(ShadowVertex input)
{
    // Cut-out materials cast shadows only where they are opaque.
    discardCutout(draw.scene, draw.material, input.uv);
}
