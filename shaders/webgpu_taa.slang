// Temporal antialiasing for WebGPU, compiled to WGSL, as taa.slang does for Vulkan: every frame is drawn with the
// projection moved by a fraction of a pixel, and this pass mixes it with what the previous frames resolved, followed to
// where each pixel has moved, bounded by the colours around it. WebGPU draws no motion of its own: a pixel follows the
// camera, from the depth of the scene through the matrices of the frame and of the one before, and the sky its
// direction through the two sky cameras.

import temporal;

struct TaaData
{
    // How much of the history a still pixel keeps, from 0 to 1.
    float blend;
    // Zero on the first frame, and whenever the history cannot be trusted.
    float historyValid;
    float texelWidth;
    float texelHeight;
    // The world from the clip space of this frame, without its jitter, and the clip space of the frame before.
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float4x4 skyInverseViewProjection;
    float4x4 previousSkyViewProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<TaaData> taa;
[[vk::binding(1, 0)]]
Texture2D sceneColor;
[[vk::binding(2, 0)]]
Texture2D historyTexture;
[[vk::binding(3, 0)]]
DepthTexture2D sceneDepth;
[[vk::binding(4, 0)]]
SamplerState clampSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput taaVertex(uint vertexIndex : SV_VertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.uv = uv;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}

// The clip space of WebGPU points Y up, texture coordinates down.
float4 clipOf(float2 uv, float depth)
{
    return float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
}

float2 uvOf(float4 clip)
{
    const float2 ndc = clip.xy / clip.w;
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

float depthAt(float2 uv)
{
    uint width;
    uint height;
    sceneDepth.GetDimensions(width, height);
    const int2 texel = clamp(int2(uv * float2(float(width), float(height))), int2(0), int2(int(width) - 1, int(height) - 1));
    return sceneDepth.Load(int3(texel, 0));
}

[shader("fragment")]
float4 taaFragment(VertexOutput input) : SV_Target
{
    const float2 texel = float2(taa.texelWidth, taa.texelHeight);
    const float3 current = weigh(sceneColor.Sample(clampSampler, input.uv).rgb);
    // The colours around the pixel say what it could reasonably be this frame, read by every pixel as WGSL wants it.
    float3 lowest = current;
    float3 highest = current;
    float2 closestUv = input.uv;
    float closestDepth = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 uv = input.uv + float2(float(x), float(y)) * texel;
            const float3 neighbor = weigh(sceneColor.Sample(clampSampler, uv).rgb);
            lowest = min(lowest, neighbor);
            highest = max(highest, neighbor);
            // The nearest neighbour, whose motion keeps the edges of moving surfaces sharp.
            const float depth = depthAt(uv);
            if (depth > closestDepth)
            {
                closestDepth = depth;
                closestUv = uv;
            }
        }
    }
    float2 historyUv;
    if (closestDepth > 0.0)
    {
        // Where the nearest surface stood in the frame before, the camera having moved.
        const float4 world = mul(taa.inverseViewProjection, clipOf(closestUv, closestDepth));
        const float2 previous = uvOf(mul(taa.previousViewProjection, float4(world.xyz / world.w, 1.0)));
        historyUv = input.uv - (closestUv - previous);
    }
    else
    {
        // The sky has no depth: it follows its direction through the two sky cameras, including the perspective
        // backdrop of an orthographic scene camera.
        const float4 world = mul(taa.skyInverseViewProjection, clipOf(input.uv, 1.0));
        const float4 previous = mul(taa.previousSkyViewProjection, float4(world.xyz / world.w, 0.0));
        historyUv = previous.w > 1e-6 ? uvOf(previous) : float2(-1.0);
    }
    const float3 history = clamp(weigh(historyTexture.Sample(clampSampler, historyUv).rgb), lowest, highest);
    if (taa.historyValid < 0.5 || any(historyUv < 0.0) || any(historyUv > 1.0))
    {
        return float4(unweigh(current), 1.0);
    }
    return float4(unweigh(lerp(current, history, taa.blend)), 1.0);
}
