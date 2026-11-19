// What the shaders of a project build on for OpenGL, as webgpu_custom.slang does for WebGPU: their textures, their uniforms
// and their constants. A material binds the textures of its shader, at most eight, each with the sampler that filters it
// as it asks, and the slot of a texture uniform holds its number among them; the slots of the uniforms are a uniform
// block of the material. OpenGL ES 3.0 has no storage buffers: the values objects give their instance uniforms are a
// uniform block of the frame, at most 1024. The draw binds its own textures beside them: the texture and the normal map
// of a run of sprites, which TEXTURE and NORMAL_TEXTURE name in canvas_item shaders, compiled with DEVEX_CANVAS_ITEM; the
// texture of the particles, TEXTURE in particles shaders, compiled with DEVEX_PARTICLES.
module gl_custom;

#if defined(DEVEX_CANVAS_ITEM)
import gl_canvas;
#elif defined(DEVEX_PARTICLES)
import gl_particles;
#endif

public static const float PI = 3.14159265358979;
public static const float TAU = 6.28318530717959;
public static const float E = 2.71828182845905;

public static const uint customTextureCount = 8;
public static const uint customSlotCount = 64;
public static const uint maxInstanceParameters = 1024;
// The textures of the draw, after those of the material.
public static const uint drawTexture = 8;
public static const uint drawNormalTexture = 9;

public struct CustomSlots
{
    public float4 slots[64];
};

public struct CustomInstances
{
    public float4 values[1024];
};

public ConstantBuffer<CustomSlots> customMaterial;
public ConstantBuffer<CustomInstances> customInstances;
public Sampler2D customTexture0;
public Sampler2D customTexture1;
public Sampler2D customTexture2;
public Sampler2D customTexture3;
public Sampler2D customTexture4;
public Sampler2D customTexture5;
public Sampler2D customTexture6;
public Sampler2D customTexture7;

// A texture of the material, as a uniform names it: its number among them.
public struct sampler2D
{
    public uint index;

    public __init(uint index)
    {
        this.index = index;
    }
};

// At a mip level given, which every stage may read.
float4 sampleAtLevel(uint index, float2 uv, float lod)
{
    switch (index)
    {
    case 0:
        return customTexture0.SampleLevel(uv, lod);
    case 1:
        return customTexture1.SampleLevel(uv, lod);
    case 2:
        return customTexture2.SampleLevel(uv, lod);
    case 3:
        return customTexture3.SampleLevel(uv, lod);
    case 4:
        return customTexture4.SampleLevel(uv, lod);
    case 5:
        return customTexture5.SampleLevel(uv, lod);
    case 6:
        return customTexture6.SampleLevel(uv, lod);
#if defined(DEVEX_CANVAS_ITEM)
    case 8: // drawTexture
        return spriteTexture.SampleLevel(uv, lod);
    case 9: // drawNormalTexture
        return normalTexture.SampleLevel(uv, lod);
#elif defined(DEVEX_PARTICLES)
    case 8: // drawTexture
        return particleTexture.SampleLevel(uv, lod);
#endif
    default:
        return customTexture7.SampleLevel(uv, lod);
    }
}

// At the mip level the slopes of the coordinates give, which only fragments have. The slopes are taken before the
// texture is chosen, where every pixel of a quad is.
float4 sampleWithSlopes(uint index, float2 uv)
{
    const float2 slopeX = ddx(uv);
    const float2 slopeY = ddy(uv);
    switch (index)
    {
    case 0:
        return customTexture0.SampleGrad(uv, slopeX, slopeY);
    case 1:
        return customTexture1.SampleGrad(uv, slopeX, slopeY);
    case 2:
        return customTexture2.SampleGrad(uv, slopeX, slopeY);
    case 3:
        return customTexture3.SampleGrad(uv, slopeX, slopeY);
    case 4:
        return customTexture4.SampleGrad(uv, slopeX, slopeY);
    case 5:
        return customTexture5.SampleGrad(uv, slopeX, slopeY);
    case 6:
        return customTexture6.SampleGrad(uv, slopeX, slopeY);
#if defined(DEVEX_CANVAS_ITEM)
    case 8: // drawTexture
        return spriteTexture.SampleGrad(uv, slopeX, slopeY);
    case 9: // drawNormalTexture
        return normalTexture.SampleGrad(uv, slopeX, slopeY);
#elif defined(DEVEX_PARTICLES)
    case 8: // drawTexture
        return particleTexture.SampleGrad(uv, slopeX, slopeY);
#endif
    default:
        return customTexture7.SampleGrad(uv, slopeX, slopeY);
    }
}

