// Maps the exposed high dynamic range scene color to the display. The swapchain encodes sRGB, so
// the output stays linear.

import common;

struct TonemapData
{
    // 0 AgX, 1 Khronos PBR Neutral, 2 ACES, 3 none.
    uint tonemapper;
};

[[vk::push_constant]]
ConstantBuffer<TonemapData> settings;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(fullscreenUv(vertexIndex) * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// AgX, after Troy Sobotka's configuration for Blender, with the fitted sigmoid of its default look.
float3 agxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x -
           0.00232;
}

float3 agx(float3 color)
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
float3 pbrNeutral(float3 color)
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
float3 aces(float3 color)
{
    color *= 0.6;
    return saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 color = max(sceneColor.Load(int3(int2(input.position.xy), 0)).rgb, float3(0.0));
    float3 mapped;
    switch (settings.tonemapper)
    {
    case 0:
        mapped = agx(color);
        break;
    case 1:
        mapped = pbrNeutral(color);
        break;
    case 2:
        mapped = aces(color);
        break;
    default:
        mapped = saturate(color);
        break;
    }
    return float4(mapped, 1.0);
}
