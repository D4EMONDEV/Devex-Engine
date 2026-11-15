// The filters of the bloom, shared by Vulkan (bloom.slang) and WebGPU (webgpu_bloom.slang), generic over the image
// they read: the threshold that halves the image and keeps what glows, the thirteen taps that halve a level, and the
// tent that adds a level over the one above.
module bloom_filter;

// An image a step of the bloom reads, between its texels.
public interface IBloomSource
{
    float3 read(float2 uv);
};

// The first step reads the image itself and keeps what is brighter than the threshold, easing into
// it so that a surface crossing the threshold does not pop.
public float3 bloomThreshold<S : IBloomSource>(S source, float2 uv, float2 texel, float threshold)
{
    float3 color = float3(0.0);
    // Four taps halve the image at the same time, which costs nothing here.
    color += source.read(uv + float2(-0.5, -0.5) * texel);
    color += source.read(uv + float2(0.5, -0.5) * texel);
    color += source.read(uv + float2(-0.5, 0.5) * texel);
    color += source.read(uv + float2(0.5, 0.5) * texel);
    color *= 0.25;

    const float brightness = max(color.r, max(color.g, color.b));
    const float knee = max(threshold * 0.5, 1e-4);
    // Below the threshold nothing glows; just above it, the glow comes in gently.
    const float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    const float weight = max(max(brightness - threshold, soft * soft / (4.0 * knee)), 0.0) /
                         max(brightness, 1e-4);
    return color * weight;
}

// Thirteen taps arranged in a square and a diamond, which halve an image without the shimmer a
// plain box filter leaves behind.
public float3 bloomDownsample<S : IBloomSource>(S source, float2 uv, float2 texel)
{
    const float3 middle = source.read(uv);
    const float3 corners = source.read(uv + float2(-2.0, -2.0) * texel) +
                           source.read(uv + float2(2.0, -2.0) * texel) +
                           source.read(uv + float2(-2.0, 2.0) * texel) +
                           source.read(uv + float2(2.0, 2.0) * texel);
    const float3 sides = source.read(uv + float2(-2.0, 0.0) * texel) +
                         source.read(uv + float2(2.0, 0.0) * texel) +
                         source.read(uv + float2(0.0, -2.0) * texel) +
                         source.read(uv + float2(0.0, 2.0) * texel);
    const float3 inner = source.read(uv + float2(-1.0, -1.0) * texel) +
                         source.read(uv + float2(1.0, -1.0) * texel) +
                         source.read(uv + float2(-1.0, 1.0) * texel) +
                         source.read(uv + float2(1.0, 1.0) * texel);
    return middle * 0.125 + corners * 0.03125 + sides * 0.0625 + inner * 0.125;
}

// A tent of nine taps, added over the level above.
public float3 bloomUpsample<S : IBloomSource>(S source, float2 uv, float2 texel, float strength)
{
    float3 color = source.read(uv) * 4.0;
    color += (source.read(uv + float2(-1.0, 0.0) * texel) + source.read(uv + float2(1.0, 0.0) * texel) +
              source.read(uv + float2(0.0, -1.0) * texel) + source.read(uv + float2(0.0, 1.0) * texel)) *
             2.0;
    color += source.read(uv + float2(-1.0, -1.0) * texel) + source.read(uv + float2(1.0, -1.0) * texel) +
             source.read(uv + float2(-1.0, 1.0) * texel) + source.read(uv + float2(1.0, 1.0) * texel);
    return color / 16.0 * strength;
}
