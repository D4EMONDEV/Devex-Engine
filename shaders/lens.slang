// What a camera's lens and shutter do to the image, before the bloom and the tonemapping: the depth of field and the
// motion blur of lens_filter.slang, reading the chain of postChain, the velocity of the prepass and the scene.

import common;
import lens_filter;

struct LensData
{
    SceneData* scene;
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
};

[[vk::push_constant]]
ConstantBuffer<LensData> lens;

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

// Where a pixel of the scene image stands in the world.
float3 worldPosition(float2 uv, float depth)
{
    return worldFromSceneDepth(lens.scene, uv, depth);
}

struct VulkanLens : ILensImages
{
    float4 source(float2 uv)
    {
        return postSource.SampleLevel(clampSampler, uv, 0.0);
    }

    float4 sourceAt(int2 pixel)
    {
        return postSource.Load(int3(pixel, 0));
    }

    float4 chain(uint image, float2 uv)
    {
        return postChain[image].SampleLevel(clampSampler, uv, 0.0);
    }

    float4 chainAt(uint image, int2 pixel)
    {
        return postChain[image].Load(int3(pixel, 0));
    }

    float viewDepth(float2 uv)
    {
        const float depth = depthTexture.SampleLevel(clampSampler, uv, 0.0).r;
        if (depth <= 0.0)
        {
            return 1.0e6;
        }
        return max(-mul(lens.scene->view, float4(worldPosition(uv, depth), 1.0)).z, 1.0e-3);
    }

    // As the velocity of the prepass says, or for the sky, which it does not cover, as the camera turns.
    float2 moved(float2 uv)
    {
        const float depth = depthTexture.SampleLevel(clampSampler, uv, 0.0).r;
        if (depth > 0.0)
        {
            return velocityTexture.SampleLevel(clampSampler, uv, 0.0).rg;
        }
        const float3 far = lens.scene->cameraPosition + normalize(worldPosition(uv, 1.0e-6) - lens.scene->cameraPosition) * 1.0e4;
        const float4 before = mul(lens.scene->previousViewProjection, float4(far, 1.0));
        return uv - (before.xy / before.w * 0.5 + 0.5);
    }
};

[shader("fragment")]
float4 circleOfConfusionFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return circleOfConfusion(images, settings(), input.uv);
}

[shader("fragment")]
float4 prefilterFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return lensPrefilter(images, settings(), input.uv);
}

[shader("fragment")]
float4 bokehFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return lensBokehBlur(images, settings(), input.uv);
}

[shader("fragment")]
float4 filterFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return lensSmooth(images, settings(), input.uv);
}

[shader("fragment")]
float4 combineFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return lensCombine(images, settings(), input.position.xy, input.uv);
}

[shader("fragment")]
float4 tilesFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return lensTilesOf(images, settings(), input.position.xy);
}

[shader("fragment")]
float4 neighboursFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return lensNeighboursOf(images, settings(), input.position.xy);
}

[shader("fragment")]
float4 motionFragment(VertexOutput input) : SV_Target
{
    VulkanLens images;
    return lensMotionBlur(images, settings(), input.position.xy);
}
