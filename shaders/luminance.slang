// Measures the exposed luminance of the scene color on a coarse grid. The CPU reads the grid back
// to adapt the exposure; every invocation writes its own element, so no synchronization is needed.

import common;

struct LuminanceData
{
    float* output;
    uint width;
    uint height;
};

[[vk::push_constant]]
ConstantBuffer<LuminanceData> grid;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= grid.width || id.y >= grid.height)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(grid.width, grid.height);
    const float3 color = sceneColor.SampleLevel(clampSampler, uv, 0.0).rgb;
    grid.output[id.y * grid.width + id.x] = max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.0);
}
