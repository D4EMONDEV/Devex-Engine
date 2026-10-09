// What the shaders of a project build on: their textures, their uniforms and their constants. The
// code Devex generates around a .dvxshader file imports this module with common and pbr, and the
// module of what the shader draws: canvas for canvas items, particles for particles.
module custom;

import common;

public static const float PI = 3.14159265358979;
public static const float TAU = 6.28318530717959;
public static const float E = 2.71828182845905;

// A texture of the project, as a uniform names it: an index of the texture array, whose sampler
// goes with it.
public struct sampler2D
{
    public uint index;

    public __init(uint index)
    {
        this.index = index;
    }
};

// Samples the texture. Fragment shaders choose the mip level from the slopes of the coordinates;
// vertex shaders, which have none, sample the largest.
public float4 texture(sampler2D sampler, float2 uv)
{
    float4 result = float4(0.0);
    __stage_switch
    {
    case fragment:
        result = sampleTexture(sampler.index, uv);
    case vertex:
        result = sampleTextureLevel(sampler.index, uv, 0.0);
    }
    return result;
}

public float4 textureLod(sampler2D sampler, float2 uv, float lod)
{
    return sampleTextureLevel(sampler.index, uv, lod);
}

// The size of a mip level, in pixels.
public int2 textureSize(sampler2D sampler, int lod)
{
    uint width;
    uint height;
    uint levels;
    textures[NonUniformResourceIndex(sampler.index)].GetDimensions(uint(max(lod, 0)), width, height, levels);
    return int2(int(width), int(height));
}

// A pixel of a mip level, without filtering.
public float4 texelFetch(sampler2D sampler, int2 texel, int lod)
{
    return textures[NonUniformResourceIndex(sampler.index)].Load(int3(texel, lod));
}

// The slot of four numbers that a uniform of the shader of a material takes.
public float4 materialSlot(SceneData* scene, uint material, uint slot)
{
    return scene->materialParameters[scene->materials[material].parameterOffset + slot];
}

// An object that gives no values to the instance uniforms of its shader.
public static const uint noInstanceParameters = 0xFFFFFFFFu;

// An instance uniform: the value the object gives, the `index`th of its values, or the default the
// slot of the material holds when it gives none.
public float4 instanceSlot(SceneData* scene, uint material, uint instance, uint slot, uint index)
{
    return instance != noInstanceParameters ? scene->instanceParameters[instance + index] : materialSlot(scene, material, slot);
}

// The texture a slot names: its index, as the bits of the first number.
public sampler2D slotTexture(float4 slot)
{
    return sampler2D(asuint(slot.x));
}
