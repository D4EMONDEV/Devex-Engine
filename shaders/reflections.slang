// The image the surfaces of the next frame reflect: the scene at half its size, without what is too
// bright for a blur to spread (the disk of the sun above all). Its smaller levels come from it.

import common;

struct ReflectionPushConstants
{
    uint width;
    uint height;
};

[[vk::push_constant]]
ConstantBuffer<ReflectionPushConstants> constants;

[[vk::binding(0, 2)]]
RWTexture2D<float4> written;

// The brightest a pixel may be, as the tonemapping would take it.
static const float brightest = 32.0;

[shader("compute")]
[numthreads(8, 8, 1)]
void shrinkScene(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= constants.width || id.y >= constants.height)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(constants.width, constants.height);
    const float3 color = sceneColor.SampleLevel(clampSampler, uv, 0.0).rgb;
    const float peak = max(color.r, max(color.g, color.b));
    // What is not a number would spread through every level, and through the next reflections.
    written[id.xy] = peak > 0.0 && peak < 1e30 ? float4(color * min(brightest / peak, 1.0), 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}