// Samples the texture. Fragment shaders choose the mip level from the slopes of the coordinates; vertex shaders, which
// have none, sample the largest.
public float4 texture(sampler2D sampler, float2 uv)
{
    float4 result = float4(0.0);
    __stage_switch
    {
    case fragment:
        result = sampleWithSlopes(sampler.index, uv);
    case vertex:
        result = sampleAtLevel(sampler.index, uv, 0.0);
    }
    return result;
}

public float4 textureLod(sampler2D sampler, float2 uv, float lod)
{
    return sampleAtLevel(sampler.index, uv, lod);
}

// The size of a mip level, in pixels: GLSL ES 3.00 cannot ask a texture how many levels it has, but halves the largest.
public int2 textureSize(sampler2D sampler, int lod)
{
    uint width = 1;
    uint height = 1;
    switch (sampler.index)
    {
    case 0:
        customTexture0.GetDimensions(width, height);
        break;
    case 1:
        customTexture1.GetDimensions(width, height);
        break;
    case 2:
        customTexture2.GetDimensions(width, height);
        break;
    case 3:
        customTexture3.GetDimensions(width, height);
        break;
    case 4:
        customTexture4.GetDimensions(width, height);
        break;
    case 5:
        customTexture5.GetDimensions(width, height);
        break;
    case 6:
        customTexture6.GetDimensions(width, height);
        break;
#if defined(DEVEX_CANVAS_ITEM)
    case 8: // drawTexture
        spriteTexture.GetDimensions(width, height);
        break;
    case 9: // drawNormalTexture
        normalTexture.GetDimensions(width, height);
        break;
#elif defined(DEVEX_PARTICLES)
    case 8: // drawTexture
        particleTexture.GetDimensions(width, height);
        break;
#endif
    default:
        customTexture7.GetDimensions(width, height);
        break;
    }
    const uint level = uint(clamp(lod, 0, 31));
    return int2(int(max(width >> level, 1u)), int(max(height >> level, 1u)));
}

// A pixel of a mip level, without filtering.
public float4 texelFetch(sampler2D sampler, int2 texel, int lod)
{
    const int3 at = int3(texel, lod);
    switch (sampler.index)
    {
    case 0:
        return customTexture0.Load(at);
    case 1:
        return customTexture1.Load(at);
    case 2:
        return customTexture2.Load(at);
    case 3:
        return customTexture3.Load(at);
    case 4:
        return customTexture4.Load(at);
    case 5:
        return customTexture5.Load(at);
    case 6:
        return customTexture6.Load(at);
#if defined(DEVEX_CANVAS_ITEM)
    case 8: // drawTexture
        return spriteTexture.Load(at);
    case 9: // drawNormalTexture
        return normalTexture.Load(at);
#elif defined(DEVEX_PARTICLES)
    case 8: // drawTexture
        return particleTexture.Load(at);
#endif
    default:
        return customTexture7.Load(at);
    }
}

// The slot of four numbers that a uniform of the shader of a material takes; the scene and the material are those of the
// code Devex generates for Vulkan, which OpenGL binds instead.
public float4 materialSlot(uint scene, uint material, uint slot)
{
    return customMaterial.slots[min(slot, customSlotCount - 1)];
}

// An object that gives no values to the instance uniforms of its shader.
public static const uint noInstanceParameters = 0xFFFFFFFFu;

// An instance uniform: the value the object gives, the `index`th of its values, or the default the slot of the material
// holds when it gives none, or when the frame gave more than OpenGL holds.
public float4 instanceSlot(uint scene, uint material, uint instance, uint slot, uint index)
{
    return instance != noInstanceParameters && instance + index < maxInstanceParameters ? customInstances.values[instance + index]
                                                                                        : materialSlot(scene, material, slot);
}

// The texture a slot names: its number among those of the material, as the bits of the first number.
public sampler2D slotTexture(float4 slot)
{
    return sampler2D(asuint(slot.x));
}
