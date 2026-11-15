// What sprites and tilemaps are drawn with: the sprites of the frame, their mapping onto their
// texture, the 2D lights and their shadows. The sprite shader draws them, and the canvas_item shaders
// of projects build on it.
module canvas;

__exported import sprite_shape;
import common;
import pbr;

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

// Where a fragment samples the textures of the sprite, and the slopes that choose their mip level.
public struct SpriteSample
{
    public float2 uv;
    public float2 slopeX;
    public float2 slopeY;
};

public SpriteSample spriteSample(Sprite sprite, float2 local)
{
    uint textureWidth;
    uint textureHeight;
    textures[NonUniformResourceIndex(sprite.texture)].GetDimensions(textureWidth, textureHeight);
    const float2 uvPerMeter = spriteUvPerMeter(sprite);
    SpriteSample at;
    at.uv = spriteUv(sprite, local, float2(float(textureWidth), float(textureHeight)));
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

// The 2D lights of the frame, as the push constants of the sprites point at them.
struct FrameLights2D : ILights2D
{
    uint count()
    {
        return data.lightCount;
    }
    Light2D light(uint index)
    {
        return data.lights[index];
    }
    float shadow(int index)
    {
        return data.shadows[index];
    }
    uint shadowResolution()
    {
        return data.shadowResolution;
    }
};

// The light the 2D lights of the frame give a point of a sprite, for its light mask and normal.
public float3 light2D(float3 position, float3 normal, bool mapped, uint lightMask)
{
    FrameLights2D frame;
    return spriteLight2D(frame, position, normal, mapped, lightMask);
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
