// What a camera's lens and shutter do to the image, before the bloom and the tonemapping. The depth of
// field, after Unity's post-processing stack: the circle of confusion of each pixel from its distance
// and the lens, the image halved and weighted by it, blurred by a disk of samples that keeps the far
// field behind sharp edges and lets the near field spill over them, smoothed, then mixed back into the
// sharp image. The motion blur, after McGuire et al., "A Reconstruction Filter for Plausible Motion
// Blur" (2012): the fastest motion of each tile of the image and of its neighbours, along which each
// pixel gathers the pixels that cross it during the exposure, those in front over those behind.

import common;

struct LensData
{
    SceneData* scene;
    // The image this pass draws into, and the scene image, in pixels.
    float targetWidth;
    float targetHeight;
    float imageWidth;
    float imageHeight;
    // The diameter of the aperture and the focal length, in meters; the focus distance; how much depth
    // of field there is, from 0 to 1; and the largest radius of a blur, in pixels of the image.
    float aperture;
    float focalLength;
    float focusDistance;
    float amount;
    float maxRadius;
    uint autoFocus;
    // The share of a frame the shutter stays open, the size of a tile in pixels, whether the motion
    // blur reads the depth of field rather than the source, and the frame, which turns its noise.
    float shutter;
    float tileSize;
    uint fromDepthOfField;
    uint frame;
};

[[vk::push_constant]]
ConstantBuffer<LensData> lens;

// The images of the chain, as postChain holds them.
static const uint circleOfConfusion = 0;
static const uint halved = 1;
static const uint bokeh = 2;
static const uint filtered = 3;
static const uint focused = 4;
static const uint tiles = 5;
static const uint neighbours = 6;

// The height of a full-frame sensor, which the focal length is measured against.
static const float sensorHeight = 0.024;

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

