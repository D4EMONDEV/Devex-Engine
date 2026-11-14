// Draws the interface over the tonemapped scene: coloured rectangles, textured ones, panels with
// rounded corners, and the letters of a font, read from its atlas of distances or drawn at their
// size. Positions arrive in
// pixels of the image, and colours are blended over it with straight alpha.

import common;
import ui_shape;

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

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 sampled = uiSamples(ui.kind) ? sampleTexture(ui.texture, input.uv) : float4(1.0);
    float4 color = shadeUi(ui.kind, ui.rect, ui.radius, ui.sharpness, input.color, input.pixel, sampled);
    if (color.a <= 0.0)
    {
        discard;
    }
    if (ui.displaySpace != 0)
    {
        color.rgb = encodeDisplay(color.rgb);
    }
    return color;
}
