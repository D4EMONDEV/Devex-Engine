// Draws sprites and the tiles of tilemaps for OpenGL, translated to GLSL ES 3.00 and GLSL 3.30: those of sprite.slang,
// lit by the 2D lights through their normal maps and hidden by the occluders whose distances their shadows keep, and lit
// sprites by the light of the scene, as canvas.slang lights them. Each sprite is an instance whose fields arrive as
// attributes, since OpenGL ES 3.0 and WebGL2 have no storage buffers, with the bytes of the local lights a lit one was
// given. What they read is in gl_canvas.slang: each draw covers a run of sprites that reads one texture and one normal
// map. They leave premultiplied: additive sprites leave an alpha of zero, which the blending adds.

import gl_canvas;
import gl_scene;

// A render::GpuSprite, read as attributes: its rows of the world matrix, then its fields in their order; and the local
// lights of a lit one, from a buffer of their own.
struct SpriteInstance
{
    float4 world0 : WORLD0;
    float4 world1 : WORLD1;
    float4 world2 : WORLD2;
    float4 world3 : WORLD3;
    float4 uvRect : UVRECT;
    float4 color : COLOR0;
    float4 border : BORDER;
    // The size of the rectangle, then its pivot.
    float4 sizeAndPivot : SIZE;
    float2 naturalSize : NATURAL;
    // The slot of its texture, then its mode and flags.
    uint2 textureAndFlags : FLAGS;
    // The 2D lights it takes, then the slot of its normal map.
    uint2 maskAndNormal : LIGHTMASK;
    uint2 lights : LIGHTS;
};

struct VertexOutput
{
    float4 position : SV_Position;
    // Where it lies in the world, and how far in front of the camera.
    float4 worldPosition : POSITION;
    // Meters from the bottom-left corner of the rectangle, before any flip.
    float2 local : TEXCOORD0;
    nointerpolation float4 uvRect : TEXCOORD1;
    nointerpolation float4 color : TEXCOORD2;
    nointerpolation float4 border : TEXCOORD3;
    // The size of the rectangle, then its natural size.
    nointerpolation float4 sizes : TEXCOORD4;
    // Its texture, its mode and flags, its light mask and its normal map.
    nointerpolation uint4 slots : TEXCOORD5;
    nointerpolation uint2 lights : TEXCOORD6;
    // The axes of its entity in the world, which turn its normal map and face a lit sprite.
    nointerpolation float3 axisX : TEXCOORD7;
    nointerpolation float3 axisY : TEXCOORD8;
    nointerpolation float3 axisZ : TEXCOORD9;
};

// The sprite a fragment belongs to, as sprite_shape reads it.
Sprite spriteOf(float4 uvRect, float4 color, float4 border, float4 sizes, uint4 slots)
{
    Sprite sprite;
    sprite.world = float4x4(1.0);
    sprite.uvRect = uvRect;
    sprite.color = color;
    sprite.border = border;
    sprite.size = sizes.xy;
    sprite.pivot = float2(0.0);
    sprite.naturalSize = sizes.zw;
    sprite.texture = slots.x;
    sprite.flags = slots.y;
    sprite.objectId = 0;
    sprite.lightMask = slots.z;
    sprite.normalTexture = slots.w;
    sprite.material = 0;
    sprite.instanceParameters = noTexture;
    sprite.padding0 = 0;
    sprite.padding1 = 0;
    sprite.padding2 = 0;
    return sprite;
}

[shader("vertex")]
VertexOutput spriteVertex(SpriteInstance instance, uint vertexIndex : SV_VulkanVertexID)
{
    const uint4 slots = uint4(instance.textureAndFlags, instance.maskAndNormal);
    Sprite sprite = spriteOf(instance.uvRect, instance.color, instance.border, float4(instance.sizeAndPivot.xy, instance.naturalSize), slots);
    sprite.pivot = instance.sizeAndPivot.zw;
    // The attributes are the columns of the matrix, as glm keeps them: a row vector times the matrix whose rows they are.
    const float4x4 columns = float4x4(instance.world0, instance.world1, instance.world2, instance.world3);
    const float2 local = corners[vertexIndex] * sprite.size;
    const float3 position = mul(float4(spritePlanePosition(sprite, local), 0.0, 1.0), columns).xyz;
    VertexOutput output;
    output.position = mul(spriteFrame.viewProjection, float4(position, 1.0));
    output.worldPosition = float4(position, -mul(scene.view, float4(position, 1.0)).z);
    output.local = local;
    output.uvRect = instance.uvRect;
    output.color = instance.color;
    output.border = instance.border;
    output.sizes = float4(instance.sizeAndPivot.xy, instance.naturalSize);
    output.slots = slots;
    output.lights = instance.lights;
    output.axisX = instance.world0.xyz;
    output.axisY = instance.world1.xyz;
    output.axisZ = instance.world2.xyz;
    return output;
}

[shader("fragment")]
float4 spriteFragment(VertexOutput input) : SV_Target
{
    Sprite sprite = spriteOf(input.uvRect, input.color, input.border, input.sizes, input.slots);
    // Its entity, turned without moving, as spriteNormalOf and the lit sprites read it.
    sprite.world = transpose(float4x4(float4(input.axisX, 0.0), float4(input.axisY, 0.0), float4(input.axisZ, 0.0), float4(0.0, 0.0, 0.0, 1.0)));
    const SpriteSample at = spriteSample(sprite, input.local);
    const float4 sampled = spriteTextureAt(at);
    const float4 color = sprite.texture != noTexture ? sprite.color * sampled : sprite.color;
    // Its normal map, where it has one, read where its texture is.
    const bool mapped = spriteMapped(sprite);
    const float3 normal = mapped ? spriteNormalOf(sprite, spriteNormalMap(sprite, at)) : float3(0.0, 0.0, 1.0);
    return shadeSprite(sprite, color, input.worldPosition.xyz, input.worldPosition.w, input.position.xy, normal, mapped, false, false,
                       input.lights);
}