// Where a pixel of the scene image stands in the world.
float3 worldPosition(float2 uv, float depth)
{
    const float4 world = mul(lens.scene->inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return world.xyz / world.w;
}

// How far in front of the camera a pixel stands, in meters along its axis; very far for the sky.
float viewDepth(float2 uv)
{
    const float depth = depthTexture.SampleLevel(clampSampler, uv, 0.0).r;
    if (depth <= 0.0)
    {
        return 1.0e6;
    }
    return max(-mul(lens.scene->view, float4(worldPosition(uv, depth), 1.0)).z, 1.0e-3);
}

float noise(float2 pixel)
{
    return frac(sin(dot(pixel + float(lens.frame % 64u) * 7.31, float2(12.9898, 78.233))) * 43758.5453);
}

// The circle of confusion of every pixel: its radius in pixels of the image, negative before the
// focus and positive after it, from the thin lens equation on a full-frame sensor.
[shader("fragment")]
float4 circleOfConfusionFragment(VertexOutput input) : SV_Target
{
    float focus = lens.focusDistance;
    if (lens.autoFocus != 0)
    {
        // What the middle of the image shows, a few taps around it.
        focus = 0.0;
        focus += min(viewDepth(float2(0.5, 0.5)), 1000.0);
        focus += min(viewDepth(float2(0.48, 0.5)), 1000.0);
        focus += min(viewDepth(float2(0.52, 0.5)), 1000.0);
        focus += min(viewDepth(float2(0.5, 0.47)), 1000.0);
        focus += min(viewDepth(float2(0.5, 0.53)), 1000.0);
        focus *= 0.2;
    }
    focus = max(focus, lens.focalLength * 1.01);
    const float distance = viewDepth(input.uv);
    const float diameter = lens.aperture * lens.focalLength * (distance - focus) / (distance * (focus - lens.focalLength));
    const float radius = 0.5 * diameter / sensorHeight * lens.imageHeight * lens.amount;
    return float4(clamp(radius, -lens.maxRadius, lens.maxRadius), 0.0, 0.0, 1.0);
}

// The image halved, each pixel weighted by its blur so that a sharp bright point does not bleed, with
// the largest blur of the four it covers.
[shader("fragment")]
float4 prefilterFragment(VertexOutput input) : SV_Target
{
    const float2 texel = 1.0 / float2(lens.imageWidth, lens.imageHeight);
    const float2 offsets[4] = { float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5), float2(0.5, 0.5) };
    float3 sum = float3(0.0);
    float weights = 0.0;
    float nearest = 0.0;
    float farthest = 0.0;
    for (uint index = 0; index < 4; ++index)
    {
        const float2 uv = input.uv + offsets[index] * texel;
        const float3 color = postSource.SampleLevel(clampSampler, uv, 0.0).rgb;
        const float radius = postChain[circleOfConfusion].SampleLevel(clampSampler, uv, 0.0).r;
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

// The blur by a disk of samples in rings, gathering what each sample's own blur lets reach the pixel:
// behind the pixel, no more than its own blur, so that the far field stays behind sharp edges; in
// front of it, as far as theirs, so that the near field spills over what is behind.
[shader("fragment")]
float4 bokehFragment(VertexOutput input) : SV_Target
{
    const float4 middle = postChain[halved].SampleLevel(clampSampler, input.uv, 0.0);
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
            const float angle = (float(index) + 0.5 * float(ring % 2)) / float(samples) * 2.0 * pi;
            const float2 offset = float2(cos(angle), sin(angle)) * (float(ring) / 3.0) * lens.maxRadius;
            const float distance = length(offset);
            const float4 tap = postChain[halved].SampleLevel(clampSampler, input.uv + offset * toUv, 0.0);
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
    const float cover = saturate(inFront.a * pi / float(count));
    return float4(lerp(behind.rgb, inFront.rgb, cover), cover);
}

// A tent of nine taps over the blur, which smooths the gaps between its samples.
[shader("fragment")]
float4 filterFragment(VertexOutput input) : SV_Target
{
    const float2 texel = 1.0 / float2(lens.targetWidth, lens.targetHeight);
    float4 sum = postChain[bokeh].SampleLevel(clampSampler, input.uv, 0.0) * 4.0;
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(-texel.x, 0.0), 0.0) * 2.0;
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(texel.x, 0.0), 0.0) * 2.0;
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(0.0, -texel.y), 0.0) * 2.0;
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(0.0, texel.y), 0.0) * 2.0;
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(-texel.x, -texel.y), 0.0);
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(texel.x, -texel.y), 0.0);
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(-texel.x, texel.y), 0.0);
    sum += postChain[bokeh].SampleLevel(clampSampler, input.uv + float2(texel.x, texel.y), 0.0);
    return sum / 16.0;
}

// The sharp image where it is in focus, the blur where it is not: behind the focus by the pixel's own
// blur, in front of it by what the near field covers.
[shader("fragment")]
float4 combineFragment(VertexOutput input) : SV_Target
{
    const float4 color = postSource.Load(int3(int2(input.position.xy), 0));
    const float4 blurred = postChain[filtered].SampleLevel(clampSampler, input.uv, 0.0);
    const float radius = postChain[circleOfConfusion].Load(int3(int2(input.position.xy), 0)).r;
    const float far = smoothstep(1.0, 2.0, radius);
    return float4(lerp(color.rgb, blurred.rgb, far + blurred.a - far * blurred.a), 1.0);
}

// How far a pixel of the image moves while the shutter is open, in pixels: as the velocity of the
// prepass says, or for the sky, which it does not cover, as the camera turns.
float2 motion(float2 uv)
{
    const float depth = depthTexture.SampleLevel(clampSampler, uv, 0.0).r;
    float2 moved;
    if (depth > 0.0)
    {
        moved = velocityTexture.SampleLevel(clampSampler, uv, 0.0).rg;
    }
    else
    {
        const float3 far = lens.scene->cameraPosition + normalize(worldPosition(uv, 1.0e-6) - lens.scene->cameraPosition) * 1.0e4;
        const float4 before = mul(lens.scene->previousViewProjection, float4(far, 1.0));
        moved = uv - (before.xy / before.w * 0.5 + 0.5);
    }
    const float2 pixels = moved * float2(lens.imageWidth, lens.imageHeight) * lens.shutter;
    // Reaching no further than a tile on either side, which the neighbours of a tile cover.
    const float span = sqrt(dot(pixels, pixels));
    return span > 2.0 * lens.tileSize ? pixels * (2.0 * lens.tileSize / span) : pixels;
}

