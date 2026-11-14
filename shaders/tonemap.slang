// Maps the exposed high dynamic range scene color to the display. An sRGB swapchain encodes the
// output itself; for one that does not, it is encoded here.

import common;
import tonemapping;

struct TonemapData
{
    // 0 AgX, 1 Khronos PBR Neutral, 2 ACES, 3 none.
    uint tonemapper;
    // How much of the halo of bright things is added back to the image.
    float bloom;
    // How dark the corners of the image become, from 0 to 1.
    float vignette;
    // How much noise is added to every pixel.
    float grain;
    // How far, in texture coordinates, red and blue are pulled apart at the edges.
    float chromatic;
    // Moves the noise from one frame to the next.
    float time;
    // The table of colors, and how many steps it has; a size of 0 leaves the image alone.
    uint colorTable;
    float colorTableSize;
    // The size of the image drawn into, smaller than the scene for a capture.
    float targetWidth;
    float targetHeight;
    // 1 when the image drawn into does not encode sRGB itself.
    uint encode;
    // The colour grading: saturation and contrast (1 leaves them), the filter the colours are
    // multiplied by, and the factors of the white balance on the cones of the eye.
    float saturation;
    float contrast;
    float filterRed;
    float filterGreen;
    float filterBlue;
    float balanceLong;
    float balanceMedium;
    float balanceShort;
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

// A table of colors kept as a strip of squares, one per blue step, read between the two nearest.
float3 applyColorTable(float3 color, uint slot, float size)
{
    color = saturate(color);
    const float sliceSize = 1.0 / size;
    const float slicePixelSize = sliceSize / size;
    const float sliceInnerSize = slicePixelSize * (size - 1.0);
    const float lowSlice = min(floor(color.b * size), size - 1.0);
    const float highSlice = min(lowSlice + 1.0, size - 1.0);
    const float x = slicePixelSize * 0.5 + color.r * sliceInnerSize;
    const float y = (color.g * (size - 1.0) + 0.5) / size;
    const float3 low = textures[NonUniformResourceIndex(slot)]
                           .Sample(clampSampler, float2(x + lowSlice * sliceSize, y))
                           .rgb;
    const float3 high = textures[NonUniformResourceIndex(slot)]
                            .Sample(clampSampler, float2(x + highSlice * sliceSize, y))
                            .rgb;
    return lerp(low, high, frac(color.b * size));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    uint imageWidth;
    uint imageHeight;
    resolvedTexture.GetDimensions(imageWidth, imageHeight);
    // A capture draws the whole scene into a smaller image, which samples it rather than reading
    // its pixels one for one.
    const float2 targetSize = float2(settings.targetWidth, settings.targetHeight);
    const bool smaller = targetSize.x < float(imageWidth) || targetSize.y < float(imageHeight);
    const float2 imageUv = (input.position.xy + 0.5) / targetSize;
    // The middle of the image, from which the corners are measured.
    const float2 fromMiddle = imageUv - 0.5;

    float3 color;
    if (settings.chromatic > 0.0)
    {
        // Lenses bend the colors apart, the more so away from the middle.
        const float2 shift = fromMiddle * settings.chromatic;
        color = float3(resolvedTexture.Sample(clampSampler, imageUv + shift).r,
                       resolvedTexture.Sample(clampSampler, imageUv).g,
                       resolvedTexture.Sample(clampSampler, imageUv - shift).b);
    }
    else if (smaller)
    {
        color = resolvedTexture.Sample(clampSampler, input.position.xy / targetSize).rgb;
    }
    else
    {
        color = resolvedTexture.Load(int3(int2(input.position.xy), 0)).rgb;
    }
    color = max(color, float3(0.0));
    if (settings.bloom > 0.0)
    {
        color += max(bloomChain[0].Sample(clampSampler, imageUv).rgb, float3(0.0)) * settings.bloom;
    }
    color = grade(color, float3(settings.balanceLong, settings.balanceMedium, settings.balanceShort),
                  float3(settings.filterRed, settings.filterGreen, settings.filterBlue), settings.contrast,
                  settings.saturation);
    float3 mapped = tonemap(color, settings.tonemapper);
    if (settings.colorTableSize > 1.0)
    {
        mapped = applyColorTable(mapped, settings.colorTable, settings.colorTableSize);
    }
    if (settings.vignette > 0.0)
    {
        // Darkens with the square of the distance to the middle, as a lens does.
        const float distance = saturate(length(fromMiddle) * 1.4142136);
        mapped *= 1.0 - settings.vignette * distance * distance;
    }
    if (settings.grain > 0.0)
    {
        // Film grain: a little noise, stronger in the shadows where the eye forgives it.
        const float noise = frac(sin(dot(input.position.xy + settings.time,
                                         float2(12.9898, 78.233))) *
                                 43758.5453) -
                            0.5;
        const float luminance = dot(mapped, float3(0.2126, 0.7152, 0.0722));
        mapped += noise * settings.grain * (1.0 - luminance * 0.7);
    }
    mapped = saturate(mapped);
    if (settings.encode != 0)
    {
        mapped = float3(encodeChannel(mapped.r), encodeChannel(mapped.g), encodeChannel(mapped.b));
    }
    return float4(mapped, 1.0);
}
