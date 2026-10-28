// Draws the interface over the tonemapped scene: coloured rectangles, textured ones, panels with
// rounded corners, and the letters of a font, read from its atlas of distances or drawn at their
// size. Positions arrive in
// pixels of the image, and colours are blended over it with straight alpha.

import common;

public struct UiVertex
{
    public float2 position;
    public float2 uv;
    public float4 color;
};

static const uint kindQuad = 0;
static const uint kindRoundedQuad = 1;
static const uint kindText = 2;
static const uint kindGlyph = 3;
static const uint kindShadow = 4;
static const uint kindOutline = 5;
static const uint kindCheck = 6;

struct UiData
{
    UiVertex* vertices;
    // The vertex each triangle corner reads, since a draw covers a slice of one shared list.
    uint* indices;
    // Turns pixels into clip space: 2 / width and 2 / height.
    float2 inverseViewport;
    uint kind;
    // Slot of the texture in the bindless array; the white texture leaves the colour alone.
    uint texture;
    // The rectangle whose corners are rounded, in pixels.
    float4 rect;
    float radius;
    // How much of the stored distances one pixel of the screen covers, for the letters.
    float sharpness;
    // 1 when the image stores the colours of the display rather than light, as the tools do: the
    // colours are encoded before they are blended, in the space their values were chosen in, so
    // that light letters on a dark panel keep the weight they were designed with.
    uint displaySpace;
};

[[vk::push_constant]]
ConstantBuffer<UiData> ui;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    // Where the pixel is in the image, which the rounded corners measure from.
    float2 pixel : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    const UiVertex vertex = ui.vertices[ui.indices[vertexIndex]];
    VertexOutput output;
    output.position = float4(vertex.position * ui.inverseViewport - 1.0, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = vertex.color;
    output.pixel = vertex.position;
    return output;
}

float encodeChannel(float value)
{
    const float clamped = saturate(value);
    return clamped <= 0.0031308 ? clamped * 12.92 : 1.055 * pow(clamped, 1.0 / 2.4) - 0.055;
}

// How far a point is from a segment.
float segmentDistance(float2 point, float2 from, float2 to)
{
    const float2 along = to - from;
    const float t = saturate(dot(point - from, along) / max(dot(along, along), 1e-8));
    return length(point - from - along * t);
}

// How far a point is from the outline of a rectangle with rounded corners: negative inside.
float roundedBoxDistance(float2 point, float2 halfSize, float radius)
{
    const float2 corner = abs(point) - halfSize + radius;
    return length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - radius;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 color = input.color;
    if (ui.kind == kindText)
    {
        // The atlas encodes one spread of distance in half its range. One screen pixel covers
        // 0.5 / sharpness, so the half-width of a one-pixel transition is 0.25 / sharpness.
        // Using the full width on each side softened small letters over two pixels.
        const float distance = sampleTexture(ui.texture, input.uv).r;
        const float softness = 0.25 / max(ui.sharpness, 0.0001);
        color.a *= smoothstep(0.5 - softness, 0.5 + softness, distance);
    }
    else if (ui.kind == kindGlyph)
    {
        // How much of the pixel the letter covers, drawn by FreeType at this very size.
        color.a *= sampleTexture(ui.texture, input.uv).r;
    }
    else if (ui.kind == kindCheck)
    {
        // A tick in the square of the rectangle: a short stroke down, a long one up, with round
        // ends, drawn along as far as the radius says.
        const float2 size = ui.rect.zw - ui.rect.xy;
        const float side = max(min(size.x, size.y), 1e-4);
        const float2 corner = (ui.rect.xy + ui.rect.zw - side) * 0.5;
        const float2 point = (input.pixel - corner) / side;
        const float2 start = float2(0.12, 0.52);
        const float2 bend = float2(0.39, 0.79);
        const float2 end = float2(0.9, 0.24);
        const float first = length(bend - start);
        const float second = length(end - bend);
        const float drawn = saturate(ui.radius) * (first + second);
        float distance = 1e4;
        if (drawn > 0.0)
        {
            distance = segmentDistance(point, start, start + (bend - start) * saturate(drawn / first));
            if (drawn > first)
            {
                distance = min(distance, segmentDistance(point, bend, bend + (end - bend) * saturate((drawn - first) / second)));
            }
        }
        color.a *= 1.0 - smoothstep(-0.5, 0.5, distance * side - ui.sharpness * 0.5);
    }
    else if (ui.kind == kindShadow || ui.kind == kindOutline)
    {
        const float2 middle = (ui.rect.xy + ui.rect.zw) * 0.5;
        const float2 halfSize = (ui.rect.zw - ui.rect.xy) * 0.5;
        const float radius = min(ui.radius, min(halfSize.x, halfSize.y));
        const float distance = roundedBoxDistance(input.pixel - middle, halfSize, radius);
        if (ui.kind == kindShadow)
        {
            // Fading from inside the outline to as far outside it.
            color.a *= 1.0 - smoothstep(-ui.sharpness, ui.sharpness, distance);
        }
        else
        {
            // Inside the outline, and not yet as far inside as the border is wide.
            color.a *= (1.0 - smoothstep(-0.5, 0.5, distance)) * smoothstep(-0.5, 0.5, distance + ui.sharpness);
        }
    }
    else
    {
        color *= sampleTexture(ui.texture, input.uv);
        if (ui.kind == kindRoundedQuad)
        {
            const float2 middle = (ui.rect.xy + ui.rect.zw) * 0.5;
            const float2 halfSize = (ui.rect.zw - ui.rect.xy) * 0.5;
            const float radius = min(ui.radius, min(halfSize.x, halfSize.y));
            const float distance = roundedBoxDistance(input.pixel - middle, halfSize, radius);
            // One pixel of softening around the outline.
            color.a *= 1.0 - smoothstep(-0.5, 0.5, distance);
        }
    }

    if (color.a <= 0.0)
    {
        discard;
    }
    if (ui.displaySpace != 0)
    {
        color.rgb = float3(encodeChannel(color.r), encodeChannel(color.g), encodeChannel(color.b));
    }
    return color;
}
