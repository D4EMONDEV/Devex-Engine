// What sprites and tilemaps are drawn with for WebGPU, as canvas.slang is for Vulkan: the sprites of the frame, the
// texture and the normal map of a run of them, the 2D lights and their shadows, and the light of the scene on lit
// sprites. webgpu_sprite.slang draws them, and the canvas_item shaders of projects build on it. The scene is group 0,
// without the decals, which sprites never read; the material of a shader of the project is group 1, empty for the
// others; the sprites group 2, where each run finds where it starts at a dynamic offset of its draw; the texture and
// the normal map of the run, bound with their samplers, group 3.
module webgpu_canvas;

__exported import sprite_shape;
import surface_shading;
import webgpu_scene;

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

public struct SpriteDraw
{
    // The first sprite of the run: instances count from it.
    public uint first;
    public uint padding0;
    public uint padding1;
    public uint padding2;
};

[[vk::binding(0, 2)]]
public ConstantBuffer<SpriteFrame> spriteFrame;
[[vk::binding(1, 2)]]
public StructuredBuffer<Sprite> sprites;
[[vk::binding(2, 2)]]
public ConstantBuffer<SpriteDraw> spriteDraw;
[[vk::binding(3, 2)]]
public StructuredBuffer<Light2D> lights2D;
[[vk::binding(4, 2)]]
public StructuredBuffer<float> shadows2D;
[[vk::binding(0, 3)]]
public Texture2D spriteTexture;
[[vk::binding(1, 3)]]
public SamplerState spriteSampler;
[[vk::binding(2, 3)]]
public Texture2D normalTexture;
[[vk::binding(3, 3)]]
public SamplerState normalSampler;

// Where a fragment samples the textures of the sprite, and the slopes that choose their mip level. The slopes are taken
// where every pixel of a quad goes the same way, as WGSL wants them: before any branch.
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

// The texture of the run where a fragment samples it; white for a sprite without one.
public float4 spriteTextureAt(SpriteSample at)
{
    return spriteTexture.SampleGrad(spriteSampler, at.uv, at.slopeX, at.slopeY);
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
    return normalTexture.SampleGrad(normalSampler, at.uv, at.slopeX, at.slopeY).rg * 2.0 - 1.0;
}

// The 2D lights of the frame, from its storage buffers.
struct FrameLights2D : ILights2D
{
    uint count()
    {
        return spriteFrame.lightCount;
    }
    Light2D light(uint index)
    {
        return lights2D[index];
    }
    float shadow(int index)
    {
        return shadows2D[index];
    }
    uint shadowResolution()
    {
        return spriteFrame.shadowResolution;
    }
};

// What leaves a fragment of a sprite of colour `color`, premultiplied: lit as a matte surface by the 3D lights when the
// sprite is lit, then, unless unshaded, tinted by the canvas and lit by the 2D lights through `normal`, in the world.
// An additive sprite leaves an alpha of zero.
public float4 shadeSprite(Sprite sprite, float4 color, float3 worldPosition, float viewDepth, float2 fragmentCoordinate,
                          float3 normal, bool mapped, bool unshaded, bool additive)
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
        luminance = surfaceLight(surface) * scene.exposure;
    }
    if (!unshaded && (sprite.flags & flagUnshaded) == 0)
    {
        // The 2D plane: the tint of the canvas, and the light of the 2D lights over it.
        FrameLights2D lights;
        luminance = luminance * spriteFrame.canvasModulate.rgb +
                    color.rgb * spriteLight2D(lights, worldPosition, normal, mapped, sprite.lightMask);
    }
    const float alpha = saturate(color.a);
    return float4(luminance * alpha, additive || (sprite.flags & flagAdditive) != 0 ? 0.0 : alpha);
}
