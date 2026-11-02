// Temporal antialiasing: every frame is drawn with the projection moved by a fraction of a pixel,
// and this pass mixes it with what the previous frames resolved, followed to where each pixel has
// moved. The history is bounded by the colors around the pixel, which keeps moving objects from
// dragging the past behind them.

import common;

struct TaaData
{
    // How much of the history a still pixel keeps, from 0 to 1.
    float blend;
    // Zero on the first frame, and whenever the history cannot be trusted.
    float historyValid;
    float texelWidth;
    float texelHeight;
    SceneData* scene;
    float2 padding;
    float4x4 previousSkyViewProjection;
};

[[vk::push_constant]]
ConstantBuffer<TaaData> taa;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VertexOutput output;
    output.uv = fullscreenUv(vertexIndex);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Bright pixels weigh less, so that a single spark cannot drag the whole neighborhood with it.
float3 weigh(float3 color)
{
    return color / (1.0 + max(color.r, max(color.g, color.b)));
}

float3 unweigh(float3 color)
{
    return color / max(1.0 - max(color.r, max(color.g, color.b)), 1e-4);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float2 texel = float2(taa.texelWidth, taa.texelHeight);
    const float3 current = weigh(sceneColor.Sample(clampSampler, input.uv).rgb);
    if (taa.historyValid < 0.5)
    {
        return float4(unweigh(current), 1.0);
    }

    // The colors around the pixel say what it could reasonably be this frame.
    float3 lowest = current;
    float3 highest = current;
    float2 closestMotion = velocityTexture.Sample(clampSampler, input.uv).rg;
    float closestDepth = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 uv = input.uv + float2(x, y) * texel;
            const float3 neighbor = weigh(sceneColor.Sample(clampSampler, uv).rgb);
            lowest = min(lowest, neighbor);
            highest = max(highest, neighbor);
            // The motion of the nearest neighbor, which keeps the edges of moving objects sharp.
            const float depth = depthTexture.Sample(clampSampler, uv).r;
            if (depth > closestDepth)
            {
                closestDepth = depth;
                closestMotion = velocityTexture.Sample(clampSampler, uv).rg;
            }
        }
    }

    float2 historyUv = input.uv - closestMotion;
    if (closestDepth <= 0.0)
    {
        // The sky has no surface velocity or depth. Follow its world direction through the two
        // sky cameras, including the perspective backdrop used by an orthographic scene camera.
        const float4 world = mul(taa.scene->skyInverseViewProjection,
                                 float4(input.uv * 2.0 - 1.0, 1.0, 1.0));
        const float4 previous = mul(taa.previousSkyViewProjection, float4(world.xyz / world.w, 0.0));
        if (previous.w <= 1e-6)
        {
            return float4(unweigh(current), 1.0);
        }
        historyUv = previous.xy / previous.w * 0.5 + 0.5;
    }
    const bool outside = any(historyUv < 0.0) || any(historyUv > 1.0);
    if (outside)
    {
        return float4(unweigh(current), 1.0);
    }

    float3 history = weigh(historyTexture.Sample(clampSampler, historyUv).rgb);
    history = clamp(history, lowest, highest);
    return float4(unweigh(lerp(current, history, taa.blend)), 1.0);
}
