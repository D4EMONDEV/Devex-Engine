// Measures the exposed luminance of the scene on a coarse grid for WebGPU, compiled to WGSL, as
// luminance.slang does for Vulkan. The CPU reads the grid back to adapt the exposure.

struct GridData
{
    uint width;
    uint height;
    uint padding0;
    uint padding1;
};

[[vk::binding(0, 0)]]
ConstantBuffer<GridData> grid;
[[vk::binding(1, 0)]]
Texture2D sceneTexture;
[[vk::binding(2, 0)]]
SamplerState clampSampler;
[[vk::binding(3, 0)]]
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(8, 8, 1)]
void luminanceMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= grid.width || id.y >= grid.height)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(float(grid.width), float(grid.height));
    const float3 color = sceneTexture.SampleLevel(clampSampler, uv, 0.0).rgb;
    output[id.y * grid.width + id.x] = max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.0);
}
