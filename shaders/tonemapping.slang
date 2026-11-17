// The colour grading and the tonemapping operators, shared by the tonemap passes of Vulkan
// (tonemap.slang) and of WebGPU (webgpu_tonemap.slang): plain arithmetic, without resources.
module tonemapping;

// The colour grading of the exposed light, before the tonemapping, as Unity's Color Adjustments and
// White Balance: the white balance on the cones of the eye (LMS), the filter, the contrast around
// middle grey in a logarithmic space, then the saturation around the luminance.
public float3 grade(float3 color, float3 balance, float3 filter, float contrast, float saturation)
{
    const float3x3 toCones = float3x3(0.390405, 0.549941, 0.00892632, 0.0708416, 0.963172, 0.00135775, 0.0231082, 0.128021, 0.936245);
    const float3x3 fromCones = float3x3(2.85847, -1.62879, -0.024891, -0.210182, 1.1582, 0.000324281, -0.041812, -0.118169, 1.06867);
    if (any(balance != float3(1.0)))
    {
        color = max(mul(fromCones, mul(toCones, color) * balance), float3(0.0));
    }
    color *= filter;
    if (contrast != 1.0)
    {
        color = 0.18 * pow(max(color, float3(0.0)) / 0.18, float3(contrast));
    }
    const float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    return max(luminance + (color - luminance) * saturation, float3(0.0));
}

public float encodeChannel(float value)
{
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

// AgX, after Troy Sobotka's configuration for Blender, with the fitted sigmoid of its default look.
float3 agxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x -
           0.00232;
}

public float3 agx(float3 color)
{
    // Rows of these matrices multiply row vectors, matching the column-major GLSL originals.
    const float3x3 inset = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                    0.0784335999999992, 0.878468636469772, 0.0784336,
                                    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 outset = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                     -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                     -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    float3 value = mul(max(color, float3(1e-10)), inset);
    value = saturate((clamp(log2(value), minEv, maxEv) - minEv) / (maxEv - minEv));
    value = agxContrast(value);
    value = mul(value, outset);
    // The sigmoid produces display-encoded values; the swapchain expects linear ones.
    return pow(max(value, float3(0.0)), float3(2.2));
}

// Khronos PBR Neutral.
public float3 pbrNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }
    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak), g);
}

// Narkowicz's fit of the ACES reference rendering transform.
public float3 aces(float3 color)
{
    color *= 0.6;
    return saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
}

// The exposed and graded light mapped to the display, linear: 0 AgX, 1 Khronos PBR Neutral, 2 ACES,
// anything else none.
public float3 tonemap(float3 color, uint tonemapper)
{
    switch (tonemapper)
    {
    case 0:
        return agx(color);
    case 1:
        return pbrNeutral(color);
    case 2:
        return aces(color);
    default:
        return saturate(color);
    }
}

// A table of colours kept as a strip of squares, one per blue step, as Vulkan and WebGPU each bind it.
public interface IColorTable
{
    float3 read(float2 uv);
};

// The tonemapped colour through the table, read between the two nearest squares; a size of 0 leaves it alone.
public float3 applyColorTable<T : IColorTable>(T table, float3 color, float size)
{
    color = saturate(color);
    const float sliceSize = 1.0 / size;
    const float slicePixelSize = sliceSize / size;
    const float sliceInnerSize = slicePixelSize * (size - 1.0);
    const float lowSlice = min(floor(color.b * size), size - 1.0);
    const float highSlice = min(lowSlice + 1.0, size - 1.0);
    const float x = slicePixelSize * 0.5 + color.r * sliceInnerSize;
    const float y = (color.g * (size - 1.0) + 0.5) / size;
    const float3 low = table.read(float2(x + lowSlice * sliceSize, y));
    const float3 high = table.read(float2(x + highSlice * sliceSize, y));
    return lerp(low, high, frac(color.b * size));
}
