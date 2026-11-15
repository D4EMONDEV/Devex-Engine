// The 3D of the WebGPU renderer (web step 3a): meshes, rigid or skinned, drawn by batches of one material and lit by
// the sun, the local lights and the environment, over the sky. The CPU culls the instances and groups them, as the
// Compatibility renderer of Godot; the environment is baked by ibl.slang as Vulkan bakes it.
#include "WebGpuHelpers.hpp"
#include "WebGpuRenderer.hpp"

#include "../Lighting.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/render/ShadowAtlas.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>

namespace devex::render::webgpu {
namespace {

constexpr wgpu::TextureFormat sceneFormat = wgpu::TextureFormat::RGBA16Float;
constexpr wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Depth32Float;
constexpr wgpu::TextureFormat environmentFormat = wgpu::TextureFormat::RGBA16Float;
constexpr std::uint64_t uniformStride = 256;
// The sizes and samples of the environment, those of vulkan::EnvironmentBaker.
constexpr std::uint32_t radianceSize = 512;
constexpr std::uint32_t specularSize = 256;
constexpr std::uint32_t specularMipCount = 6;
constexpr std::uint32_t irradianceSize = 32;
constexpr std::uint32_t brdfTableSize = 128;
constexpr std::uint32_t prefilterSamples = 256;
constexpr std::uint32_t irradianceSamples = 512;
constexpr std::uint32_t brdfSamples = 1024;
// The shadows of vulkan::VulkanRenderer: the cascades of the sun, and the atlas of the local lights.
constexpr std::uint32_t shadowMapSize = 2048;
constexpr std::uint32_t shadowAtlasSize = 4096;
constexpr std::uint32_t shadowAtlasCell = 512;
constexpr std::size_t largeShadowLights = 2;

// What the shaders read, laid out as their WGSL does.
struct SceneData
{
    math::Mat4 viewProjection{1.0f};
    math::Mat4 view{1.0f};
    math::Vec3 cameraPosition{0.0f};
    float exposure = 1.0f;
    math::Vec3 sunDirection{0.0f, -1.0f, 0.0f};
    std::uint32_t sunEnabled = 0;
    math::Vec3 sunIlluminance{0.0f};
    std::uint32_t lightCount = 0;
    math::Vec3 sky{1.0f};
    float environmentRotation = 0.0f;
    float specularMipCount = 1.0f;
    float padding[3]{};
    std::array<math::Mat4, 4> cascadeViewProjections{};
    math::Vec4 cascadeSplits{0.0f};
    math::Vec4 cascadeTexelSizes{0.0f};
    float shadowDistance = 0.0f;
    std::uint32_t sunCastsShadows = 0;
    float padding1[2]{};
};
struct InstanceData
{
    math::Mat4 world{1.0f};
    std::uint32_t firstBone = 0;
    std::uint32_t boneCount = 0;
    std::uint32_t padding[2]{};
};
struct MaterialData
{
    math::Vec4 baseColorFactor{1.0f};
    math::Vec3 emissiveFactor{0.0f};
    float alphaCutoff = 0.5f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    std::uint32_t alphaMode = 0;
    std::uint32_t doubleSided = 0;
    std::uint32_t padding[2]{};
};
struct DrawData
{
    std::uint32_t first = 0;
    std::uint32_t padding[3]{};
};
struct SkyData
{
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Vec3 sky{1.0f};
    float exposure = 1.0f;
    float environmentRotation = 0.0f;
    float padding[3]{};
};
struct BakeParameters
{
    std::uint32_t size = 0;
    float roughness = 0.0f;
    std::uint32_t sampleCount = 0;
    float sourceLod = 0.0f;
    float sourceSize = 0.0f;
    // A uniform buffer of WGSL rounds its structures to 16 bytes.
    float padding[3]{};
};
static_assert(sizeof(SceneData) == 512 && sizeof(InstanceData) == 80 && sizeof(MaterialData) == 64 &&
              sizeof(SkyData) == 96 && sizeof(BakeParameters) == 32);

constexpr std::uint32_t skinnedBit = 1;
constexpr std::uint32_t atlasBit = 2;
constexpr std::uint32_t bothSidesBit = 2;
constexpr std::uint32_t blendedBit = 4;

[[nodiscard]] wgpu::BindGroupLayoutEntry storageEntry(std::uint32_t binding, wgpu::ShaderStage visibility)
{
    wgpu::BindGroupLayoutEntry entry;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    return entry;
}

[[nodiscard]] wgpu::BindGroupLayoutEntry cubeEntry(std::uint32_t binding, wgpu::ShaderStage visibility)
{
    wgpu::BindGroupLayoutEntry entry = textureEntry(binding, visibility);
    entry.texture.viewDimension = wgpu::TextureViewDimension::Cube;
    return entry;
}

// Vulkan's clip space points Y down: the shadows are read with its matrices, whose texture coordinates go down too, and
// drawn with them turned to WebGPU's, which points Y up.
[[nodiscard]] math::Mat4 flipY() noexcept
{
    math::Mat4 flip{1.0f};
    flip[1][1] = -1.0f;
    return flip;
}

[[nodiscard]] wgpu::TextureView cubeView(const wgpu::Texture& texture)
{
    wgpu::TextureViewDescriptor descriptor;
    descriptor.dimension = wgpu::TextureViewDimension::Cube;
    return texture.CreateView(&descriptor);
}

// One level of a cube, its six faces, as a compute shader writes it.
[[nodiscard]] wgpu::TextureView facesView(const wgpu::Texture& texture, std::uint32_t mip)
{
    wgpu::TextureViewDescriptor descriptor;
    descriptor.dimension = wgpu::TextureViewDimension::e2DArray;
    descriptor.baseMipLevel = mip;
    descriptor.mipLevelCount = 1;
    descriptor.arrayLayerCount = 6;
    return texture.CreateView(&descriptor);
}

[[nodiscard]] wgpu::Buffer uniformBuffer(const wgpu::Device& device, const wgpu::Queue& queue, const void* data, std::size_t size,
                                         const char* label)
{
    wgpu::BufferDescriptor descriptor;
    descriptor.label = label;
    descriptor.size = size;
    descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);
    queue.WriteBuffer(buffer, 0, data, size);
    return buffer;
}

} // namespace

