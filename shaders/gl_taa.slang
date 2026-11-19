// Temporal antialiasing for OpenGL, translated to GLSL ES 3.00 and GLSL 3.30, as webgpu_taa.slang does for WebGPU: every
// frame is drawn with the projection moved by a fraction of a pixel, and this pass mixes it with what the previous frames
// resolved, followed to where each pixel has moved, bounded by the colours around it. A pixel follows the camera, from
// the depth of the scene through the matrices of the frame and of the one before, and the sky its direction through the
// two sky cameras. The matrices are those OpenGL draws with, whose depth runs from -1 to 1 over the reversed depth of
// Vulkan that the depth buffer keeps; texture coordinates and clip space both start from the bottom row.

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

ConstantBuffer<TaaData> taa;
Sampler2D sceneColor;
Sampler2D historyTexture;
Sampler2D sceneDepth;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput taaVertex(uint vertexIndex : SV_VulkanVertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.uv = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// The clip space of OpenGL, from a depth of the buffer, from 0 to 1.
float4 clipOf(float2 uv, float depth)
{
    return float4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
}

float2 uvOf(float4 clip)
{
    return clip.xy / clip.w * 0.5 + 0.5;
}

float depthAt(float2 uv)
{
    uint width;
    uint height;
    sceneDepth.GetDimensions(width, height);
    const int2 texel = clamp(int2(uv * float2(float(width), float(height))), int2(0), int2(int(width) - 1, int(height) - 1));
    return sceneDepth.Load(int3(texel, 0)).r;
}

[shader("fragment")]
float4 taaFragment(VertexOutput input) : SV_Target
{
    const float2 texel = float2(taa.texelWidth, taa.texelHeight);
    const float3 current = weigh(sceneColor.SampleLevel(input.uv, 0.0).rgb);
    // The colours around the pixel say what it could reasonably be this frame.
    float3 lowest = current;
    float3 highest = current;
    float2 closestUv = input.uv;
    float closestDepth = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 uv = input.uv + float2(float(x), float(y)) * texel;
            const float3 neighbor = weigh(sceneColor.SampleLevel(uv, 0.0).rgb);
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
    const float3 history = clamp(weigh(historyTexture.SampleLevel(historyUv, 0.0).rgb), lowest, highest);
    if (taa.historyValid < 0.5 || any(historyUv < 0.0) || any(historyUv > 1.0))
    {
        return float4(unweigh(current), 1.0);
    }
    return float4(unweigh(lerp(current, history, taa.blend)), 1.0);
}
