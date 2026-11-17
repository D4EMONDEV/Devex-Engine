// What a camera's lens and shutter do to the image, shared by Vulkan (lens.slang) and WebGPU (webgpu_lens.slang), generic
// over the images they read. The depth of field, after Unity's post-processing stack: the circle of confusion of each
// pixel from its distance and the lens, the image halved and weighted by it, blurred by a disk of samples that keeps
// the far field behind sharp edges and lets the near field spill over them, smoothed, then mixed back into the sharp
// image. The motion blur, after McGuire et al., "A Reconstruction Filter for Plausible Motion Blur" (2012): the fastest
// motion of each tile of the image and of its neighbours, along which each pixel gathers the pixels that cross it
// during the exposure, those in front over those behind.
module lens_filter;

// What every pass of the lens is told.
public struct LensSettings
{
    // The image this pass draws into, and the scene image, in pixels.
    public float targetWidth;
    public float targetHeight;
    public float imageWidth;
    public float imageHeight;
    // The diameter of the aperture and the focal length, in meters; the focus distance; how much depth of field there
    // is, from 0 to 1; and the largest radius of a blur, in pixels of the image.
    public float aperture;
    public float focalLength;
    public float focusDistance;
    public float amount;
    public float maxRadius;
    public uint autoFocus;
    // The share of a frame the shutter stays open, the size of a tile in pixels, whether the motion blur reads the
    // depth of field rather than the source, and the frame, which turns its noise.
    public float shutter;
    public float tileSize;
    public uint fromDepthOfField;
    public uint frame;
};

// What the lens reads: the image it works on, the images of its chain, and where each pixel stands and moves.
public interface ILensImages
{
    float4 source(float2 uv);
    float4 sourceAt(int2 pixel);
    float4 chain(uint image, float2 uv);
    float4 chainAt(uint image, int2 pixel);
    // How far in front of the camera a pixel stands, in meters along its axis; very far for the sky.
    float viewDepth(float2 uv);
    // How far a pixel moved since the frame before, in texture coordinates.
    float2 moved(float2 uv);
};

// The images of the chain.
public static const uint lensCircleOfConfusion = 0;
public static const uint lensHalved = 1;
public static const uint lensBokeh = 2;
public static const uint lensFiltered = 3;
public static const uint lensFocused = 4;
public static const uint lensTiles = 5;
public static const uint lensNeighbours = 6;
public static const uint lensChainSize = 7;

static const float lensPi = 3.14159265358979;
// The height of a full-frame sensor, which the focal length is measured against.
static const float sensorHeight = 0.024;

float noise(LensSettings lens, float2 pixel)
{
    return frac(sin(dot(pixel + float(lens.frame % 64u) * 7.31, float2(12.9898, 78.233))) * 43758.5453);
}

// The circle of confusion of every pixel: its radius in pixels of the image, negative before the focus and positive
// after it, from the thin lens equation on a full-frame sensor.
public float4 circleOfConfusion<I : ILensImages>(I images, LensSettings lens, float2 uv)
{
    float focus = lens.focusDistance;
    if (lens.autoFocus != 0)
    {
        // What the middle of the image shows, a few taps around it.
        focus = 0.0;
        focus += min(images.viewDepth(float2(0.5, 0.5)), 1000.0);
        focus += min(images.viewDepth(float2(0.48, 0.5)), 1000.0);
        focus += min(images.viewDepth(float2(0.52, 0.5)), 1000.0);
        focus += min(images.viewDepth(float2(0.5, 0.47)), 1000.0);
        focus += min(images.viewDepth(float2(0.5, 0.53)), 1000.0);
        focus *= 0.2;
    }
    focus = max(focus, lens.focalLength * 1.01);
    const float distance = images.viewDepth(uv);
    const float diameter = lens.aperture * lens.focalLength * (distance - focus) / (distance * (focus - lens.focalLength));
    const float radius = 0.5 * diameter / sensorHeight * lens.imageHeight * lens.amount;
    return float4(clamp(radius, -lens.maxRadius, lens.maxRadius), 0.0, 0.0, 1.0);
}

// The image halved, each pixel weighted by its blur so that a sharp bright point does not bleed, with the largest blur
// of the four it covers.
public float4 lensPrefilter<I : ILensImages>(I images, LensSettings lens, float2 uv)
{
    const float2 texel = 1.0 / float2(lens.imageWidth, lens.imageHeight);
    const float2 offsets[4] = { float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5), float2(0.5, 0.5) };
    float3 sum = float3(0.0);
    float weights = 0.0;
    float nearest = 0.0;
    float farthest = 0.0;
    for (uint index = 0; index < 4; ++index)
    {
        const float2 at = uv + offsets[index] * texel;
        const float3 color = images.source(at).rgb;
        const float radius = images.chain(lensCircleOfConfusion, at).r;
        const float weight = abs(radius) / (max(color.r, max(color.g, color.b)) + 1.0);
        sum += color * weight;
        weights += weight;
        nearest = min(nearest, radius);
        farthest = max(farthest, radius);
    }
    const float radius = -nearest > farthest ? nearest : farthest;
    // A focused pixel keeps nothing: it is the sharp image that shows there.
    const float3 color = sum / max(weights, 1e-5) * smoothstep(0.0, 2.0, abs(radius));
    return float4(color, radius);
}