core::Result<void> WebGpuRenderer::createScenePipelines()
{
    const wgpu::ShaderStage both = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    const wgpu::ShaderStage fragment = wgpu::ShaderStage::Fragment;
    wgpu::BindGroupLayoutEntry cascades = textureEntry(9, fragment);
    cascades.texture.sampleType = wgpu::TextureSampleType::Depth;
    cascades.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    wgpu::BindGroupLayoutEntry atlas = textureEntry(10, fragment);
    atlas.texture.sampleType = wgpu::TextureSampleType::Depth;
    wgpu::BindGroupLayoutEntry comparison = samplerEntry(11, fragment);
    comparison.sampler.type = wgpu::SamplerBindingType::Comparison;
    const std::array sceneEntries{uniformEntry(0, both, false, sizeof(SceneData)), storageEntry(1, fragment),
                                  storageEntry(2, wgpu::ShaderStage::Vertex), storageEntry(3, wgpu::ShaderStage::Vertex),
                                  cubeEntry(4, fragment), cubeEntry(5, fragment), textureEntry(6, fragment), samplerEntry(7, fragment),
                                  storageEntry(8, fragment), cascades, atlas, comparison};
    m_sceneLayout = bindGroupLayout(m_device, "Scene", sceneEntries);
    std::array<wgpu::BindGroupLayoutEntry, 11> materialEntries{};
    materialEntries[0] = uniformEntry(0, fragment, false, sizeof(MaterialData));
    for (std::uint32_t texture = 0; texture < 5; ++texture)
    {
        materialEntries[1 + texture] = textureEntry(1 + texture, fragment);
        materialEntries[6 + texture] = samplerEntry(6 + texture, fragment);
    }
    m_materialLayout = bindGroupLayout(m_device, "Material", materialEntries);
    const std::array drawEntries{uniformEntry(0, wgpu::ShaderStage::Vertex, true, sizeof(DrawData))};
    m_meshDrawLayout = bindGroupLayout(m_device, "Mesh draw", drawEntries);
    const std::array skyEntries{uniformEntry(0, fragment, false, sizeof(SkyData)), textureEntry(1, fragment), samplerEntry(2, fragment)};
    m_skyLayout = bindGroupLayout(m_device, "Sky", skyEntries);

    const wgpu::ShaderModule mesh = shaderModule("webgpu_mesh");
    const std::array meshGroups{m_sceneLayout, m_materialLayout, m_meshDrawLayout};
    const wgpu::PipelineLayout meshLayout = pipelineLayout(m_device, meshGroups);
    const std::array rigidAttributes{
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3, .offset = 0, .shaderLocation = 0},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3, .offset = 12, .shaderLocation = 1},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x2, .offset = 24, .shaderLocation = 2},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4, .offset = 32, .shaderLocation = 3},
    };
    const std::array skinAttributes{
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Uint16x4, .offset = 0, .shaderLocation = 4},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4, .offset = 8, .shaderLocation = 5},
    };
    static_assert(sizeof(asset::Vertex) == 48 && sizeof(asset::VertexSkin) == 24);
    std::array<wgpu::VertexBufferLayout, 2> buffers{};
    buffers[0].arrayStride = sizeof(asset::Vertex);
    buffers[0].attributeCount = rigidAttributes.size();
    buffers[0].attributes = rigidAttributes.data();
    buffers[1].arrayStride = sizeof(asset::VertexSkin);
    buffers[1].attributeCount = skinAttributes.size();
    buffers[1].attributes = skinAttributes.data();
    // A blended surface lets (1 - alpha) of what is behind it through.
    wgpu::BlendState blend;
    blend.color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::SrcAlpha,
                   .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha};
    blend.alpha = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One,
                   .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha};
    for (std::uint32_t index = 0; index < m_meshPipelines.size(); ++index)
    {
        const bool skinned = (index & skinnedBit) != 0;
        const bool blended = (index & blendedBit) != 0;
        wgpu::ColorTargetState target;
        target.format = sceneFormat;
        target.blend = blended ? &blend : nullptr;
        wgpu::FragmentState fragmentState;
        fragmentState.module = mesh;
        fragmentState.entryPoint = "meshFragment";
        fragmentState.targetCount = 1;
        fragmentState.targets = &target;
        // Reversed depth: nearer is greater. Blended surfaces test it without writing it.
        wgpu::DepthStencilState depth;
        depth.format = depthFormat;
        depth.depthWriteEnabled = !blended;
        depth.depthCompare = wgpu::CompareFunction::Greater;
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = "Mesh";
        descriptor.layout = meshLayout;
        descriptor.vertex.module = mesh;
        descriptor.vertex.entryPoint = skinned ? "skinnedVertex" : "meshVertex";
        descriptor.vertex.bufferCount = skinned ? 2 : 1;
        descriptor.vertex.buffers = buffers.data();
        descriptor.primitive.frontFace = wgpu::FrontFace::CCW;
        descriptor.primitive.cullMode = (index & bothSidesBit) != 0 ? wgpu::CullMode::None : wgpu::CullMode::Back;
        descriptor.depthStencil = &depth;
        descriptor.fragment = &fragmentState;
        m_meshPipelines[index] = m_device.CreateRenderPipeline(&descriptor);
    }

    // The depth of the casters of shadows, through the matrix of their view, with the slope bias of Vulkan.
    const std::array shadowEntries{storageEntry(0, wgpu::ShaderStage::Vertex), storageEntry(1, wgpu::ShaderStage::Vertex),
                                   uniformEntry(2, wgpu::ShaderStage::Vertex, true, sizeof(math::Mat4))};
    m_shadowLayout = bindGroupLayout(m_device, "Shadow", shadowEntries);
    const std::array shadowGroups{m_shadowLayout, m_materialLayout, m_meshDrawLayout};
    const wgpu::PipelineLayout shadowLayout = pipelineLayout(m_device, shadowGroups);
    const wgpu::ShaderModule shadow = shaderModule("webgpu_shadow");
    m_unclippedDepth = m_device.HasFeature(wgpu::FeatureName::DepthClipControl);
    for (std::uint32_t index = 0; index < m_shadowPipelines.size(); ++index)
    {
        const bool skinned = (index & skinnedBit) != 0;
        wgpu::FragmentState fragmentState;
        fragmentState.module = shadow;
        fragmentState.entryPoint = "shadowFragment";
        wgpu::DepthStencilState depth;
        depth.format = depthFormat;
        depth.depthWriteEnabled = true;
        depth.depthCompare = wgpu::CompareFunction::LessEqual;
        // Steep surfaces need more bias than those facing the light.
        depth.depthBiasSlopeScale = (index & atlasBit) != 0 ? 2.0f : 1.5f;
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = "Shadow";
        descriptor.layout = shadowLayout;
        descriptor.vertex.module = shadow;
        descriptor.vertex.entryPoint = skinned ? "skinnedShadowVertex" : "shadowVertex";
        descriptor.vertex.bufferCount = skinned ? 2 : 1;
        descriptor.vertex.buffers = buffers.data();
        descriptor.primitive.cullMode = wgpu::CullMode::None;
        descriptor.primitive.unclippedDepth = m_unclippedDepth;
        descriptor.depthStencil = &depth;
        descriptor.fragment = &fragmentState;
        m_shadowPipelines[index] = m_device.CreateRenderPipeline(&descriptor);
    }
    wgpu::TextureDescriptor cascadeMap;
    cascadeMap.label = "Cascades";
    cascadeMap.size = {shadowMapSize, shadowMapSize, 4};
    cascadeMap.format = depthFormat;
    cascadeMap.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    m_cascadeMap = m_device.CreateTexture(&cascadeMap);
    for (std::uint32_t layer = 0; layer < m_cascadeLayers.size(); ++layer)
    {
        wgpu::TextureViewDescriptor view;
        view.dimension = wgpu::TextureViewDimension::e2D;
        view.baseArrayLayer = layer;
        view.arrayLayerCount = 1;
        m_cascadeLayers[layer] = m_cascadeMap.CreateView(&view);
    }
    wgpu::TextureDescriptor atlasMap = cascadeMap;
    atlasMap.label = "Shadow atlas";
    atlasMap.size = {shadowAtlasSize, shadowAtlasSize, 1};
    m_atlasMap = m_device.CreateTexture(&atlasMap);
    // Bilinear comparisons, as Vulkan's sampler of shadows.
    wgpu::SamplerDescriptor shadowSampler;
    shadowSampler.addressModeU = shadowSampler.addressModeV = shadowSampler.addressModeW = wgpu::AddressMode::ClampToEdge;
    shadowSampler.magFilter = shadowSampler.minFilter = wgpu::FilterMode::Linear;
    shadowSampler.compare = wgpu::CompareFunction::LessEqual;
    m_shadowSampler = m_device.CreateSampler(&shadowSampler);

    const wgpu::ShaderModule sky = shaderModule("webgpu_sky");
    wgpu::ColorTargetState skyTarget;
    skyTarget.format = sceneFormat;
    wgpu::FragmentState skyFragment;
    skyFragment.module = sky;
    skyFragment.entryPoint = "skyFragment";
    skyFragment.targetCount = 1;
    skyFragment.targets = &skyTarget;
    wgpu::DepthStencilState skyDepth;
    skyDepth.format = depthFormat;
    skyDepth.depthWriteEnabled = false;
    skyDepth.depthCompare = wgpu::CompareFunction::Always;
    const std::array skyGroups{m_skyLayout};
    wgpu::RenderPipelineDescriptor skyPipeline;
    skyPipeline.label = "Sky";
    skyPipeline.layout = pipelineLayout(m_device, skyGroups);
    skyPipeline.vertex.module = sky;
    skyPipeline.vertex.entryPoint = "skyVertex";
    skyPipeline.depthStencil = &skyDepth;
    skyPipeline.fragment = &skyFragment;
    m_skyPipeline = m_device.CreateRenderPipeline(&skyPipeline);

    wgpu::BufferDescriptor scene;
    scene.label = "Scene";
    scene.size = sizeof(SceneData);
    scene.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    m_sceneBuffer = m_device.CreateBuffer(&scene);
    scene.label = "Sky";
    scene.size = sizeof(SkyData);
    m_skyBuffer = m_device.CreateBuffer(&scene);

    // A flat normal, for the normal map of a material without one.
    wgpu::TextureDescriptor flat;
    flat.size = {1, 1, 1};
    flat.format = wgpu::TextureFormat::RGBA8Unorm;
    flat.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    m_flatNormal = m_device.CreateTexture(&flat);
    const std::array<std::uint8_t, 4> normal{128, 128, 255, 255};
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = m_flatNormal;
    wgpu::TexelCopyBufferLayout rows;
    rows.bytesPerRow = 4;
    const wgpu::Extent3D one{1, 1, 1};
    m_queue.WriteTexture(&destination, normal.data(), normal.size(), &rows, &one);

    m_defaultMaterial = createMaterial({});
    m_environment.irradiance = whiteCube();
    m_environment.specular = whiteCube();
    m_environment.specularMips = 1;
    bakeBrdfTable();
    return {};
}

