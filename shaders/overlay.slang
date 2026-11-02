// Draws the lines and triangles of the tools over the tonemapped scene, such as grids, light shapes
// and gizmos, and outlines the selected objects. Colors are linear, blended over the display image.

import common;

public struct OverlayVertex
{
    public float3 position;
    public float padding;
    public float4 color;
};

struct OverlayData
{
    SceneData* scene;
    OverlayVertex* vertices;
    // Color of the outlines.
    float4 outlineColor;
    // Multiplies clip space depth: above 1, lines lying on a surface stay in front of it.
    float depthScale;
    float lineWidth;
    float viewportWidth;
    float viewportHeight;
    float4 depthPlane;
    float2 jitter;
    float2 padding;
};

[[vk::push_constant]]
ConstantBuffer<OverlayData> overlay;

struct ColorVertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
ColorVertexOutput colorVertex(uint vertexIndex : SV_VertexID)
{
    const OverlayVertex vertex = overlay.vertices[vertexIndex];
    ColorVertexOutput output;
    // Tools draw after the temporal resolve: their lines must stay at the positions used by
    // editor hit testing, without the scene's per-frame subpixel jitter.
    output.position = mul(overlay.scene->unjitteredViewProjection, float4(vertex.position, 1.0));
    output.position.z *= overlay.depthScale;
    output.color = vertex.color;
    return output;
}

[shader("fragment")]
float4 colorFragment(ColorVertexOutput input) : SV_Target
{
    return input.color;
}

struct LineVertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    noperspective float side : TEXCOORD0;
};

bool clipToolLine(inout float4 from, inout float4 to, inout float4 fromColor, inout float4 toColor,
                 float distanceFrom, float distanceTo)
{
    if (distanceFrom < 0.0 && distanceTo < 0.0)
        return false;
    if (distanceFrom < 0.0 || distanceTo < 0.0)
    {
        const float t = distanceFrom / (distanceFrom - distanceTo);
        const float4 intersection = lerp(from, to, t);
        const float4 color = lerp(fromColor, toColor, t);
        if (distanceFrom < 0.0) { from = intersection; fromColor = color; }
        else { to = intersection; toColor = color; }
    }
    return true;
}

// Expand each pair into two triangles. Coverage and width are in pixels on every Vulkan device,
// without relying on wideLines or on temporal antialiasing of the scene underneath the tools.
[shader("vertex")]
LineVertexOutput lineVertex(uint vertexIndex : SV_VertexID)
{
    const uint segment = vertexIndex / 6;
    const OverlayVertex a = overlay.vertices[segment * 2];
    const OverlayVertex b = overlay.vertices[segment * 2 + 1];
    float4 from = mul(overlay.scene->unjitteredViewProjection, float4(a.position, 1.0));
    float4 to = mul(overlay.scene->unjitteredViewProjection, float4(b.position, 1.0));
    float4 fromColor = a.color;
    float4 toColor = b.color;
    // Clip against the reverse-Z near/far planes before dividing by w. Lines crossing the camera
    // must not turn into enormous quads, nor disappear while one endpoint is still in view.
    LineVertexOutput output;
    output.position = float4(2.0, 2.0, 2.0, 1.0);
    output.color = 0.0;
    output.side = 0.0;
    if (!clipToolLine(from, to, fromColor, toColor, from.w - from.z, to.w - to.z) ||
        !clipToolLine(from, to, fromColor, toColor, from.z, to.z))
        return output;
    if (from.w <= 1e-6 || to.w <= 1e-6)
        return output;
    const float2 size = float2(overlay.viewportWidth, overlay.viewportHeight);
    const float2 direction = (to.xy / to.w - from.xy / from.w) * size;
    const float lengthSquared = dot(direction, direction);
    if (lengthSquared < 1e-8)
        return output;
    const float2 normal = float2(-direction.y, direction.x) * rsqrt(lengthSquared);
    const uint corner = vertexIndex % 6;
    const bool atEnd = corner == 1 || corner == 4 || corner == 5;
    const float side = corner < 2 || corner == 4 ? -1.0 : 1.0;
    const float radius = overlay.lineWidth * 0.5 + 0.5;
    output.position = atEnd ? to : from;
    output.position.xy += normal * side * radius * 2.0 / size * output.position.w;
    output.color = atEnd ? toColor : fromColor;
    output.side = side * radius;

    if (dot(overlay.depthPlane.xyz, overlay.depthPlane.xyz) > 0.5)
    {
        // The colour is unjittered, but a coplanar floor wrote its depth with the scene's jitter.
        // Evaluate the plane at that same ray, including the width of the line. Applying a fixed
        // depth bias alone cannot cover subpixel depth differences close to the horizon.
        const float2 ndc = output.position.xy / output.position.w + overlay.jitter;
        // Transform the plane equation to clip space and solve for z. Unlike intersecting two
        // nearby world-space points, this remains precise for a camera almost parallel to it.
        const float4 plane = mul(transpose(overlay.scene->inverseViewProjection), overlay.depthPlane);
        if (abs(plane.z) > 1e-8)
        {
            const float depth = -dot(float3(plane.xy, plane.w), float3(ndc, 1.0)) / plane.z;
            // The stroke extends beyond the centreline. At the horizon a widened corner can
            // point behind the plane: keep its depth in range so jitter cannot clip the stroke.
            output.position.z = saturate(depth) * output.position.w;
        }
    }
    output.position.z = min(output.position.z * overlay.depthScale, output.position.w);
    return output;
}

[shader("fragment")]
float4 lineFragment(LineVertexOutput input) : SV_Target
{
    const float coverage = saturate(overlay.lineWidth * 0.5 + 0.5 - abs(input.side));
    return float4(input.color.rgb, input.color.a * coverage);
}

struct FullscreenOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
FullscreenOutput outlineVertex(uint vertexIndex : SV_VertexID)
{
    FullscreenOutput output;
    output.position = float4(fullscreenUv(vertexIndex) * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Pixels outside the selection mask within two pixels of it form the outline.
[shader("fragment")]
float4 outlineFragment(FullscreenOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    if (selectionMask.Load(int3(pixel, 0)).r > 0.5)
    {
        discard;
    }

    uint width;
    uint height;
    selectionMask.GetDimensions(width, height);
    const int2 last = int2(width, height) - 1;
    float coverage = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const int2 neighbor = clamp(pixel + int2(x, y), int2(0), last);
            coverage = max(coverage, selectionMask.Load(int3(neighbor, 0)).r);
        }
    }
    if (coverage < 0.5)
    {
        discard;
    }
    return overlay.outlineColor;
}
