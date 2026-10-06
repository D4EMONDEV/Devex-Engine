// Draws sprites among the blended surfaces of the scene: rectangles of a texture lying in the XY
// plane of their entity, facing +Z. A simple sprite stretches its rectangle over its quad; a sliced
// one keeps its borders at their size and stretches its middle, a tiled one repeats its middle.
// Colours are relative to the exposure, as the emission of materials, unless the sprite is lit as a
// matte surface. Unless it is unshaded, the canvas modulation tints it and the 2D lights add their
// light, through its normal map, hidden by the occluders whose distances their shadows keep. They
// leave premultiplied: additive sprites leave an alpha of zero, which the blending adds. The same
// quads report their object for picking, and cover the selection mask.

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
    public uint padding0;
};

// A light of the 2D plane, as render::vulkan::GpuLight2D.
struct Light2D
{
    // A point light: position, height and radius. A directional one: the middle of the line its
    // shadow strips start from, its height, and the width of the strips.
    float4 position;
    // Colour times energy, and how fast a point light fades.
    float4 color;
    // A directional light: the way it goes, then the line across it.
    float4 direction;
    // Softness of the shadows, how far those of a directional light reach, and the length of its strips.
    float4 shadow;
    uint kind;
    uint itemMask;
    // Where its distances start in the shadow buffer; -1 without shadows.
    int shadowOffset;
    uint padding;
};

static const uint modeMask = 3;
static const uint modeSimple = 0;
static const uint modeSliced = 1;
static const uint modeTiled = 2;
static const uint flagFlipX = 4;
static const uint flagFlipY = 8;
static const uint flagAdditive = 16;
static const uint flagLit = 32;
static const uint flagUnshaded = 64;
static const uint noTexture = 0xFFFFFFFF;

struct SpriteData
{
    SceneData* scene;
    Sprite* sprites;
    // The 2D lights of the frame, and the distances their shadows keep.
    Light2D* lights;
    float* shadows;
    // The tint of the canvas.
    float4 canvasModulate;
    // The first sprite of the draw: instances count from it.
    uint first;
    uint lightCount;
    // How many distances each shadowed light keeps.
    uint shadowResolution;
    uint padding0;
};

[[vk::push_constant]]
ConstantBuffer<SpriteData> data;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    // Meters from the bottom-left corner of the rectangle, before any flip.
    float2 local : TEXCOORD0;
    float viewDepth : TEXCOORD1;
    nointerpolation uint sprite : TEXCOORD2;
};

// The two triangles of the rectangle, from its bottom-left corner.
static const float2 corners[6] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                                   float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0) };

VertexOutput transform(float4x4 viewProjection, uint vertexIndex, uint instanceIndex)
{
    let scene = data.scene;
    const uint index = data.first + instanceIndex;
    const Sprite sprite = data.sprites[index];
    const float2 local = corners[vertexIndex] * sprite.size;
    // Flipping mirrors the sprite around its pivot.
    float2 position = local - sprite.pivot * sprite.size;
    if ((sprite.flags & flagFlipX) != 0)
    {
        position.x = -position.x;
    }
    if ((sprite.flags & flagFlipY) != 0)
    {
        position.y = -position.y;
    }
    const float3 world = mul(sprite.world, float4(position, 0.0, 1.0)).xyz;
    VertexOutput output;
    output.position = mul(viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.local = local;
    output.viewDepth = -mul(scene->view, float4(world, 1.0)).z;
    output.sprite = index;
    return output;
}

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
struct SpriteSample
{
    float2 uv;
    float2 slopeX;
    float2 slopeY;
};

SpriteSample spriteSample(Sprite sprite, float2 local)
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
float4 spriteColor(Sprite sprite, float2 local)
{
    const float4 color = sprite.color;
    if (sprite.texture == noTexture)
    {
        return color;
    }
    const SpriteSample at = spriteSample(sprite, local);
    return color * sampleTextureGrad(sprite.texture, at.uv, at.slopeX, at.slopeY);
}

// The normal of the sprite at a fragment, in the world: that of its normal map, mirrored as the
// sprite shows it and turned with its entity; +Z of its entity without one.
float3 spriteNormal(Sprite sprite, float2 local)
{
    float3 normal = float3(0.0, 0.0, 1.0);
    if (sprite.normalTexture != noTexture && sprite.texture != noTexture)
    {
        const SpriteSample at = spriteSample(sprite, local);
        const float2 encoded = sampleTextureGrad(sprite.normalTexture, at.uv, at.slopeX, at.slopeY).rg * 2.0 - 1.0;
        normal = float3(encoded, sqrt(saturate(1.0 - dot(encoded, encoded))));
        // A flipped sprite, or a tile whose coordinates run backwards, shows its normals mirrored.
        if (((sprite.flags & flagFlipX) != 0) != (sprite.uvRect.z < sprite.uvRect.x))
        {
            normal.x = -normal.x;
        }
        if (((sprite.flags & flagFlipY) != 0) != (sprite.uvRect.w < sprite.uvRect.y))
        {
            normal.y = -normal.y;
        }
    }
    return normalize(mul(sprite.world, float4(normal, 0.0)).xyz);
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
float3 light2D(float3 position, float3 normal, bool mapped, uint lightMask)
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

[shader("vertex")]
VertexOutput spriteVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    return transform(data.scene->viewProjection, vertexIndex, instanceIndex);
}

[shader("fragment")]
float4 spriteFragment(VertexOutput input) : SV_Target
{
    let scene = data.scene;
    const Sprite sprite = data.sprites[input.sprite];
    const float4 color = spriteColor(sprite, input.local);
    float3 luminance = color.rgb;
    if ((sprite.flags & flagLit) != 0)
    {
        // A matte surface along the +Z of its entity, turned towards the viewer.
        const float3 toCamera = normalize(scene->cameraPosition - input.worldPosition);
        float3 normal = normalize(mul(sprite.world, float4(0.0, 0.0, 1.0, 0.0)).xyz);
        if (dot(normal, toCamera) < 0.0)
        {
            normal = -normal;
        }
        Surface surface;
        surface.position = input.worldPosition;
        surface.normal = normal;
        surface.view = toCamera;
        surface.diffuseColor = color.rgb;
        surface.f0 = float3(0.0);
        surface.roughness = 1.0;
        surface.alpha = 1.0;
        surface.occlusion = 1.0;
        surface.viewDistance = input.viewDepth;
        surface.fragmentCoordinate = input.position.xy;
        luminance = shadeSurface(scene, surface) * scene->exposure;
    }
    if ((sprite.flags & flagUnshaded) == 0)
    {
        // The 2D plane: the tint of the canvas, and the light of the 2D lights over it.
        const bool mapped = sprite.normalTexture != noTexture && sprite.texture != noTexture;
        const float3 normal = mapped ? spriteNormal(sprite, input.local) : float3(0.0, 0.0, 1.0);
        luminance = luminance * data.canvasModulate.rgb + color.rgb * light2D(input.worldPosition, normal, mapped, sprite.lightMask);
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, (sprite.flags & flagAdditive) != 0 ? 0.0 : alpha);
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
