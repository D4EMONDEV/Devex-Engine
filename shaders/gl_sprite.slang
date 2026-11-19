// Draws sprites and the tiles of tilemaps for OpenGL, translated to GLSL ES 3.00 and GLSL 3.30: those of sprite.slang,
// each sprite an instance whose fields arrive as attributes, since OpenGL ES 3.0 and WebGL2 have no storage buffers.
// Each draw covers a run of sprites that reads one texture, bound with its sampler. They leave premultiplied: additive
// sprites leave an alpha of zero, which the blending adds. The 2D lights and the light of the scene on lit sprites come
// with the 3D.

import sprite_shape;

struct SpriteFrame
{
    float4x4 viewProjection;
    // The tint of the canvas.
    float4 canvasModulate;
};

ConstantBuffer<SpriteFrame> spriteFrame;
Sampler2D spriteTexture;

// A render::GpuSprite, read as attributes: its rows of the world matrix, then its fields in their order.
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
};

struct VertexOutput
{
    float4 position : SV_Position;
    // Meters from the bottom-left corner of the rectangle, before any flip.
    float2 local : TEXCOORD0;
    nointerpolation float4 uvRect : TEXCOORD1;
    nointerpolation float4 color : TEXCOORD2;
    nointerpolation float4 border : TEXCOORD3;
    // The size of the rectangle, then its natural size.
    nointerpolation float4 sizes : TEXCOORD4;
    nointerpolation uint2 textureAndFlags : TEXCOORD5;
};

// The sprite a fragment belongs to, as sprite_shape reads it.
Sprite spriteOf(float4 uvRect, float4 color, float4 border, float4 sizes, uint2 textureAndFlags)
{
    Sprite sprite;
    sprite.world = float4x4(1.0);
    sprite.uvRect = uvRect;
    sprite.color = color;
    sprite.border = border;
    sprite.size = sizes.xy;
    sprite.pivot = float2(0.0);
    sprite.naturalSize = sizes.zw;
    sprite.texture = textureAndFlags.x;
    sprite.flags = textureAndFlags.y;
    sprite.objectId = 0;
    sprite.lightMask = 0;
    sprite.normalTexture = noTexture;
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
    Sprite sprite = spriteOf(instance.uvRect, instance.color, instance.border,
                             float4(instance.sizeAndPivot.xy, instance.naturalSize), instance.textureAndFlags);
    sprite.pivot = instance.sizeAndPivot.zw;
    // The attributes are the columns of the matrix, as glm keeps them: a row vector times the matrix whose rows they are.
    const float4x4 columns = float4x4(instance.world0, instance.world1, instance.world2, instance.world3);
    const float2 local = corners[vertexIndex] * sprite.size;
    const float3 position = mul(float4(spritePlanePosition(sprite, local), 0.0, 1.0), columns).xyz;
    VertexOutput output;
    output.position = mul(spriteFrame.viewProjection, float4(position, 1.0));
    output.local = local;
    output.uvRect = instance.uvRect;
    output.color = instance.color;
    output.border = instance.border;
    output.sizes = float4(instance.sizeAndPivot.xy, instance.naturalSize);
    output.textureAndFlags = instance.textureAndFlags;
    return output;
}

[shader("fragment")]
float4 spriteFragment(VertexOutput input) : SV_Target
{
    const Sprite sprite = spriteOf(input.uvRect, input.color, input.border, input.sizes, input.textureAndFlags);
    uint width;
    uint height;
    spriteTexture.GetDimensions(width, height);
    // The slopes of an unbroken mapping, taken before any branch, so that the seams of the borders and of the tiles
    // pick the same mip level as the rest.
    const float2 uvPerMeter = spriteUvPerMeter(sprite);
    const float2 slopeX = ddx(input.local) * uvPerMeter;
    const float2 slopeY = ddy(input.local) * uvPerMeter;
    const float2 uv = spriteUv(sprite, input.local, float2(float(width), float(height)));
    const float4 sampled = spriteTexture.SampleGrad(uv, slopeX, slopeY);
    const float4 color = sprite.texture != noTexture ? sprite.color * sampled : sprite.color;
    float3 luminance = color.rgb;
    if ((sprite.flags & flagUnshaded) == 0)
    {
        luminance *= spriteFrame.canvasModulate.rgb;
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, (sprite.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
