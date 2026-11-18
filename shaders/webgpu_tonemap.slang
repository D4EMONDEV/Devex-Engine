// Maps the exposed high dynamic range scene to the display for WebGPU, compiled to WGSL, as tonemap.slang does: the
// chromatic aberration, the bloom, the grading, the operators, the table of colours, the vignette and the grain. The
// image drawn into encodes sRGB itself, or this pass encodes it.

import tonemapping;

struct TonemapData
{
    // 0 AgX, 1 Khronos PBR Neutral, 2 ACES, 3 none.
    uint tonemapper;
    // How dark the corners of the image become, from 0 to 1.
    float vignette;
    // How much noise is added to every pixel.
    float grain;
    // Moves the noise from one frame to the next.
    float time;
    // The size of the image drawn into, smaller than the scene for a capture.
    float targetWidth;
    float targetHeight;
    // 1 when the image drawn into does not encode sRGB itself.
    uint encode;
    // The colour grading: saturation and contrast (1 leaves them).
    float saturation;
    float contrast;
    // How much of the halo of bright things is added back to the image.
    float bloom;
    // How far, in texture coordinates, red and blue are pulled apart at the edges.
    float chromatic;
    // How many steps the table of colours has; 0 leaves the image alone.
    float colorTableSize;
    // The filter the colours are multiplied by, and the factors of the white balance on the cones
    // of the eye.
    float4 filter;
    float4 balance;
};

[[vk::binding(0, 0)]]
ConstantBuffer<TonemapData> settings;
[[vk::binding(1, 0)]]
Texture2D sceneTexture;
[[vk::binding(2, 0)]]
SamplerState clampSampler;
[[vk::binding(3, 0)]]
Texture2D bloomTexture;
[[vk::binding(4, 0)]]
Texture2D colorTableTexture;

struct BoundTable : IColorTable
{
    float3 read(float2 uv)
    {
        return colorTableTexture.SampleLevel(clampSampler, uv, 0.0).rgb;
    }
};

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput tonemapVertex(uint vertexIndex : SV_VertexID)
{
    // One triangle over the whole image.
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 tonemapFragment(VertexOutput input) : SV_Target
{
    uint imageWidth;
    uint imageHeight;
    sceneTexture.GetDimensions(imageWidth, imageHeight);
    // A capture draws the whole scene into a smaller image, and a scene drawn in fewer pixels is
    // stretched over the window: both sample it rather than read its pixels one for one.
    const float2 targetSize = float2(settings.targetWidth, settings.targetHeight);
    const bool resized = targetSize.x != float(imageWidth) || targetSize.y != float(imageHeight);
    const float2 imageUv = (input.position.xy + 0.5) / targetSize;
    const float2 fromMiddle = imageUv - 0.5;
    const float3 sampled = sceneTexture.SampleLevel(clampSampler, input.position.xy / targetSize, 0.0).rgb;
    const float3 loaded = sceneTexture.Load(int3(int2(input.position.xy), 0)).rgb;
    // The halo, read by every pixel as WGSL wants it.
    const float3 halo = max(bloomTexture.Sample(clampSampler, imageUv).rgb, float3(0.0));
    float3 color = max(resized ? sampled : loaded, float3(0.0));
    if (settings.chromatic > 0.0)
    {
        // Lenses bend the colours apart, the more so away from the middle.
        const float2 shift = fromMiddle * settings.chromatic;
        color = max(float3(sceneTexture.SampleLevel(clampSampler, imageUv + shift, 0.0).r, sceneTexture.SampleLevel(clampSampler, imageUv, 0.0).g,
                           sceneTexture.SampleLevel(clampSampler, imageUv - shift, 0.0).b),
                    float3(0.0));
    }
    if (settings.bloom > 0.0)
    {
        color += halo * settings.bloom;
    }
    color = grade(color, settings.balance.rgb, settings.filter.rgb, settings.contrast, settings.saturation);
    float3 mapped = tonemap(color, settings.tonemapper);
    if (settings.colorTableSize > 1.0)
    {
        BoundTable table;
        mapped = applyColorTable(table, mapped, settings.colorTableSize);
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
        const float noise = frac(sin(dot(input.position.xy + settings.time, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
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