wgpu::Texture WebGpuRenderer::whiteCube() const
{
    // A sky of one colour lights as white maps tinted by that colour, as Vulkan bakes a white texture.
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "White environment";
    descriptor.size = {1, 1, 6};
    descriptor.format = environmentFormat;
    descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    const wgpu::Texture cube = m_device.CreateTexture(&descriptor);
    // Half floats of 1.
    const std::array<std::uint16_t, 4> white{0x3C00, 0x3C00, 0x3C00, 0x3C00};
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = cube;
        destination.origin = {0, 0, face};
        wgpu::TexelCopyBufferLayout rows;
        rows.bytesPerRow = 8;
        const wgpu::Extent3D one{1, 1, 1};
        m_queue.WriteTexture(&destination, white.data(), sizeof(white), &rows, &one);
    }
    return cube;
}

void WebGpuRenderer::bakeBrdfTable()
{
    const wgpu::ShaderModule ibl = shaderModule("ibl");
    wgpu::ComputePipelineDescriptor descriptor;
    descriptor.label = "BRDF table";
    descriptor.compute.module = ibl;
    descriptor.compute.entryPoint = "brdfTable";
    const wgpu::ComputePipeline pipeline = m_device.CreateComputePipeline(&descriptor);
    wgpu::TextureDescriptor table;
    table.label = "BRDF table";
    table.size = {brdfTableSize, brdfTableSize, 1};
    table.format = environmentFormat;
    table.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding;
    m_brdfTable = m_device.CreateTexture(&table);
    const BakeParameters parameters{.size = brdfTableSize, .sampleCount = brdfSamples};
    const wgpu::Buffer uniform = uniformBuffer(m_device, m_queue, &parameters, sizeof(parameters), "Bake");
    const std::array entries{viewBinding(3, m_brdfTable.CreateView()), bufferBinding(6, uniform, sizeof(parameters))};
    const wgpu::BindGroup group = bindGroup(m_device, pipeline.GetBindGroupLayout(0), entries);
    const wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    const wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, group);
    pass.DispatchWorkgroups((brdfTableSize + 7) / 8, (brdfTableSize + 7) / 8);
    pass.End();
    const wgpu::CommandBuffer commands = encoder.Finish();
    m_queue.Submit(1, &commands);
}

