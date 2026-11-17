// What the shaders of a project build on for WebGPU, as custom.slang does for Vulkan: their textures, their uniforms
// and their constants. WebGPU has no bindless textures: a material binds the textures of its shader in its group, at
// most eight, each with the sampler that filters it as it asks, and the slot of a texture uniform holds the number of
// its binding there; the slots of the uniforms are a buffer of the material, and the values objects give their
// instance uniforms a buffer of the frame.
module webgpu_custom;

public static const float PI = 3.14159265358979;
public static const float TAU = 6.28318530717959;
public static const float E = 2.71828182845905;

public static const uint customTextureCount = 8;
public static const uint customSlotCount = 64;

public struct CustomSlots
{
    public float4 slots[64];
};

[[vk::binding(0, 1)]]
public ConstantBuffer<CustomSlots> customMaterial;
[[vk::binding(1, 1)]]
public Texture2D customTexture0;
[[vk::binding(2, 1)]]
public Texture2D customTexture1;
[[vk::binding(3, 1)]]
public Texture2D customTexture2;
[[vk::binding(4, 1)]]
public Texture2D customTexture3;
[[vk::binding(5, 1)]]
public Texture2D customTexture4;
[[vk::binding(6, 1)]]
public Texture2D customTexture5;
[[vk::binding(7, 1)]]
public Texture2D customTexture6;
[[vk::binding(8, 1)]]
public Texture2D customTexture7;
[[vk::binding(9, 1)]]
public SamplerState customSampler0;
[[vk::binding(10, 1)]]
public SamplerState customSampler1;
[[vk::binding(11, 1)]]
public SamplerState customSampler2;
[[vk::binding(12, 1)]]
public SamplerState customSampler3;
[[vk::binding(13, 1)]]
public SamplerState customSampler4;
[[vk::binding(14, 1)]]
public SamplerState customSampler5;
[[vk::binding(15, 1)]]
public SamplerState customSampler6;
[[vk::binding(16, 1)]]
public SamplerState customSampler7;
// The values objects give the instance uniforms of their shader, in the group of the frame.
[[vk::binding(18, 0)]]
public StructuredBuffer<float4> customInstanceParameters;

// A texture of the material, as a uniform names it: the number of its binding.
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
        return customTexture0.SampleLevel(customSampler0, uv, lod);
    case 1:
        return customTexture1.SampleLevel(customSampler1, uv, lod);
    case 2:
        return customTexture2.SampleLevel(customSampler2, uv, lod);
    case 3:
        return customTexture3.SampleLevel(customSampler3, uv, lod);
    case 4:
        return customTexture4.SampleLevel(customSampler4, uv, lod);
    case 5:
        return customTexture5.SampleLevel(customSampler5, uv, lod);
    case 6:
        return customTexture6.SampleLevel(customSampler6, uv, lod);
    default:
        return customTexture7.SampleLevel(customSampler7, uv, lod);
    }
}

// At the mip level the slopes of the coordinates give, which only fragments have. The slopes are taken before the
// texture is chosen: WGSL counts the number of a texture read from a uniform as varying from pixel to pixel.
float4 sampleWithSlopes(uint index, float2 uv)
{
    const float2 slopeX = ddx(uv);
    const float2 slopeY = ddy(uv);
    switch (index)
    {
    case 0:
        return customTexture0.SampleGrad(customSampler0, uv, slopeX, slopeY);
    case 1:
        return customTexture1.SampleGrad(customSampler1, uv, slopeX, slopeY);
    case 2:
        return customTexture2.SampleGrad(customSampler2, uv, slopeX, slopeY);
    case 3:
        return customTexture3.SampleGrad(customSampler3, uv, slopeX, slopeY);
    case 4:
        return customTexture4.SampleGrad(customSampler4, uv, slopeX, slopeY);
    case 5:
        return customTexture5.SampleGrad(customSampler5, uv, slopeX, slopeY);
    case 6:
        return customTexture6.SampleGrad(customSampler6, uv, slopeX, slopeY);
    default:
        return customTexture7.SampleGrad(customSampler7, uv, slopeX, slopeY);
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

// The size of a mip level, in pixels.
public int2 textureSize(sampler2D sampler, int lod)
{
    uint width = 1;
    uint height = 1;
    uint levels = 1;
    const uint level = uint(max(lod, 0));
    switch (sampler.index)
    {
    case 0:
        customTexture0.GetDimensions(level, width, height, levels);
        break;
    case 1:
        customTexture1.GetDimensions(level, width, height, levels);
        break;
    case 2:
        customTexture2.GetDimensions(level, width, height, levels);
        break;
    case 3:
        customTexture3.GetDimensions(level, width, height, levels);
        break;
    case 4:
        customTexture4.GetDimensions(level, width, height, levels);
        break;
    case 5:
        customTexture5.GetDimensions(level, width, height, levels);
        break;
    case 6:
        customTexture6.GetDimensions(level, width, height, levels);
        break;
    default:
        customTexture7.GetDimensions(level, width, height, levels);
        break;
    }
    return int2(int(width), int(height));
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
    default:
        return customTexture7.Load(at);
    }
}

// The slot of four numbers that a uniform of the shader of a material takes; the scene and the material are those of
// the code Devex generates for Vulkan, which WebGPU binds instead.
public float4 materialSlot(uint scene, uint material, uint slot)
{
    return customMaterial.slots[min(slot, customSlotCount - 1)];
}

// An object that gives no values to the instance uniforms of its shader.
public static const uint noInstanceParameters = 0xFFFFFFFFu;

// An instance uniform: the value the object gives, the `index`th of its values, or the default the slot of the material
// holds when it gives none.
public float4 instanceSlot(uint scene, uint material, uint instance, uint slot, uint index)
{
    return instance != noInstanceParameters ? customInstanceParameters[instance + index] : materialSlot(scene, material, slot);
}

// The texture a slot names: the number of its binding, as the bits of the first number.
public sampler2D slotTexture(float4 slot)
{
    return sampler2D(asuint(slot.x));
}
