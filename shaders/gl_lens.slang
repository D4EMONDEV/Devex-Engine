// What a camera's lens and shutter do to the image for OpenGL, translated to GLSL ES 3.00 and GLSL 3.30: the depth of
// field and the motion blur of lens_filter.slang, as webgpu_lens.slang draws them for WebGPU. A pixel follows the camera,
// from the depth of the scene through the matrices of the frame and of the one before, as the temporal antialiasing
// does, and the sky its direction through the two sky cameras. The matrices are those OpenGL draws with, whose depth runs
// from -1 to 1; texture coordinates and pixels both start from the bottom row.

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

ConstantBuffer<LensData> lens;
Sampler2D sourceImage;
Sampler2D sceneDepth;
Sampler2D circleOfConfusionImage;
Sampler2D halvedImage;
Sampler2D bokehImage;
Sampler2D filteredImage;
Sampler2D focusedImage;
Sampler2D tilesImage;
Sampler2D neighboursImage;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput lensVertex(uint vertexIndex : SV_VulkanVertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.uv = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
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

struct BoundLens : ILensImages
{
    float4 source(float2 uv)
    {
        return sourceImage.SampleLevel(uv, 0.0);
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
            return circleOfConfusionImage.SampleLevel(uv, 0.0);
        case lensHalved:
            return halvedImage.SampleLevel(uv, 0.0);
        case lensBokeh:
            return bokehImage.SampleLevel(uv, 0.0);
        case lensFiltered:
            return filteredImage.SampleLevel(uv, 0.0);
        case lensFocused:
            return focusedImage.SampleLevel(uv, 0.0);
        case lensTiles:
            return tilesImage.SampleLevel(uv, 0.0);
        default:
            return neighboursImage.SampleLevel(uv, 0.0);
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