void WebGpuRenderer::bakeEnvironment(TextureHandle sky)
{
    m_environment.sky = sky;
    const GpuTexture* const source = m_textures.find(sky);
    if (source == nullptr || !source->bindable)
    {
        m_environment.irradiance = whiteCube();
        m_environment.specular = whiteCube();
        m_environment.specularMips = 1;
        return;
    }
    const wgpu::ShaderModule ibl = shaderModule("ibl");
    const auto pipeline = [&](const char* entry) {
        wgpu::ComputePipelineDescriptor descriptor;
        descriptor.label = entry;
        descriptor.compute.module = ibl;
        descriptor.compute.entryPoint = entry;
        return m_device.CreateComputePipeline(&descriptor);
    };
    const wgpu::ComputePipeline toCube = pipeline("equirectangularToCube");
    const wgpu::ComputePipeline prefilter = pipeline("prefilterSpecular");
    const wgpu::ComputePipeline irradiance = pipeline("irradiance");
    const auto cube = [&](std::uint32_t size, std::uint32_t mips, const char* label) {
        wgpu::TextureDescriptor descriptor;
        descriptor.label = label;
        descriptor.size = {size, size, 6};
        descriptor.mipLevelCount = mips;
        descriptor.format = environmentFormat;
        descriptor.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding;
        return m_device.CreateTexture(&descriptor);
    };
    const std::uint32_t radianceMips = static_cast<std::uint32_t>(std::bit_width(radianceSize));
    const wgpu::Texture radiance = cube(radianceSize, radianceMips, "Radiance");
    const wgpu::Texture specular = cube(specularSize, specularMipCount, "Specular environment");
    const wgpu::Texture diffuse = cube(irradianceSize, 1, "Irradiance environment");

    // The samplers of vulkan::EnvironmentBaker: the map wraps around, and the cube is read between its texels.
    wgpu::SamplerDescriptor wrap;
    wrap.addressModeU = wgpu::AddressMode::Repeat;
    wrap.addressModeV = wgpu::AddressMode::ClampToEdge;
    wrap.magFilter = wrap.minFilter = wgpu::FilterMode::Linear;
    wrap.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    const wgpu::Sampler equirectangularSampler = m_device.CreateSampler(&wrap);
    wgpu::SamplerDescriptor clamp = wrap;
    clamp.addressModeU = clamp.addressModeV = clamp.addressModeW = wgpu::AddressMode::ClampToEdge;
    const wgpu::Sampler cubeSampler = m_device.CreateSampler(&clamp);

    std::vector<wgpu::Buffer> uniforms;
    const wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    const auto dispatch = [&](const wgpu::ComputePipeline& compute, std::span<const wgpu::BindGroupEntry> entries,
                              const BakeParameters& parameters) {
        uniforms.push_back(uniformBuffer(m_device, m_queue, &parameters, sizeof(parameters), "Bake"));
        std::vector<wgpu::BindGroupEntry> all(entries.begin(), entries.end());
        all.push_back(bufferBinding(6, uniforms.back(), sizeof(parameters)));
        const wgpu::BindGroup group = bindGroup(m_device, compute.GetBindGroupLayout(0), all);
        const wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(compute);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups((parameters.size + 7) / 8, (parameters.size + 7) / 8, 6);
        pass.End();
    };
    // Every level of the radiance cube samples the map at a matching detail.
    const wgpu::TextureView equirectangular = source->texture.CreateView();
    for (std::uint32_t mip = 0; mip < radianceMips; ++mip)
    {
        const std::uint32_t size = std::max(radianceSize >> mip, 1U);
        const float sourceLod =
            std::max(std::log2(static_cast<float>(source->size.width) / (4.0f * static_cast<float>(size))), 0.0f);
        const std::array entries{viewBinding(0, equirectangular), viewBinding(2, facesView(radiance, mip)),
                                 samplerBinding(4, equirectangularSampler)};
        dispatch(toCube, entries, {.size = size, .sourceLod = sourceLod});
    }
    const wgpu::TextureView radianceCube = cubeView(radiance);
    for (std::uint32_t mip = 0; mip < specularMipCount; ++mip)
    {
        const std::array entries{viewBinding(1, radianceCube), viewBinding(2, facesView(specular, mip)), samplerBinding(5, cubeSampler)};
        dispatch(prefilter, entries,
                 {.size = std::max(specularSize >> mip, 1U),
                  .roughness = static_cast<float>(mip) / static_cast<float>(specularMipCount - 1),
                  .sampleCount = prefilterSamples,
                  .sourceSize = static_cast<float>(radianceSize)});
    }
    const std::array entries{viewBinding(1, radianceCube), viewBinding(2, facesView(diffuse, 0)), samplerBinding(5, cubeSampler)};
    dispatch(irradiance, entries,
             {.size = irradianceSize, .sampleCount = irradianceSamples, .sourceSize = static_cast<float>(radianceSize)});
    const wgpu::CommandBuffer commands = encoder.Finish();
    m_queue.Submit(1, &commands);
    m_environment.irradiance = diffuse;
    m_environment.specular = specular;
    m_environment.specularMips = specularMipCount;
}

