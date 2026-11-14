// What a sprite is and where its rectangle samples its texture, shared by the sprites of Vulkan
// (canvas.slang) and of WebGPU (webgpu_sprite.slang): plain arithmetic, without resources.
module sprite_shape;

public struct Sprite
{
    public float4x4 world;
    // Texture coordinates of the top-left corner, then of the bottom-right one.
    public float4 uvRect;
    // Straight alpha.
    public float4 color;
    // Sliced and tiled: the borders kept at their size, in meters: left, bottom, right, top.
    public float4 border;
    // The rectangle drawn, in meters, and the point of it at the origin of the entity.
    public float2 size;
    public float2 pivot;
    // The size of the sprite at its pixels, in meters.
    public float2 naturalSize;
    public uint texture;
    public uint flags;
    public uint objectId;
    // The 2D lights whose item mask shares a bit with it shine on it, through its normal map.
    public uint lightMask;
    public uint normalTexture;
    // The material whose shader draws it; its uniforms, for a shader of the project.
    public uint material;
    // Where the values it gives the instance uniforms of that shader start, or 0xFFFFFFFF for none.
    public uint instanceParameters;
    public uint padding0;
    public uint padding1;
    public uint padding2;
};

public static const uint modeMask = 3;
public static const uint modeSimple = 0;
public static const uint modeSliced = 1;
public static const uint modeTiled = 2;
public static const uint flagFlipX = 4;
public static const uint flagFlipY = 8;
public static const uint flagAdditive = 16;
public static const uint flagLit = 32;
public static const uint flagUnshaded = 64;
public static const uint noTexture = 0xFFFFFFFF;

// The two triangles of the rectangle, from its bottom-left corner.
public static const float2 corners[6] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                                          float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0) };

// Where a point of the rectangle falls on the sprite along one axis, from 0 at its low edge to 1
// at its high edge: borders keep their size, the middle stretches or repeats.
public float spriteFraction(float position, float size, float natural, float low, float high, uint mode)
{
    if (mode == modeSimple || natural <= 0.0)
    {
        return position / max(size, 1e-6);
    }
    // Borders larger than the rectangle shrink together.
    const float scale = min(1.0, size / max(low + high, 1e-6));
    const float drawnLow = low * scale;
    const float drawnHigh = high * scale;
    if (position < drawnLow)
    {
        return position / max(drawnLow, 1e-6) * low / natural;
    }
    if (position > size - drawnHigh)
    {
        return 1.0 - (size - position) / max(drawnHigh, 1e-6) * high / natural;
    }
    const float middle = natural - low - high;
    const float drawnMiddle = max(size - drawnLow - drawnHigh, 1e-6);
    const float into = position - drawnLow;
    if (mode == modeTiled && middle > 1e-6)
    {
        return (low + frac(into / middle) * middle) / natural;
    }
    return (low + into / drawnMiddle * middle) / natural;
}

// Where a fragment of the rectangle samples a texture of `textureSize` texels, at `local` meters from
// its bottom-left corner. Texture rows run from the top.
public float2 spriteUv(Sprite sprite, float2 local, float2 textureSize)
{
    const uint mode = sprite.flags & modeMask;
    const float2 fraction = float2(
        spriteFraction(local.x, sprite.size.x, sprite.naturalSize.x, sprite.border.x, sprite.border.z, mode),
        spriteFraction(local.y, sprite.size.y, sprite.naturalSize.y, sprite.border.y, sprite.border.w, mode));
    const float2 uv = float2(lerp(sprite.uvRect.x, sprite.uvRect.z, fraction.x),
                             lerp(sprite.uvRect.w, sprite.uvRect.y, fraction.y));
    // Clamp to this sprite's texel centres, not to the full atlas. Raster coverage can
    // interpolate just beyond a quad edge, and a mirrored edge may land exactly on the
    // next tile even with nearest filtering. Clamping the sample (rather than shrinking
    // the UV rectangle) preserves the pixel spacing and the sliced/tiled mapping.
    const float2 halfTexel = 0.5 / textureSize;
    const float2 low = min(sprite.uvRect.xy, sprite.uvRect.zw);
    const float2 high = max(sprite.uvRect.xy, sprite.uvRect.zw);
    const float2 inset = min(halfTexel, (high - low) * 0.5);
    return clamp(uv, low + inset, high - inset);
}

// The slope of an unbroken mapping, so that the seams of the borders and of the tiles pick the
// same mip level as the rest.
public float2 spriteUvPerMeter(Sprite sprite)
{
    return float2(sprite.uvRect.z - sprite.uvRect.x, sprite.uvRect.y - sprite.uvRect.w) /
           max(sprite.naturalSize, float2(1e-6));
}

// Where a point of the rectangle of a sprite lies in the plane of its entity, from the meters from
// its bottom-left corner: flipping mirrors the sprite around its pivot.
public float2 spritePlanePosition(Sprite sprite, float2 local)
{
    float2 position = local - sprite.pivot * sprite.size;
    if ((sprite.flags & flagFlipX) != 0)
    {
        position.x = -position.x;
    }
    if ((sprite.flags & flagFlipY) != 0)
    {
        position.y = -position.y;
    }
    return position;
}
