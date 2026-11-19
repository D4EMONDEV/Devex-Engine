// Draws a texture into a level of a layer of an array for OpenGL, translated into GLSL, as webgpu_blit.slang does for
// WebGPU: the textures of the layers of a terrain, each level read from the mip of the source that fits it. A copy of
// framebuffers cannot read compressed textures, which a triangle over the whole level samples.

struct BlitData
{
    // The size of the image drawn into, in pixels.
    float targetWidth;
    float targetHeight;
    // How many levels the source has, which OpenGL ES cannot ask it.
    float sourceLevels;
    float padding0;
};

ConstantBuffer<BlitData> blit;
Sampler2D sourceImage;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput blitVertex(uint vertexIndex : SV_VulkanVertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 blitFragment(VertexOutput input) : SV_Target
{
    uint width;
    uint height;
    sourceImage.GetDimensions(width, height);
    // OpenGL counts the rows it draws into from the first stored, as the source keeps its own.
    const float2 target = float2(blit.targetWidth, blit.targetHeight);
    const float2 uv = input.position.xy / target;
    // The level whose texels are as large as the pixels drawn.
    const float level = clamp(log2(max(float(width) / target.x, float(height) / target.y)), 0.0, blit.sourceLevels - 1.0);
    return sourceImage.SampleLevel(uv, level);
}
