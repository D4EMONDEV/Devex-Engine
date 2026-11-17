// Draws sprites and the tiles of tilemaps for WebGPU, compiled to WGSL: those of sprite.slang, lit by the 2D lights
// through their normal maps and hidden by the occluders whose distances their shadows keep, and lit sprites by the
// light of the scene, as canvas.slang lights them. What they read is in webgpu_canvas.slang: each draw covers a run of
// sprites that reads one texture and one normal map. They leave premultiplied: additive sprites leave an alpha of zero,
// which the blending adds.

import webgpu_scene;
import webgpu_canvas;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    // Meters from the bottom-left corner of the rectangle, before any flip.
    float2 local : TEXCOORD0;
    float viewDepth : TEXCOORD1;
    nointerpolation uint sprite : TEXCOORD2;
};

[shader("vertex")]
VertexOutput spriteVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const uint index = spriteDraw.first + instanceIndex;
    const Sprite sprite = sprites[index];
    const float2 local = corners[vertexIndex] * sprite.size;
    const float3 world = mul(sprite.world, float4(spritePlanePosition(sprite, local), 0.0, 1.0)).xyz;
    VertexOutput output;
    output.position = mul(spriteFrame.viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.local = local;
    output.viewDepth = -mul(scene.view, float4(world, 1.0)).z;
    output.sprite = index;
    return output;
}

[shader("fragment")]
float4 spriteFragment(VertexOutput input) : SV_Target
{
    const Sprite sprite = sprites[input.sprite];
    const SpriteSample at = spriteSample(sprite, input.local);
    const float4 sampled = spriteTextureAt(at);
    const float4 color = sprite.texture != noTexture ? sprite.color * sampled : sprite.color;
    // Its normal map, where it has one, read where its texture is.
    const bool mapped = spriteMapped(sprite);
    const float3 normal = mapped ? spriteNormalOf(sprite, spriteNormalMap(sprite, at)) : float3(0.0, 0.0, 1.0);
    return shadeSprite(sprite, color, input.worldPosition, input.viewDepth, input.position.xy, normal, mapped, false, false);
}