wgpu::BindGroup WebGpuRenderer::materialGroup(GpuMaterial& material)
{
    if (material.group && material.textureEpoch == m_textureEpoch)
    {
        return material.group;
    }
    const MaterialDesc& description = material.description;
    const MaterialData data{
        .baseColorFactor = description.baseColorFactor,
        .emissiveFactor = description.emissiveFactor,
        .alphaCutoff = description.alphaCutoff,
        .metallicFactor = description.metallicFactor,
        .roughnessFactor = description.roughnessFactor,
        .normalScale = description.normalScale,
        .occlusionStrength = description.occlusionStrength,
        .alphaMode = static_cast<std::uint32_t>(description.alphaMode),
        .doubleSided = description.doubleSided ? 1U : 0U,
    };
    if (!material.uniform)
    {
        wgpu::BufferDescriptor descriptor;
        descriptor.label = "Material";
        descriptor.size = sizeof(MaterialData);
        descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        material.uniform = m_device.CreateBuffer(&descriptor);
    }
    m_queue.WriteBuffer(material.uniform, 0, &data, sizeof(data));
    // Invalid or destroyed textures sample as white, or as a flat normal for the normal texture.
    const auto textureOf = [this](TextureHandle handle, const wgpu::Texture& fallback) {
        const GpuTexture* const texture = m_textures.find(handle);
        return texture != nullptr && texture->bindable ? std::pair{texture->texture, texture->nearest}
                                                       : std::pair{fallback, false};
    };
    const std::array textures{textureOf(description.baseColorTexture, m_white),
                              textureOf(description.metallicRoughnessTexture, m_white),
                              textureOf(description.normalTexture, m_flatNormal),
                              textureOf(description.occlusionTexture, m_white),
                              textureOf(description.emissiveTexture, m_white)};
    std::array<wgpu::BindGroupEntry, 11> entries{};
    entries[0] = bufferBinding(0, material.uniform, sizeof(MaterialData));
    for (std::uint32_t texture = 0; texture < textures.size(); ++texture)
    {
        entries[1 + texture] = viewBinding(1 + texture, textures[texture].first.CreateView());
        entries[6 + texture] = samplerBinding(6 + texture, textures[texture].second ? m_nearestSampler : m_linearSampler);
    }
    material.group = bindGroup(m_device, m_materialLayout, entries);
    material.textureEpoch = m_textureEpoch;
    return material.group;
}