// The blur by a disk of samples in rings, gathering what each sample's own blur lets reach the pixel: behind the
// pixel, no more than its own blur, so that the far field stays behind sharp edges; in front of it, as far as theirs,
// so that the near field spills over what is behind.
public float4 lensBokehBlur<I : ILensImages>(I images, LensSettings lens, float2 uv)
{
    const float4 middle = images.chain(lensHalved, uv);
    // Pixels of the image in a pixel of the halved image, as the margin of the comparisons.
    const float margin = 4.0;
    const float2 toUv = 1.0 / float2(lens.imageWidth, lens.imageHeight);
    float4 behind = float4(0.0);
    float4 inFront = float4(0.0);
    uint count = 0;
    for (uint ring = 0; ring <= 3; ++ring)
    {
        const uint samples = ring == 0 ? 1 : ring * 8;
        for (uint index = 0; index < samples; ++index)
        {
            const float angle = (float(index) + 0.5 * float(ring % 2)) / float(samples) * 2.0 * lensPi;
            const float2 offset = float2(cos(angle), sin(angle)) * (float(ring) / 3.0) * lens.maxRadius;
            const float distance = length(offset);
            const float4 tap = images.chain(lensHalved, uv + offset * toUv);
            const float behindRadius = max(min(middle.a, tap.a), 0.0);
            const float behindWeight = saturate((behindRadius - distance + margin) / margin);
            float inFrontWeight = saturate((-tap.a - distance + margin) / margin);
            // Focused samples were darkened by their weighting: they add nothing in front.
            inFrontWeight *= step(2.0, -tap.a);
            behind += float4(tap.rgb, 1.0) * behindWeight;
            inFront += float4(tap.rgb, 1.0) * inFrontWeight;
            ++count;
        }
    }
    behind.rgb /= behind.a + (behind.a == 0.0 ? 1.0 : 0.0);
    inFront.rgb /= inFront.a + (inFront.a == 0.0 ? 1.0 : 0.0);
    // The near field covers as much of the pixel as its samples did of the disk.
    const float cover = saturate(inFront.a * lensPi / float(count));
    return float4(lerp(behind.rgb, inFront.rgb, cover), cover);
}

// A tent of nine taps over the blur, which smooths the gaps between its samples.
public float4 lensSmooth<I : ILensImages>(I images, LensSettings lens, float2 uv)
{
    const float2 texel = 1.0 / float2(lens.targetWidth, lens.targetHeight);
    float4 sum = images.chain(lensBokeh, uv) * 4.0;
    sum += images.chain(lensBokeh, uv + float2(-texel.x, 0.0)) * 2.0;
    sum += images.chain(lensBokeh, uv + float2(texel.x, 0.0)) * 2.0;
    sum += images.chain(lensBokeh, uv + float2(0.0, -texel.y)) * 2.0;
    sum += images.chain(lensBokeh, uv + float2(0.0, texel.y)) * 2.0;
    sum += images.chain(lensBokeh, uv + float2(-texel.x, -texel.y));
    sum += images.chain(lensBokeh, uv + float2(texel.x, -texel.y));
    sum += images.chain(lensBokeh, uv + float2(-texel.x, texel.y));
    sum += images.chain(lensBokeh, uv + float2(texel.x, texel.y));
    return sum / 16.0;
}

// The sharp image where it is in focus, the blur where it is not: behind the focus by the pixel's own blur, in front
// of it by what the near field covers.
public float4 lensCombine<I : ILensImages>(I images, LensSettings lens, float2 position, float2 uv)
{
    const float4 color = images.sourceAt(int2(position));
    const float4 blurred = images.chain(lensFiltered, uv);
    const float radius = images.chainAt(lensCircleOfConfusion, int2(position)).r;
    const float far = smoothstep(1.0, 2.0, radius);
    return float4(lerp(color.rgb, blurred.rgb, far + blurred.a - far * blurred.a), 1.0);
}

