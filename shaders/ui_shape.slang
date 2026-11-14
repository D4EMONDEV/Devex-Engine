// The shapes of the interface, shared by the interface of Vulkan (ui.slang) and of WebGPU
// (webgpu_ui.slang): plain arithmetic, without resources.
module ui_shape;

public struct UiVertex
{
    public float2 position;
    public float2 uv;
    public float4 color;
};

public static const uint kindQuad = 0;
public static const uint kindRoundedQuad = 1;
public static const uint kindText = 2;
public static const uint kindGlyph = 3;
public static const uint kindShadow = 4;
public static const uint kindOutline = 5;
public static const uint kindCheck = 6;

float encodeChannel(float value)
{
    const float clamped = saturate(value);
    return clamped <= 0.0031308 ? clamped * 12.92 : 1.055 * pow(clamped, 1.0 / 2.4) - 0.055;
}

// How far a point is from a segment.
public float segmentDistance(float2 point, float2 from, float2 to)
{
    const float2 along = to - from;
    const float t = saturate(dot(point - from, along) / max(dot(along, along), 1e-8));
    return length(point - from - along * t);
}

// How far a point is from the outline of a rectangle with rounded corners: negative inside.
public float roundedBoxDistance(float2 point, float2 halfSize, float radius)
{
    const float2 corner = abs(point) - halfSize + radius;
    return length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - radius;
}

// Colours encoded as a display shows them, for an image that keeps them so.
public float3 encodeDisplay(float3 color)
{
    return float3(encodeChannel(color.r), encodeChannel(color.g), encodeChannel(color.b));
}

// Whether a draw of this kind reads its texture.
public bool uiSamples(uint kind)
{
    return kind != kindCheck && kind != kindShadow && kind != kindOutline;
}

// The colour of a pixel of a draw, straight alpha, before an empty one is discarded: of `kind`, over
// `rect` in pixels with corners of `radius`, at `pixel` in the image, from the colour of its vertices
// and `sampled`, what its texture holds under it (white when it reads none).
public float4 shadeUi(uint kind, float4 rect, float radius, float sharpness, float4 color, float2 pixel, float4 sampled)
{
    if (kind == kindText)
    {
        // The atlas encodes one spread of distance in half its range. One screen pixel covers
        // 0.5 / sharpness, so the half-width of a one-pixel transition is 0.25 / sharpness.
        // Using the full width on each side softened small letters over two pixels.
        const float distance = sampled.r;
        const float softness = 0.25 / max(sharpness, 0.0001);
        color.a *= smoothstep(0.5 - softness, 0.5 + softness, distance);
    }
    else if (kind == kindGlyph)
    {
        // How much of the pixel the letter covers, drawn by FreeType at this very size.
        color.a *= sampled.r;
    }
    else if (kind == kindCheck)
    {
        // A tick in the square of the rectangle: a short stroke down, a long one up, with round
        // ends, drawn along as far as the radius says.
        const float2 size = rect.zw - rect.xy;
        const float side = max(min(size.x, size.y), 1e-4);
        const float2 corner = (rect.xy + rect.zw - side) * 0.5;
        const float2 point = (pixel - corner) / side;
        const float2 start = float2(0.12, 0.52);
        const float2 bend = float2(0.39, 0.79);
        const float2 end = float2(0.9, 0.24);
        const float first = length(bend - start);
        const float second = length(end - bend);
        const float drawn = saturate(radius) * (first + second);
        float distance = 1e4;
        if (drawn > 0.0)
        {
            distance = segmentDistance(point, start, start + (bend - start) * saturate(drawn / first));
            if (drawn > first)
            {
                distance = min(distance, segmentDistance(point, bend, bend + (end - bend) * saturate((drawn - first) / second)));
            }
        }
        color.a *= 1.0 - smoothstep(-0.5, 0.5, distance * side - sharpness * 0.5);
    }
    else if (kind == kindShadow || kind == kindOutline)
    {
        const float2 middle = (rect.xy + rect.zw) * 0.5;
        const float2 halfSize = (rect.zw - rect.xy) * 0.5;
        const float radius = min(radius, min(halfSize.x, halfSize.y));
        const float distance = roundedBoxDistance(pixel - middle, halfSize, radius);
        if (kind == kindShadow)
        {
            // Fading from inside the outline to as far outside it.
            color.a *= 1.0 - smoothstep(-sharpness, sharpness, distance);
        }
        else
        {
            // Inside the outline, and not yet as far inside as the border is wide.
            color.a *= (1.0 - smoothstep(-0.5, 0.5, distance)) * smoothstep(-0.5, 0.5, distance + sharpness);
        }
    }
    else
    {
        color *= sampled;
        if (kind == kindRoundedQuad)
        {
            const float2 middle = (rect.xy + rect.zw) * 0.5;
            const float2 halfSize = (rect.zw - rect.xy) * 0.5;
            const float radius = min(radius, min(halfSize.x, halfSize.y));
            const float distance = roundedBoxDistance(pixel - middle, halfSize, radius);
            // One pixel of softening around the outline.
            color.a *= 1.0 - smoothstep(-0.5, 0.5, distance);
        }
    }

    return color;
}