void WebGpuRenderer::prepareMeshes(const ViewMatrices& matrices, const Frustum& frustum)
{
    // The environment follows the sky texture of the world.
    if (m_world.environment.sky != m_environment.sky ||
        (m_environment.sky.isValid() && !m_textures.contains(m_environment.sky)))
    {
        bakeEnvironment(m_world.environment.sky);
    }

    const RenderSun& sun = m_world.sun;
    const float sunLength = math::length(sun.direction);
    packLights(m_world, m_lights);

    // The cascades of the sun as Vulkan fits them, and the views of the atlas the lights that cast shadows are given.
    const RenderCamera& camera = m_world.camera;
    const float aspectRatio = static_cast<float>(m_sceneSize.width) / static_cast<float>(std::max(m_sceneSize.height, 1U));
    const bool sunEnabled = std::max({sun.illuminance.r, sun.illuminance.g, sun.illuminance.b}) > 0.0f;
    std::optional<ShadowCascades> cascades;
    if (sunEnabled && sun.castShadows && sun.shadowDistance > camera.nearPlane)
    {
        cascades = computeShadowCascades(math::inverse(camera.view), ViewVolume::of(camera, aspectRatio), camera.nearPlane,
                                         sun.shadowDistance, sun.direction, shadowMapSize);
    }
    const ShadowAssignment assigned =
        assignShadowViews(m_world.lights, frustum, matrices.cameraPosition,
                          ShadowAtlasLayout{.size = shadowAtlasSize, .cell = shadowAtlasCell, .largeLights = largeShadowLights}, flipY());
    for (std::size_t index = 0; index < m_lights.size() && index < assigned.firstView.size(); ++index)
    {
        m_lights[index].firstShadowView = assigned.firstView[index];
        if (assigned.firstView[index] >= 0)
        {
            m_lights[index].shadowFar = assigned.far[index];
        }
    }

    SceneData scene{
        .viewProjection = matrices.viewProjection,
        .view = m_world.camera.view,
        .cameraPosition = matrices.cameraPosition,
        .exposure = m_exposure,
        .sunDirection = sunLength > 0.0f ? sun.direction / sunLength : math::Vec3{0.0f, -1.0f, 0.0f},
        .sunEnabled = math::length(sun.illuminance) > 0.0f ? 1U : 0U,
        .sunIlluminance = sun.illuminance,
        .lightCount = static_cast<std::uint32_t>(m_lights.size()),
        .sky = m_world.environment.color * m_world.environment.intensity,
        .environmentRotation = m_world.environment.rotation,
        .specularMipCount = static_cast<float>(m_environment.specularMips),
    };
    if (cascades)
    {
        scene.sunCastsShadows = 1;
        scene.shadowDistance = sun.shadowDistance;
        for (std::size_t cascade = 0; cascade < 4; ++cascade)
        {
            scene.cascadeViewProjections[cascade] = cascades->viewProjections[cascade];
            scene.cascadeSplits[static_cast<int>(cascade)] = cascades->splitDistances[cascade];
            scene.cascadeTexelSizes[static_cast<int>(cascade)] = cascades->texelSizes[cascade];
        }
    }
    m_queue.WriteBuffer(m_sceneBuffer, 0, &scene, sizeof(scene));
    const SkyData sky{
        .skyInverseViewProjection = math::inverse(matrices.skyViewProjection),
        .sky = scene.sky,
        .exposure = m_exposure,
        .environmentRotation = m_world.environment.rotation,
    };
    m_queue.WriteBuffer(m_skyBuffer, 0, &sky, sizeof(sky));

    // The instances the camera sees, by batches of one mesh, submesh, material and way to be drawn; blended ones alone,
    // the farthest first.
    struct Visible
    {
        const MeshInstance* instance;
        const GpuMesh* mesh;
        MaterialHandle material;
        std::uint32_t pipeline;
        float distance;
    };
    std::vector<Visible> visible;
    for (const MeshInstance& instance : m_world.meshes)
    {
        const GpuMesh* const mesh = m_meshes.find(instance.mesh);
        if (mesh == nullptr || instance.submesh >= mesh->submeshes.size())
        {
            continue;
        }
        const bool skinned = instance.boneCount > 0 && mesh->skin;
        // A skinned pose reaches past the box of its rest: it is always drawn.
        if (!skinned && !mesh->bounds.isEmpty() && !frustum.intersects(math::transform(instance.transform, mesh->bounds)))
        {
            continue;
        }
        MaterialHandle material = m_materials.contains(instance.material) ? instance.material : m_defaultMaterial;
        const MaterialDesc& description = m_materials.find(material)->description;
        std::uint32_t pipeline = skinned ? skinnedBit : 0;
        pipeline |= description.doubleSided ? bothSidesBit : 0;
        pipeline |= description.alphaMode == asset::AlphaMode::Blend ? blendedBit : 0;
        const math::Vec3 centre = math::Vec3(instance.transform[3]);
        const math::Vec3 toCamera = centre - matrices.cameraPosition;
        visible.push_back({&instance, mesh, material, pipeline, math::dot(toCamera, toCamera)});
    }
    std::ranges::stable_sort(visible, [](const Visible& left, const Visible& right) {
        const bool leftBlended = (left.pipeline & blendedBit) != 0;
        const bool rightBlended = (right.pipeline & blendedBit) != 0;
        if (leftBlended != rightBlended)
        {
            return !leftBlended;
        }
        if (leftBlended)
        {
            return left.distance > right.distance;
        }
        return std::tuple(left.pipeline, left.material.index, left.mesh, left.instance->submesh) <
               std::tuple(right.pipeline, right.material.index, right.mesh, right.instance->submesh);
    });
    m_meshBatches.clear();
    std::vector<InstanceData> instances;
    instances.reserve(visible.size());
    for (const Visible& entry : visible)
    {
        const bool blended = (entry.pipeline & blendedBit) != 0;
        if (blended || m_meshBatches.empty() || m_meshBatches.back().mesh != entry.mesh ||
            m_meshBatches.back().submesh != entry.instance->submesh || m_meshBatches.back().material != entry.material ||
            m_meshBatches.back().pipeline != entry.pipeline)
        {
            m_meshBatches.push_back({.mesh = entry.mesh,
                                     .submesh = entry.instance->submesh,
                                     .material = entry.material,
                                     .pipeline = entry.pipeline,
                                     .firstInstance = static_cast<std::uint32_t>(instances.size()),
                                     .distance = entry.distance});
        }
        ++m_meshBatches.back().instanceCount;
        instances.push_back({.world = entry.instance->transform,
                             .firstBone = entry.instance->firstBone,
                             .boneCount = entry.instance->boneCount});
    }

    // The casters of each view of the shadows, by batches as the camera's, after them: opaque and cut-out surfaces cast
    // shadows, blended ones do not.
    struct ShadowView
    {
        math::Mat4 drawn;
        Frustum frustum;
        bool atlas;
        std::uint32_t layer;
        std::array<float, 4> viewport;
    };
    std::vector<ShadowView> views;
    if (cascades)
    {
        for (std::uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            const math::Mat4& read = cascades->viewProjections[cascade];
            views.push_back({flipY() * read, frustumOf(read), false, cascade, {}});
        }
    }
    for (const render::ShadowView& view : assigned.views)
    {
        const float size = static_cast<float>(shadowAtlasSize);
        views.push_back({flipY() * view.viewProjection, frustumOf(view.viewProjection), true, 0,
                         {view.tile.x * size, view.tile.y * size, view.tile.z * size, view.tile.w * size}});
    }
    m_shadowBatches.clear();
    m_shadowPasses.clear();
    struct Caster
    {
        const MeshInstance* instance;
        const GpuMesh* mesh;
        MaterialHandle material;
        std::uint32_t pipeline;
    };
    std::vector<Caster> casters;
    for (std::uint32_t index = 0; index < views.size(); ++index)
    {
        const ShadowView& view = views[index];
        casters.clear();
        for (const MeshInstance& instance : m_world.meshes)
        {
            const GpuMesh* const mesh = m_meshes.find(instance.mesh);
            if (mesh == nullptr || instance.submesh >= mesh->submeshes.size())
            {
                continue;
            }
            const MaterialHandle material = m_materials.contains(instance.material) ? instance.material : m_defaultMaterial;
            if (m_materials.find(material)->description.alphaMode == asset::AlphaMode::Blend)
            {
                continue;
            }
            const bool skinned = instance.boneCount > 0 && mesh->skin;
            if (!skinned && !mesh->bounds.isEmpty() && !view.frustum.intersects(math::transform(instance.transform, mesh->bounds)))
            {
                continue;
            }
            casters.push_back({&instance, mesh, material, (skinned ? skinnedBit : 0U) | (view.atlas ? atlasBit : 0U)});
        }
        std::ranges::sort(casters, [](const Caster& left, const Caster& right) {
            return std::tuple(left.pipeline, left.material.index, left.mesh, left.instance->submesh) <
                   std::tuple(right.pipeline, right.material.index, right.mesh, right.instance->submesh);
        });
        ShadowPass pass{.firstBatch = static_cast<std::uint32_t>(m_shadowBatches.size()),
                        .view = index,
                        .atlas = view.atlas,
                        .layer = view.layer,
                        .viewport = view.viewport};
        for (const Caster& caster : casters)
        {
            if (m_shadowBatches.size() == pass.firstBatch || m_shadowBatches.back().mesh != caster.mesh ||
                m_shadowBatches.back().submesh != caster.instance->submesh || m_shadowBatches.back().material != caster.material ||
                m_shadowBatches.back().pipeline != caster.pipeline)
            {
                m_shadowBatches.push_back({.mesh = caster.mesh,
                                           .submesh = caster.instance->submesh,
                                           .material = caster.material,
                                           .pipeline = caster.pipeline,
                                           .firstInstance = static_cast<std::uint32_t>(instances.size())});
            }
            ++m_shadowBatches.back().instanceCount;
            instances.push_back({.world = caster.instance->transform,
                                 .firstBone = caster.instance->firstBone,
                                 .boneCount = caster.instance->boneCount});
        }
        pass.batchCount = static_cast<std::uint32_t>(m_shadowBatches.size()) - pass.firstBatch;
        m_shadowPasses.push_back(pass);
    }

    const std::uint64_t lightBytes = std::max<std::size_t>(m_lights.size(), 1) * sizeof(GpuLight);
    const std::uint64_t instanceBytes = std::max<std::size_t>(instances.size(), 1) * sizeof(InstanceData);
    const std::uint64_t boneBytes = std::max<std::size_t>(m_world.boneMatrices.size(), 1) * sizeof(math::Mat4);
    growBuffer(m_lightBuffer, m_lightCapacity, lightBytes, wgpu::BufferUsage::Storage, "Lights");
    growBuffer(m_instanceBuffer, m_instanceCapacity, instanceBytes, wgpu::BufferUsage::Storage, "Instances");
    growBuffer(m_boneBuffer, m_boneCapacity, boneBytes, wgpu::BufferUsage::Storage, "Bones");
    const std::size_t drawCount = m_meshBatches.size() + m_shadowBatches.size();
    growBuffer(m_meshDrawBuffer, m_meshDrawCapacity, std::max<std::size_t>(drawCount, 1) * uniformStride,
               wgpu::BufferUsage::Uniform, "Mesh draws");
    const std::uint64_t shadowViewBytes = std::max<std::size_t>(assigned.views.size(), 1) * sizeof(render::ShadowView);
    growBuffer(m_shadowViewBuffer, m_shadowViewCapacity, shadowViewBytes, wgpu::BufferUsage::Storage, "Shadow views");
    if (!assigned.views.empty())
    {
        m_queue.WriteBuffer(m_shadowViewBuffer, 0, assigned.views.data(), assigned.views.size() * sizeof(render::ShadowView));
    }
    growBuffer(m_shadowMatrixBuffer, m_shadowMatrixCapacity, std::max<std::size_t>(views.size(), 1) * uniformStride,
               wgpu::BufferUsage::Uniform, "Shadow matrices");
    std::vector<std::byte> shadowMatrices(views.size() * uniformStride);
    for (std::size_t view = 0; view < views.size(); ++view)
    {
        std::memcpy(shadowMatrices.data() + view * uniformStride, &views[view].drawn, sizeof(math::Mat4));
    }
    if (!shadowMatrices.empty())
    {
        m_queue.WriteBuffer(m_shadowMatrixBuffer, 0, shadowMatrices.data(), shadowMatrices.size());
    }
    if (!m_lights.empty())
    {
        m_queue.WriteBuffer(m_lightBuffer, 0, m_lights.data(), m_lights.size() * sizeof(GpuLight));
    }
    if (!instances.empty())
    {
        m_queue.WriteBuffer(m_instanceBuffer, 0, instances.data(), instances.size() * sizeof(InstanceData));
    }
    if (!m_world.boneMatrices.empty())
    {
        m_queue.WriteBuffer(m_boneBuffer, 0, m_world.boneMatrices.data(), m_world.boneMatrices.size() * sizeof(math::Mat4));
    }
    // The draws of the camera, then those of the shadows.
    std::vector<std::byte> draws(drawCount * uniformStride);
    for (std::size_t batch = 0; batch < drawCount; ++batch)
    {
        const DrawData data{.first = batch < m_meshBatches.size() ? m_meshBatches[batch].firstInstance
                                                                  : m_shadowBatches[batch - m_meshBatches.size()].firstInstance};
        std::memcpy(draws.data() + batch * uniformStride, &data, sizeof(data));
    }
    if (!draws.empty())
    {
        m_queue.WriteBuffer(m_meshDrawBuffer, 0, draws.data(), draws.size());
    }

    wgpu::TextureViewDescriptor cascadeArrayView;
    cascadeArrayView.dimension = wgpu::TextureViewDimension::e2DArray;
    const wgpu::TextureView cascadeArray = m_cascadeMap.CreateView(&cascadeArrayView);
    const std::array sceneEntries{bufferBinding(0, m_sceneBuffer, sizeof(SceneData)), bufferBinding(1, m_lightBuffer, lightBytes),
                                  bufferBinding(2, m_instanceBuffer, instanceBytes), bufferBinding(3, m_boneBuffer, boneBytes),
                                  viewBinding(4, cubeView(m_environment.irradiance)), viewBinding(5, cubeView(m_environment.specular)),
                                  viewBinding(6, m_brdfTable.CreateView()), samplerBinding(7, m_clampSampler),
                                  bufferBinding(8, m_shadowViewBuffer, shadowViewBytes), viewBinding(9, cascadeArray),
                                  viewBinding(10, m_atlasMap.CreateView()), samplerBinding(11, m_shadowSampler)};
    m_sceneGroup = bindGroup(m_device, m_sceneLayout, sceneEntries);
    const std::array shadowEntries{bufferBinding(0, m_instanceBuffer, instanceBytes), bufferBinding(1, m_boneBuffer, boneBytes),
                                   bufferBinding(2, m_shadowMatrixBuffer, sizeof(math::Mat4))};
    m_shadowGroup = bindGroup(m_device, m_shadowLayout, shadowEntries);
    const std::array drawEntries{bufferBinding(0, m_meshDrawBuffer, sizeof(DrawData))};
    m_meshDrawGroup = bindGroup(m_device, m_meshDrawLayout, drawEntries);

    // The sky texture, or white for a sky of one colour.
    const GpuTexture* const skyTexture = m_textures.find(m_world.environment.sky);
    wgpu::SamplerDescriptor wrap;
    wrap.addressModeU = wgpu::AddressMode::Repeat;
    wrap.addressModeV = wgpu::AddressMode::ClampToEdge;
    wrap.magFilter = wrap.minFilter = wgpu::FilterMode::Linear;
    wrap.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    const std::array skyEntries{bufferBinding(0, m_skyBuffer, sizeof(SkyData)),
                                viewBinding(1, skyTexture != nullptr && skyTexture->bindable ? skyTexture->texture.CreateView()
                                                                                             : m_white.CreateView()),
                                samplerBinding(2, m_device.CreateSampler(&wrap))};
    m_skyGroup = bindGroup(m_device, m_skyLayout, skyEntries);
}

