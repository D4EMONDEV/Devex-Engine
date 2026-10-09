// Draws sprites among the blended surfaces of the scene: rectangles of a texture lying in the XY
// plane of their entity, facing +Z. A simple sprite stretches its rectangle over its quad; a sliced
// one keeps its borders at their size and stretches its middle, a tiled one repeats its middle.
// Colours are relative to the exposure, as the emission of materials, unless the sprite is lit as a
// matte surface. Unless it is unshaded, the canvas modulation tints it and the 2D lights add their
// light, through its normal map, hidden by the occluders whose distances their shadows keep. They
// leave premultiplied: additive sprites leave an alpha of zero, which the blending adds. The same
// quads report their object for picking, and cover the selection mask.

import common;
import canvas;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    // Meters from the bottom-left corner of the rectangle, before any flip.
    float2 local : TEXCOORD0;
    float viewDepth : TEXCOORD1;
    nointerpolation uint sprite : TEXCOORD2;
};

VertexOutput transform(float4x4 viewProjection, uint vertexIndex, uint instanceIndex)
{
    let scene = data.scene;
    const uint index = data.first + instanceIndex;
    const Sprite sprite = data.sprites[index];
    const float2 local = corners[vertexIndex] * sprite.size;
    const float3 world = mul(sprite.world, float4(spritePlanePosition(sprite, local), 0.0, 1.0)).xyz;
    VertexOutput output;
    output.position = mul(viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.local = local;
    output.viewDepth = -mul(scene->view, float4(world, 1.0)).z;
    output.sprite = index;
    return output;
}

[shader("vertex")]
VertexOutput spriteVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    return transform(data.scene->viewProjection, vertexIndex, instanceIndex);
}

[shader("fragment")]
float4 spriteFragment(VertexOutput input) : SV_Target
{
    const Sprite sprite = data.sprites[input.sprite];
    const float4 color = spriteColor(sprite, input.local);
    const bool mapped = spriteMapped(sprite);
    const float3 normal = mapped ? spriteNormalOf(sprite, spriteNormalMap(sprite, input.local)) : float3(0.0, 0.0, 1.0);
    return shadeSprite(sprite, color, input.worldPosition, input.viewDepth, input.position.xy, normal, mapped, false, false);
}

// Picking and the selection mask keep the pixels the sprite covers.
void discardClear(VertexOutput input)
{
    const Sprite sprite = data.sprites[input.sprite];
    if (spriteColor(sprite, input.local).a < 0.25)
    {
        discard;
    }
}

// The pick projection maps the requested pixels to the whole target.
[shader("vertex")]
VertexOutput spritePickVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    return transform(data.scene->pickViewProjection, vertexIndex, instanceIndex);
}

[shader("fragment")]
uint spritePickFragment(VertexOutput input) : SV_Target
{
    discardClear(input);
    return data.sprites[input.sprite].objectId;
}

[shader("vertex")]
VertexOutput spriteMaskVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    // Sprites and tilemaps share this silhouette, displayed after TAA like mesh outlines.
    return transform(data.scene->unjitteredViewProjection, vertexIndex, instanceIndex);
}

[shader("fragment")]
float spriteMaskFragment(VertexOutput input) : SV_Target
{
    discardClear(input);
    return 1.0;
}
