// The weighing of the colours the temporal antialiasing mixes, shared by Vulkan (taa.slang) and WebGPU
// (webgpu_taa.slang).
module temporal;

// Bright pixels weigh less, so that a single spark cannot drag the whole neighborhood with it.
public float3 weigh(float3 color)
{
    return color / (1.0 + max(color.r, max(color.g, color.b)));
}

public float3 unweigh(float3 color)
{
    return color / max(1.0 - max(color.r, max(color.g, color.b)), 1e-4);
}