void WebGpuRenderer::drawSky(const wgpu::RenderPassEncoder& pass)
{
    pass.SetPipeline(m_skyPipeline);
    pass.SetBindGroup(0, m_skyGroup);
    pass.Draw(3);
    ++m_drawCalls;
}

void WebGpuRenderer::drawShadows(const wgpu::CommandEncoder& encoder)
{
    const auto drawBatches = [this](const wgpu::RenderPassEncoder& pass, const ShadowPass& shadow) {
        const auto view = static_cast<std::uint32_t>(std::uint64_t{shadow.view} * uniformStride);
        std::uint32_t bound = 0xFFFFFFFFU;
        for (std::uint32_t batch = shadow.firstBatch; batch < shadow.firstBatch + shadow.batchCount; ++batch)
        {
            const MeshBatch& drawn = m_shadowBatches[batch];
            GpuMaterial* const material = m_materials.find(drawn.material);
            if (material == nullptr)
            {
                continue;
            }
            if (drawn.pipeline != bound)
            {
                pass.SetPipeline(m_shadowPipelines[drawn.pipeline]);
                pass.SetBindGroup(0, m_shadowGroup, 1, &view);
                bound = drawn.pipeline;
            }
            pass.SetBindGroup(1, materialGroup(*material));
            const auto offset = static_cast<std::uint32_t>(std::uint64_t{m_meshBatches.size() + batch} * uniformStride);
            pass.SetBindGroup(2, m_meshDrawGroup, 1, &offset);
            pass.SetVertexBuffer(0, drawn.mesh->vertices);
            if ((drawn.pipeline & skinnedBit) != 0)
            {
                pass.SetVertexBuffer(1, drawn.mesh->skin);
            }
            pass.SetIndexBuffer(drawn.mesh->indices, wgpu::IndexFormat::Uint32);
            const asset::Submesh& submesh = drawn.mesh->submeshes[drawn.submesh];
            pass.DrawIndexed(submesh.indexCount, drawn.instanceCount, submesh.firstIndex);
            ++m_drawCalls;
        }
    };
    const auto begin = [&encoder](const wgpu::TextureView& target, const char* label) {
        wgpu::RenderPassDepthStencilAttachment depth;
        depth.view = target;
        depth.depthLoadOp = wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor descriptor;
        descriptor.label = label;
        descriptor.depthStencilAttachment = &depth;
        return encoder.BeginRenderPass(&descriptor);
    };
    // Each cascade into its layer.
    for (const ShadowPass& shadow : m_shadowPasses)
    {
        if (!shadow.atlas)
        {
            const wgpu::RenderPassEncoder pass = begin(m_cascadeLayers[shadow.layer], "Cascade");
            drawBatches(pass, shadow);
            pass.End();
        }
    }
    // The lights into their tiles of the atlas, at once.
    if (std::ranges::any_of(m_shadowPasses, [](const ShadowPass& shadow) { return shadow.atlas; }))
    {
        const wgpu::RenderPassEncoder pass = begin(m_atlasMap.CreateView(), "Shadow atlas");
        for (const ShadowPass& shadow : m_shadowPasses)
        {
            if (shadow.atlas)
            {
                pass.SetViewport(shadow.viewport[0], shadow.viewport[1], shadow.viewport[2], shadow.viewport[3], 0.0f, 1.0f);
                pass.SetScissorRect(static_cast<std::uint32_t>(shadow.viewport[0]), static_cast<std::uint32_t>(shadow.viewport[1]),
                                    static_cast<std::uint32_t>(shadow.viewport[2]), static_cast<std::uint32_t>(shadow.viewport[3]));
                drawBatches(pass, shadow);
            }
        }
        pass.End();
    }
}

