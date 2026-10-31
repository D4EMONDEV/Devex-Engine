// Hierarchical depth, farthest per region (minimum with reverse-Z). The levels live in a buffer,
// each a ceil-halving of the previous one, so odd image edges are covered rather than dropped.
import common;

struct DepthReduceData
{
    float* pyramid;
    uint sourceOffset;
    uint targetOffset;
    uint sourceWidth;
    uint sourceHeight;
    uint firstLevel;
    uint padding;
};
[[vk::push_constant]]
ConstantBuffer<DepthReduceData> reduce;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    const uint2 sourceSize = uint2(reduce.sourceWidth, reduce.sourceHeight);
    const uint2 targetSize = (sourceSize + 1u) / 2u;
    if (any(id.xy >= targetSize)) return;
    float farthest = 1.0;
    for (uint y = 0; y < 2; ++y)
    for (uint x = 0; x < 2; ++x)
    {
        const uint2 at = min(id.xy * 2u + uint2(x, y), sourceSize - 1u);
        const float depth = reduce.firstLevel != 0 ? depthTexture.Load(int3(at, 0)).r
            : reduce.pyramid[reduce.sourceOffset + at.y * sourceSize.x + at.x];
        farthest = min(farthest, depth);
    }
    reduce.pyramid[reduce.targetOffset + id.y * targetSize.x + id.x] = farthest;
}
