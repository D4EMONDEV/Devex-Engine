// What sprites and tilemaps are drawn with: the sprites of the frame, their mapping onto their
// texture, the 2D lights and their shadows. The sprite shader draws them, and the canvas_item shaders
// of projects build on it.
module canvas;

import common;
import pbr;

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
};

// A light of the 2D plane, as render::vulkan::GpuLight2D.
public struct Light2D
{
    // A point light: position, height and radius. A directional one: the middle of the line its
    // shadow strips start from, its height, and the width of the strips.
    public float4 position;
    // Colour times energy, and how fast a point light fades.
    public float4 color;
    // A directional light: the way it goes, then the line across it.
    public float4 direction;
    // Softness of the shadows, how far those of a directional light reach, and the length of its strips.
    public float4 shadow;
    public uint kind;
    public uint itemMask;
    // Where its distances start in the shadow buffer; -1 without shadows.
    public int shadowOffset;
    public uint padding;
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

public struct SpriteData
{
    public SceneData* scene;
    public Sprite* sprites;
    // The 2D lights of the frame, and the distances their shadows keep.
    public Light2D* lights;
    public float* shadows;
    // The tint of the canvas.
    public float4 canvasModulate;
    // The first sprite of the draw: instances count from it.
    public uint first;
    public uint lightCount;
    // How many distances each shadowed light keeps.
    public uint shadowResolution;
    public uint padding0;
};

[[vk::push_constant]]
public ConstantBuffer<SpriteData> data;

// The two triangles of the rectangle, from its bottom-left corner.
public static const float2 corners[6] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                                          float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0) };

// Where a point of the rectangle falls on the sprite along one axis, from 0 at its low edge to 1
// at its high edge: borders keep their size, the middle stretches or repeats.
float spriteFraction(float position, float size, float natural, float low, float high, uint mode)
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

// Where a fragment samples the textures of the sprite, and the slopes that choose their mip level.
public struct SpriteSample
{
    public float2 uv;
    public float2 slopeX;
    public float2 slopeY;
};

public SpriteSample spriteSample(Sprite sprite, float2 local)
{
    const uint mode = sprite.flags & modeMask;
    const float2 fraction = float2(
        spriteFraction(local.x, sprite.size.x, sprite.naturalSize.x, sprite.border.x, sprite.border.z, mode),
        spriteFraction(local.y, sprite.size.y, sprite.naturalSize.y, sprite.border.y, sprite.border.w, mode));
    // Texture rows run from the top.
    const float2 uv = float2(lerp(sprite.uvRect.x, sprite.uvRect.z, fraction.x),
                             lerp(sprite.uvRect.w, sprite.uvRect.y, fraction.y));
    // Clamp to this sprite's texel centres, not to the full atlas. Raster coverage can
    // interpolate just beyond a quad edge, and a mirrored edge may land exactly on the
    // next tile even with nearest filtering. Clamping the sample (rather than shrinking
    // the UV rectangle) preserves the pixel spacing and the sliced/tiled mapping.
    uint textureWidth;
    uint textureHeight;
    textures[NonUniformResourceIndex(sprite.texture)].GetDimensions(textureWidth, textureHeight);
    const float2 halfTexel = 0.5 / float2(textureWidth, textureHeight);
    const float2 low = min(sprite.uvRect.xy, sprite.uvRect.zw);
    const float2 high = max(sprite.uvRect.xy, sprite.uvRect.zw);
    const float2 inset = min(halfTexel, (high - low) * 0.5);
    const float2 sampleUv = clamp(uv, low + inset, high - inset);
    // The slope of an unbroken mapping, so that the seams of the borders and of the tiles pick the
    // same mip level as the rest.
    const float2 uvPerMeter = float2(sprite.uvRect.z - sprite.uvRect.x, sprite.uvRect.y - sprite.uvRect.w) /
                              max(sprite.naturalSize, float2(1e-6));
    SpriteSample at;
    at.uv = sampleUv;
    at.slopeX = ddx(local) * uvPerMeter;
    at.slopeY = ddy(local) * uvPerMeter;
    return at;
}

// The colour of the sprite at a fragment, straight alpha.
public float4 spriteColor(Sprite sprite, float2 local)
{
    const float4 color = sprite.color;
    if (sprite.texture == noTexture)
    {
        return color;
    }
    const SpriteSample at = spriteSample(sprite, local);
    return color * sampleTextureGrad(sprite.texture, at.uv, at.slopeX, at.slopeY);
}

// The normal of the sprite at a fragment, in the world, from the X and Y of its normal map from -1 to
// 1: mirrored as the sprite shows it and turned with its entity.
public float3 spriteNormalOf(Sprite sprite, float2 encoded)
{
    float3 normal = float3(encoded, sqrt(saturate(1.0 - dot(encoded, encoded))));
    // A flipped sprite, or a tile whose coordinates run backwards, shows its normals mirrored.
    if (((sprite.flags & flagFlipX) != 0) != (sprite.uvRect.z < sprite.uvRect.x))
    {
        normal.x = -normal.x;
    }
    if (((sprite.flags & flagFlipY) != 0) != (sprite.uvRect.w < sprite.uvRect.y))
    {
        normal.y = -normal.y;
    }
    return normalize(mul(sprite.world, float4(normal, 0.0)).xyz);
}

// Whether the sprite has a normal map that the 2D lights shine through.
public bool spriteMapped(Sprite sprite)
{
    return sprite.normalTexture != noTexture && sprite.texture != noTexture;
}

