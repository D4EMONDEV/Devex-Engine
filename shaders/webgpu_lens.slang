// What a camera's lens and shutter do to the image for WebGPU, compiled to WGSL: the depth of field and the motion blur
// of lens_filter.slang, as lens.slang draws them for Vulkan. WebGPU draws no motion of its own: a pixel follows the
// camera, from the depth of the scene through the matrices of the frame and of the one before, as its temporal
// antialiasing does, and the sky its direction through the two sky cameras.

import lens_filter;

struct LensData
{
    // As LensSettings has them.
    float targetWidth;
    float targetHeight;
    float imageWidth;
    float imageHeight;
    float aperture;
    float focalLength;
    float focusDistance;
    float amount;
    float maxRadius;
    uint autoFocus;
    float shutter;
    float tileSize;
    uint fromDepthOfField;
    uint frame;
    float padding1;
    float padding2;
    // The world from the clip space of this frame, without its jitter, the clip space of the frame before, and the
    // view of the camera; the same two for the sky.
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float4x4 view;
    float4x4 skyInverseViewProjection;
    float4x4 previousSkyViewProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<LensData> lens;
[[vk::binding(1, 0)]]
Texture2D sourceImage;
[[vk::binding(2, 0)]]
DepthTexture2D sceneDepth;
[[vk::binding(3, 0)]]
Texture2D circleOfConfusionImage;
[[vk::binding(4, 0)]]
Texture2D halvedImage;
[[vk::binding(5, 0)]]
Texture2D bokehImage;
[[vk::binding(6, 0)]]
Texture2D filteredImage;
[[vk::binding(7, 0)]]
Texture2D focusedImage;
[[vk::binding(8, 0)]]
Texture2D tilesImage;
[[vk::binding(9, 0)]]
Texture2D neighboursImage;
[[vk::binding(10, 0)]]
SamplerState clampSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput lensVertex(uint vertexIndex : SV_VertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.uv = uv;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}

LensSettings settings()
{
    LensSettings result;
    result.targetWidth = lens.targetWidth;
    result.targetHeight = lens.targetHeight;
    result.imageWidth = lens.imageWidth;
    result.imageHeight = lens.imageHeight;
    result.aperture = lens.aperture;
    result.focalLength = lens.focalLength;
    result.focusDistance = lens.focusDistance;
    result.amount = lens.amount;
    result.maxRadius = lens.maxRadius;
    result.autoFocus = lens.autoFocus;
    result.shutter = lens.shutter;
    result.tileSize = lens.tileSize;
    result.fromDepthOfField = lens.fromDepthOfField;
    result.frame = lens.frame;
    return result;
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

struct BoundLens : ILensImages
{
    float4 source(float2 uv)
    {
        return sourceImage.SampleLevel(clampSampler, uv, 0.0);
    }

    float4 sourceAt(int2 pixel)
    {
        return sourceImage.Load(int3(pixel, 0));
    }

    float4 chain(uint image, float2 uv)
    {
        switch (image)
        {
        case lensCircleOfConfusion:
            return circleOfConfusionImage.SampleLevel(clampSampler, uv, 0.0);
        case lensHalved:
            return halvedImage.SampleLevel(clampSampler, uv, 0.0);
        case lensBokeh:
            return bokehImage.SampleLevel(clampSampler, uv, 0.0);
        case lensFiltered:
            return filteredImage.SampleLevel(clampSampler, uv, 0.0);
        case lensFocused:
            return focusedImage.SampleLevel(clampSampler, uv, 0.0);
        case lensTiles:
            return tilesImage.SampleLevel(clampSampler, uv, 0.0);
        default:
            return neighboursImage.SampleLevel(clampSampler, uv, 0.0);
        }
    }

    float4 chainAt(uint image, int2 pixel)
    {
        switch (image)
        {
        case lensCircleOfConfusion:
            return circleOfConfusionImage.Load(int3(pixel, 0));
        case lensHalved:
            return halvedImage.Load(int3(pixel, 0));
        case lensBokeh:
            return bokehImage.Load(int3(pixel, 0));
        case lensFiltered:
            return filteredImage.Load(int3(pixel, 0));
        case lensFocused:
            return focusedImage.Load(int3(pixel, 0));
        case lensTiles:
            return tilesImage.Load(int3(pixel, 0));
        default:
            return neighboursImage.Load(int3(pixel, 0));
        }
    }

    float viewDepth(float2 uv)
    {
        const float depth = depthAt(uv);
        if (depth <= 0.0)
        {
            return 1.0e6;
        }
        const float4 world = mul(lens.inverseViewProjection, clipOf(uv, depth));
        return max(-mul(lens.view, float4(world.xyz / world.w, 1.0)).z, 1.0e-3);
    }

    float2 moved(float2 uv)
    {
        const float depth = depthAt(uv);
        if (depth > 0.0)
        {
            // Where the surface stood in the frame before, the camera having moved.
            const float4 world = mul(lens.inverseViewProjection, clipOf(uv, depth));
            return uv - uvOf(mul(lens.previousViewProjection, float4(world.xyz / world.w, 1.0)));
        }
        // The sky has no depth: it follows its direction through the two sky cameras.
        const float4 world = mul(lens.skyInverseViewProjection, clipOf(uv, 1.0));
        const float4 previous = mul(lens.previousSkyViewProjection, float4(world.xyz / world.w, 0.0));
        return previous.w > 1e-6 ? uv - uvOf(previous) : float2(0.0);
    }
};

[shader("fragment")]
float4 circleOfConfusionFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return circleOfConfusion(images, settings(), input.uv);
}

[shader("fragment")]
float4 prefilterFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return lensPrefilter(images, settings(), input.uv);
}

[shader("fragment")]
float4 bokehFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return lensBokehBlur(images, settings(), input.uv);
}

[shader("fragment")]
float4 filterFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return lensSmooth(images, settings(), input.uv);
}

[shader("fragment")]
float4 combineFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return lensCombine(images, settings(), input.position.xy, input.uv);
}

[shader("fragment")]
float4 tilesFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return lensTilesOf(images, settings(), input.position.xy);
}

[shader("fragment")]
float4 neighboursFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return lensNeighboursOf(images, settings(), input.position.xy);
}

[shader("fragment")]
float4 motionFragment(VertexOutput input) : SV_Target
{
    BoundLens images;
    return lensMotionBlur(images, settings(), input.position.xy);
}