// The fastest motion of the pixels of each tile.
[shader("fragment")]
float4 tilesFragment(VertexOutput input) : SV_Target
{
    const uint size = uint(lens.tileSize);
    const uint2 first = uint2(input.position.xy) * size;
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
            const float2 moved = motion(pixel / float2(lens.imageWidth, lens.imageHeight));
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
[shader("fragment")]
float4 neighboursFragment(VertexOutput input) : SV_Target
{
    const int2 tile = int2(input.position.xy);
    const int2 count = int2(int(lens.targetWidth), int(lens.targetHeight));
    float2 fastest = float2(0.0);
    float fastestLength = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 neighbour = clamp(tile + int2(x, y), int2(0), count - 1);
            const float2 moved = postChain[tiles].Load(int3(neighbour, 0)).rg;
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

float3 lensInput(float2 uv)
{
    return lens.fromDepthOfField != 0 ? postChain[focused].SampleLevel(clampSampler, uv, 0.0).rgb
                                      : postSource.SampleLevel(clampSampler, uv, 0.0).rgb;
}

// Whether the first depth stands in front of the second, softly over a few centimeters.
float closer(float first, float second)
{
    return saturate(1.0 - (first - second) / 0.05);
}

// A pixel blurred by what crosses it during the exposure: samples along the fastest motion around
// it, each counted as far as its own motion, or the pixel's, reaches it, those in front over those
// behind, and both where they move alike.
[shader("fragment")]
float4 motionFragment(VertexOutput input) : SV_Target
{
    const float2 size = float2(lens.imageWidth, lens.imageHeight);
    const float2 uv = input.position.xy / size;
    const float3 center = lensInput(uv);
    const int2 tile = int2(input.position.xy / lens.tileSize);
    const float2 fastest = postChain[neighbours].Load(int3(tile, 0)).rg;
    const float reach = 0.5 * length(fastest);
    if (reach < 0.5)
    {
        return float4(center, 1.0);
    }
    const float2 own = motion(uv);
    const float ownReach = max(0.5 * length(own), 0.5);
    const float depth = viewDepth(uv);
    // Each pixel along a direction shifted at random, which the antialiasing of the next frames smooths.
    const float jitter = noise(input.position.xy) - 0.5;
    const uint samples = 12;
    float weight = 1.0 / ownReach;
    float3 sum = center * weight;
    for (uint index = 0; index < samples; ++index)
    {
        const float t = lerp(-1.0, 1.0, (float(index) + jitter + 1.0) / float(samples + 1));
        const float2 offset = fastest * 0.5 * t;
        const float distance = length(offset);
        const float2 sampleUv = uv + offset / size;
        const float sampleDepth = viewDepth(sampleUv);
        const float sampleReach = max(0.5 * length(motion(sampleUv)), 0.5);
        // In front, it blurs over the pixel as far as it moves; behind, the pixel's own motion shows it.
        const float inFront = closer(sampleDepth, depth);
        const float behind = closer(depth, sampleDepth);
        const float cone = saturate(1.0 - distance / sampleReach);
        const float ownCone = saturate(1.0 - distance / ownReach);
        const float cylinder = 1.0 - smoothstep(0.95 * sampleReach, 1.05 * sampleReach, distance);
        const float ownCylinder = 1.0 - smoothstep(0.95 * ownReach, 1.05 * ownReach, distance);
        const float alike = inFront * cone + behind * ownCone + cylinder * ownCylinder * 2.0;
        sum += lensInput(sampleUv) * alike;
        weight += alike;
    }
    return float4(sum / weight, 1.0);
}
