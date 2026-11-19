// What sprites and tilemaps are drawn with for OpenGL, translated into GLSL with the shaders that import it, as
// webgpu_canvas.slang is for WebGPU: the frame of the sprites, the texture and the normal map of a run of them, the 2D
// lights and their shadows, and the light of the scene on lit sprites. gl_sprite.slang draws them. OpenGL ES 3.0 has no
// storage buffers: the 2D lights lie in a uniform block, at most 64 a frame as the engine gathers them, and the
// distances their shadows keep in a texture of floats, a row a light, read texel by texel.
module gl_canvas;

__exported import sprite_shape;
import gl_scene;
import surface_shading;

public static const uint maxLights2D = 64;
// How many distances a row of the texture of the shadows holds: those of one light.
public static const uint shadowRow = 1024;

public struct SpriteFrame
{
    public float4x4 viewProjection;
    // The tint of the canvas.
    public float4 canvasModulate;
    public uint lightCount;
    // How many distances each shadowed light keeps.
    public uint shadowResolution;
    public uint padding0;
    public uint padding1;
};

public struct Light2DData
{
    public Light2D lights[maxLights2D];
};

public ConstantBuffer<SpriteFrame> spriteFrame;
public ConstantBuffer<Light2DData> light2DData;
public Sampler2D shadows2D;
public Sampler2D spriteTexture;
public Sampler2D normalTexture;

// Where a fragment samples the textures of the sprite, and the slopes that choose their mip level, taken before any
// branch so that the seams of the borders and of the tiles pick the same level as the rest.
public struct SpriteSample
{
    public float2 uv;
    public float2 slopeX;
    public float2 slopeY;
};

public SpriteSample spriteSample(Sprite sprite, float2 local)
{
    uint width;
    uint height;
    spriteTexture.GetDimensions(width, height);
    const float2 uvPerMeter = spriteUvPerMeter(sprite);
    SpriteSample at;
    at.uv = spriteUv(sprite, local, float2(float(width), float(height)));
    at.slopeX = ddx(local) * uvPerMeter;
    at.slopeY = ddy(local) * uvPerMeter;
    return at;
}

// The texture of the run where a fragment samples it.
public float4 spriteTextureAt(SpriteSample at)
{
    return spriteTexture.SampleGrad(at.uv, at.slopeX, at.slopeY);
}

// Whether the sprite has a normal map that the 2D lights shine through.
public bool spriteMapped(Sprite sprite)
{
    return sprite.normalTexture != noTexture && sprite.texture != noTexture;
}

// The X and Y of the normal map of the sprite at a fragment, from -1 to 1; flat without one.
public float2 spriteNormalMap(Sprite sprite, SpriteSample at)
{
    if (!spriteMapped(sprite))
    {
        return float2(0.0);
    }
    return normalTexture.SampleGrad(at.uv, at.slopeX, at.slopeY).rg * 2.0 - 1.0;
}

// The 2D lights of the frame, from their uniform block and the texture of their shadows.
struct FrameLights2D : ILights2D
{
    uint count()
    {
        return spriteFrame.lightCount;
    }
    Light2D light(uint index)
    {
        return light2DData.lights[index];
    }
    float shadow(int index)
    {
        return shadows2D.Load(int3(index % int(shadowRow), index / int(shadowRow), 0)).r;
    }
    uint shadowResolution()
    {
        return spriteFrame.shadowResolution;
    }
};

// What leaves a fragment of a sprite of colour `color`, premultiplied: lit as a matte surface by the local lights it was
// given, `lights`, and by the sun and the environment when the sprite is lit, then, unless unshaded, tinted by the canvas
// and lit by the 2D lights through `normal`, in the world. An additive sprite leaves an alpha of zero.
public float4 shadeSprite(Sprite sprite, float4 color, float3 worldPosition, float viewDepth, float2 fragmentCoordinate,
                          float3 normal, bool mapped, bool unshaded, bool additive, uint2 lights)
{
    float3 luminance = color.rgb;
    if ((sprite.flags & flagLit) != 0)
    {
        // A matte surface along the +Z of its entity, turned towards the viewer.
        const float3 toCamera = normalize(scene.cameraPosition - worldPosition);
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
        luminance = surfaceLight(surface, lights) * scene.exposure;
    }
    if (!unshaded && (sprite.flags & flagUnshaded) == 0)
    {
        // The 2D plane: the tint of the canvas, and the light of the 2D lights over it.
        FrameLights2D frameLights;
        luminance = luminance * spriteFrame.canvasModulate.rgb +
                    color.rgb * spriteLight2D(frameLights, worldPosition, normal, mapped, sprite.lightMask);
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, additive || (sprite.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