// The X and Y of the normal map of the sprite at a fragment, from -1 to 1; flat without one.
public float2 spriteNormalMap(Sprite sprite, float2 local)
{
    if (!spriteMapped(sprite))
    {
        return float2(0.0);
    }
    const SpriteSample at = spriteSample(sprite, local);
    return sampleTextureGrad(sprite.normalTexture, at.uv, at.slopeX, at.slopeY).rg * 2.0 - 1.0;
}

static const float shadowBias2D = 0.02;

// The distance a shadowed light keeps nearest to a place along its line of distances.
float occluderAt(Light2D light, float place, bool wraps)
{
    const int count = int(data.shadowResolution);
    int index = int(floor(place + 0.5));
    index = wraps ? ((index % count) + count) % count : clamp(index, 0, count - 1);
    return data.shadows[light.shadowOffset + index];
}

// How much of a point light reaches a point `distance` away along `fromLight`: its edges blur more
// the farther they fall behind the occluder.
float pointShadow(Light2D light, float2 fromLight, float distance)
{
    const float count = float(data.shadowResolution);
    float angle = atan2(fromLight.y, fromLight.x);
    angle = angle < 0.0 ? angle + 2.0 * pi : angle;
    const float place = angle / (2.0 * pi) * count - 0.5;
    const float behind = max(distance - occluderAt(light, place, true), 0.0);
    const float spread = light.shadow.x * behind / max(distance, 1e-3) / (2.0 * pi) * count;
    float lit = 0.0;
    for (int tap = -2; tap <= 2; ++tap)
    {
        lit += distance <= occluderAt(light, place + float(tap) * 0.5 * spread, true) + shadowBias2D ? 1.0 : 0.0;
    }
    return lit / 5.0;
}

// The same for a directional light, whose shadows reach `shadow.y` beyond their occluder.
float directionalShadow(Light2D light, float2 position)
{
    const float count = float(data.shadowResolution);
    const float2 offset = position - light.position.xy;
    const float width = max(light.position.w, 1e-3);
    const float place = (dot(offset, light.direction.zw) + width * 0.5) / width * count - 0.5;
    if (place < -0.5 || place > count - 0.5)
    {
        return 1.0;
    }
    const float along = dot(offset, light.direction.xy);
    const float reach = light.shadow.y;
    const float behind = max(along - occluderAt(light, place, false), 0.0);
    const float spread = light.shadow.x * behind / width * count;
    float lit = 0.0;
    for (int tap = -2; tap <= 2; ++tap)
    {
        const float occluder = occluderAt(light, place + float(tap) * 0.5 * spread, false);
        lit += along <= occluder + shadowBias2D || along > occluder + reach ? 1.0 : 0.0;
    }
    return lit / 5.0;
}

// The light the 2D lights of the frame give a point of a sprite, for its light mask and normal.
public float3 light2D(float3 position, float3 normal, bool mapped, uint lightMask)
{
    float3 total = float3(0.0);
    for (uint index = 0; index < data.lightCount; ++index)
    {
        const Light2D light = data.lights[index];
        if ((light.itemMask & lightMask) == 0)
        {
            continue;
        }
        float3 toLight;
        float amount = 1.0;
        if (light.kind == 0)
        {
            const float2 offset = light.position.xy - position.xy;
            const float distance = length(offset);
            const float radius = light.position.w;
            if (distance >= radius)
            {
                continue;
            }
            amount = pow(saturate(1.0 - distance / radius), light.color.w);
            toLight = normalize(float3(offset, max(light.position.z, 1e-3)));
            if (light.shadowOffset >= 0)
            {
                amount *= pointShadow(light, -offset, distance);
            }
        }
        else
        {
            const float height = light.position.z;
            toLight = normalize(float3(-light.direction.xy * sqrt(max(1.0 - height * height, 0.0)), height));
            if (light.shadowOffset >= 0)
            {
                amount *= directionalShadow(light, position.xy);
            }
        }
        if (mapped)
        {
            amount *= saturate(dot(normal, toLight));
        }
        total += light.color.rgb * amount;
    }
    return total;
}

// What leaves a fragment of a sprite of colour `color`, premultiplied: lit as a matte surface by the
// 3D lights when the sprite is lit, then, unless unshaded, tinted by the canvas and lit by the 2D
// lights through `normal`, in the world. An additive sprite leaves an alpha of zero.
public float4 shadeSprite(Sprite sprite, float4 color, float3 worldPosition, float viewDepth, float2 fragmentCoordinate,
                          float3 normal, bool mapped, bool unshaded, bool additive)
{
    let scene = data.scene;
    float3 luminance = color.rgb;
    if ((sprite.flags & flagLit) != 0)
    {
        // A matte surface along the +Z of its entity, turned towards the viewer.
        const float3 toCamera = normalize(scene->cameraPosition - worldPosition);
        float3 facing = normalize(mul(sprite.world, float4(0.0, 0.0, 1.0, 0.0)).xyz);
        if (dot(facing, toCamera) < 0.0)
        {
            facing = -facing;
        }
        Surface surface;
        surface.position = worldPosition;
        surface.normal = facing;
        surface.view = toCamera;
        surface.diffuseColor = color.rgb;
        surface.f0 = float3(0.0);
        surface.roughness = 1.0;
        surface.alpha = 1.0;
        surface.occlusion = 1.0;
        surface.viewDistance = viewDepth;
        surface.fragmentCoordinate = fragmentCoordinate;
        luminance = shadeSurface(scene, surface) * scene->exposure;
    }
    if (!unshaded && (sprite.flags & flagUnshaded) == 0)
    {
        // The 2D plane: the tint of the canvas, and the light of the 2D lights over it.
        luminance = luminance * data.canvasModulate.rgb + color.rgb * light2D(worldPosition, normal, mapped, sprite.lightMask);
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, additive || (sprite.flags & flagAdditive) != 0 ? 0.0 : alpha);
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