// How far a pixel of the image moves while the shutter is open, in pixels, reaching no further than a tile on either
// side, which the neighbours of a tile cover.
public float2 lensMotion<I : ILensImages>(I images, LensSettings lens, float2 uv)
{
    const float2 pixels = images.moved(uv) * float2(lens.imageWidth, lens.imageHeight) * lens.shutter;
    const float span = sqrt(dot(pixels, pixels));
    return span > 2.0 * lens.tileSize ? pixels * (2.0 * lens.tileSize / span) : pixels;
}

// The fastest motion of the pixels of each tile.
public float4 lensTilesOf<I : ILensImages>(I images, LensSettings lens, float2 position)
{
    const uint size = uint(lens.tileSize);
    const uint2 first = uint2(position) * size;
    float2 fastest = float2(0.0);
    float fastestLength = 0.0;
    for (uint y = 0; y < size; ++y)
    {
        for (uint x = 0; x < size; ++x)
        {
            const float2 pixel = float2(first + uint2(x, y)) + 0.5;
            if (pixel.x >= lens.imageWidth || pixel.y >= lens.imageHeight)
            {
                continue;
            }
            const float2 moved = lensMotion(images, lens, pixel / float2(lens.imageWidth, lens.imageHeight));
            const float speed = dot(moved, moved);
            if (speed > fastestLength)
            {
                fastest = moved;
                fastestLength = speed;
            }
        }
    }
    return float4(fastest, 0.0, 1.0);
}

// The fastest motion of each tile and of the tiles around it, which may reach into it.
public float4 lensNeighboursOf<I : ILensImages>(I images, LensSettings lens, float2 position)
{
    const int2 tile = int2(position);
    const int2 count = int2(int(lens.targetWidth), int(lens.targetHeight));
    float2 fastest = float2(0.0);
    float fastestLength = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 neighbour = clamp(tile + int2(x, y), int2(0), count - 1);
            const float2 moved = images.chainAt(lensTiles, neighbour).rg;
            const float speed = dot(moved, moved);
            if (speed > fastestLength)
            {
                fastest = moved;
                fastestLength = speed;
            }
        }
    }
    return float4(fastest, 0.0, 1.0);
}

float3 lensInput<I : ILensImages>(I images, LensSettings lens, float2 uv)
{
    return lens.fromDepthOfField != 0 ? images.chain(lensFocused, uv).rgb : images.source(uv).rgb;
}

// Whether the first depth stands in front of the second, softly over a few centimeters.
float closer(float first, float second)
{
    return saturate(1.0 - (first - second) / 0.05);
}

// A pixel blurred by what crosses it during the exposure: samples along the fastest motion around it, each counted as
// far as its own motion, or the pixel's, reaches it, those in front over those behind, and both where they move alike.
public float4 lensMotionBlur<I : ILensImages>(I images, LensSettings lens, float2 position)
{
    const float2 size = float2(lens.imageWidth, lens.imageHeight);
    const float2 uv = position / size;
    const float3 center = lensInput(images, lens, uv);
    const int2 tile = int2(position / lens.tileSize);
    const float2 fastest = images.chainAt(lensNeighbours, tile).rg;
    const float reach = 0.5 * length(fastest);
    if (reach < 0.5)
    {
        return float4(center, 1.0);
    }
    const float2 own = lensMotion(images, lens, uv);
    const float ownReach = max(0.5 * length(own), 0.5);
    const float depth = images.viewDepth(uv);
    // Each pixel along a direction shifted at random, which the antialiasing of the next frames smooths.
    const float jitter = noise(lens, position) - 0.5;
    const uint samples = 12;
    float weight = 1.0 / ownReach;
    float3 sum = center * weight;
    for (uint index = 0; index < samples; ++index)
    {
        const float t = lerp(-1.0, 1.0, (float(index) + jitter + 1.0) / float(samples + 1));
        const float2 offset = fastest * 0.5 * t;
        const float distance = length(offset);
        const float2 sampleUv = uv + offset / size;
        const float sampleDepth = images.viewDepth(sampleUv);
        const float sampleReach = max(0.5 * length(lensMotion(images, lens, sampleUv)), 0.5);
        // In front, it blurs over the pixel as far as it moves; behind, the pixel's own motion shows it.
        const float inFront = closer(sampleDepth, depth);
        const float behind = closer(depth, sampleDepth);
        const float cone = saturate(1.0 - distance / sampleReach);
        const float ownCone = saturate(1.0 - distance / ownReach);
        const float cylinder = 1.0 - smoothstep(0.95 * sampleReach, 1.05 * sampleReach, distance);
        const float ownCylinder = 1.0 - smoothstep(0.95 * ownReach, 1.05 * ownReach, distance);
        const float alike = inFront * cone + behind * ownCone + cylinder * ownCylinder * 2.0;
        sum += lensInput(images, lens, sampleUv) * alike;
        weight += alike;
    }
    return float4(sum / weight, 1.0);
}