void WebGpuRenderer::drawMeshes(const wgpu::RenderPassEncoder& pass, bool blended)
{
    std::uint32_t bound = 0xFFFFFFFFU;
    for (std::uint32_t batch = 0; batch < m_meshBatches.size(); ++batch)
    {
        const MeshBatch& drawn = m_meshBatches[batch];
        if (((drawn.pipeline & blendedBit) != 0) != blended)
        {
            continue;
        }
        GpuMaterial* const material = m_materials.find(drawn.material);
        if (material == nullptr)
        {
            continue;
        }
        if (drawn.pipeline != bound)
        {
            pass.SetPipeline(m_meshPipelines[drawn.pipeline]);
            pass.SetBindGroup(0, m_sceneGroup);
            bound = drawn.pipeline;
        }
        pass.SetBindGroup(1, materialGroup(*material));
        const auto offset = static_cast<std::uint32_t>(std::uint64_t{batch} * uniformStride);
        pass.SetBindGroup(2, m_meshDrawGroup, 1, &offset);
        pass.SetVertexBuffer(0, drawn.mesh->vertices);
        if ((drawn.pipeline & skinnedBit) != 0)
        {
            pass.SetVertexBuffer(1, drawn.mesh->skin);
        }
        pass.SetIndexBuffer(drawn.mesh->indices, wgpu::IndexFormat::Uint32);
        const asset::Submesh& submesh = drawn.mesh->submeshes[drawn.submesh];
        pass.DrawIndexed(submesh.indexCount, drawn.instanceCount, submesh.firstIndex);
        ++m_drawCalls;
    }
}

} // namespace devex::render::webgpu
