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
    std::uint32_t decalCount = 0;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    std::array<math::Mat4, 4> cascadeViewProjections{};
    math::Vec4 cascadeSplits{0.0f};
    math::Vec4 cascadeTexelSizes{0.0f};
    float shadowDistance = 0.0f;
    std::uint32_t sunCastsShadows = 0;
    float time = 0.0f;
    float padding1 = 0.0f;
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Vec3 fogColor{0.0f};
    float fogDensity = 0.0f;
    float fogHeight = 0.0f;
    float fogFalloff = 1.0f;
    float fogStart = 0.0f;
    float fogMaxOpacity = 1.0f;
    float fogSunScattering = 0.0f;
    float fogSunAnisotropy = 0.0f;
    float fogSkyBlend = 0.0f;
    float padding2 = 0.0f;
    math::Mat4 inverseViewProjection{1.0f};
};
struct TaaData
{
    float blend = 0.9f;
    float historyValid = 0.0f;
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
    math::Mat4 inverseViewProjection{1.0f};
    math::Mat4 previousViewProjection{1.0f};
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Mat4 previousSkyViewProjection{1.0f};
};
struct BloomData
{
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
    float strength = 1.0f;
    float padding = 0.0f;
};
// A decal of the frame, as webgpu_mesh.slang reads it: its box, its fades, and where its textures lie in the atlases.
struct AtlasDecalData
{
    math::Mat4 worldToDecal{1.0f};
    math::Vec4 axisX{0.0f};
    math::Vec4 axisZ{0.0f};
    math::Vec4 axisY{0.0f};
    math::Vec4 modulate{1.0f};
    float albedoMix = 1.0f;
    float emissionEnergy = 1.0f;
    float normalFade = 0.0f;
    float upperFade = 0.3f;
    float lowerFade = 0.3f;
    float fadeBegin = 0.0f;
    float fadeLength = 0.0f;
    float padding = 0.0f;
    std::array<math::Vec4, 4> rects{};
};
static_assert(sizeof(AtlasDecalData) == 224);
// What the blit of a texture into an atlas reads.
struct BlitData
{
    math::Vec4 rect{0.0f};
    float targetWidth = 0.0f;
    float targetHeight = 0.0f;
    float padding[2]{};
};
// A terrain, as webgpu_terrain.slang reads it, at an offset of its own.
struct TerrainUniform
{
    math::Vec3 origin{0.0f};
    float size = 1.0f;
    std::uint32_t resolution = 0;
    std::uint32_t layerCount = 0;
    float textureSize = 4.0f;
    float blendSharpness = 0.5f;
    std::uint32_t triplanar = 0;
    std::int32_t autoBaseLayer = 0;
    std::int32_t autoSlopeLayer = -1;
    float autoSlope = 0.0f;
    std::int32_t autoHeightLayer = -1;
    float autoHeight = 0.0f;
    float autoBlend = 0.0f;
    float windStrength = 0.0f;
    float windSpeed = 0.0f;
    float grassDistance = 0.0f;
    float leafRange = 0.0f;
    std::uint32_t gridCells = terrainBlockCells;
    math::Vec3 grassColor{0.0f};
    float grassHeight = 0.0f;
    math::Vec3 grassTipColor{0.0f};
    float grassWidth = 0.0f;
    std::array<math::Vec4, 8> baseColorFactors{};
    std::array<math::Vec4, 8> layerFactors{};
};
static_assert(sizeof(TerrainUniform) == 368);
constexpr std::uint64_t terrainStride = 512;
// What a draw of a terrain reads.
struct TerrainDrawData
{
    math::Vec3 cameraPosition{0.0f};
    std::uint32_t firstTile = 0;
    std::uint32_t bladesPerTile = 1;
    float bladeWidthScale = 1.0f;
    float time = 0.0f;
    float padding = 0.0f;
};
// The textures of the layers of a terrain, drawn into arrays at this size, with their mips.
constexpr std::uint32_t terrainLayerSize = 512;
constexpr std::uint32_t terrainLayerMips = 10;

// The atlases of the decals: as wide as webgpu_mesh.slang says, every texture no larger than half of it.
constexpr std::uint32_t decalAtlasSize = 2048;
constexpr wgpu::TextureFormat decalColorFormat = wgpu::TextureFormat::RGBA8UnormSrgb;
constexpr wgpu::TextureFormat decalDataFormat = wgpu::TextureFormat::RGBA8Unorm;

// What every step of the lens reads, as webgpu_lens.slang has it, at an offset of its own.
struct LensData
{
    float targetWidth = 0.0f;
    float targetHeight = 0.0f;
    float imageWidth = 0.0f;
    float imageHeight = 0.0f;
    float aperture = 0.0f;
    float focalLength = 0.0f;
    float focusDistance = 0.0f;
    float amount = 0.0f;
    float maxRadius = 0.0f;
    std::uint32_t autoFocus = 0;
    float shutter = 0.0f;
    float tileSize = 8.0f;
    std::uint32_t fromDepthOfField = 0;
    std::uint32_t frame = 0;
    float padding[2]{};
    math::Mat4 inverseViewProjection{1.0f};
    math::Mat4 previousViewProjection{1.0f};
    math::Mat4 view{1.0f};
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Mat4 previousSkyViewProjection{1.0f};
};
static_assert(sizeof(LensData) == 384);
// The steps of the lens, each at an offset of its own: four for the depth of field and its combination, three for the
// motion blur.
constexpr std::uint32_t lensSteps = 8;
constexpr std::uint64_t lensStride = 512;
// The images of its chain, as lens_filter.slang numbers them.
enum LensImage : std::size_t
{
    lensCircleOfConfusion,
    lensHalved,
    lensBokeh,
    lensFiltered,
    lensFocused,
    lensTiles,
    lensNeighbours,
};
constexpr wgpu::TextureFormat confusionFormat = wgpu::TextureFormat::R16Float;
constexpr wgpu::TextureFormat motionFormat = wgpu::TextureFormat::RG16Float;
// What a still pixel keeps of its history, as Vulkan keeps it.
constexpr float taaBlend = 0.9f;
// The steps of the bloom: the threshold, four halvings and four additions.
constexpr std::uint32_t bloomSteps = 9;
struct ParticleDrawData
{
    std::uint32_t first = 0;
    std::uint32_t facing = 0;
    std::uint32_t flags = 0;
    std::uint32_t textured = 0;
    math::Vec2 sheet{1.0f};
    float softness = 0.0f;
    std::uint32_t padding = 0;
};
struct InstanceData
{
    math::Mat4 world{1.0f};
    std::uint32_t firstBone = 0;
    std::uint32_t boneCount = 0;
    // Where the values it gives the instance uniforms of its shader start; none without.
    std::uint32_t instanceParameters = noInstanceParameters;
    std::uint32_t padding = 0;
};

// The vertices of meshes as WebGPU reads them: rigid, then the skin of a skinned one in a second buffer.
[[nodiscard]] std::array<wgpu::VertexBufferLayout, 2> meshVertexBuffers()
{
    static const std::array rigid{
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3, .offset = 0, .shaderLocation = 0},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3, .offset = 12, .shaderLocation = 1},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x2, .offset = 24, .shaderLocation = 2},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4, .offset = 32, .shaderLocation = 3},
    };
    static const std::array skin{
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Uint16x4, .offset = 0, .shaderLocation = 4},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4, .offset = 8, .shaderLocation = 5},
    };
    std::array<wgpu::VertexBufferLayout, 2> buffers{};
    buffers[0].arrayStride = sizeof(asset::Vertex);
    buffers[0].attributeCount = rigid.size();
    buffers[0].attributes = rigid.data();
    buffers[1].arrayStride = sizeof(asset::VertexSkin);
    buffers[1].attributeCount = skin.size();
    buffers[1].attributes = skin.data();
    return buffers;
}
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
// What a draw of meshes reads: its first instance, and for the shaders of the project the time and the camera.
struct DrawData
{
    std::uint32_t first = 0;
    float time = 0.0f;
    std::uint32_t padding[2]{};
    math::Vec3 cameraPosition{0.0f};
    std::uint32_t padding1 = 0;
};
// The uniforms of the shader of a material, as webgpu_custom.slang has them.
constexpr std::size_t customSlotCount = 64;
constexpr std::uint32_t customTextureCount = 8;
constexpr std::uint32_t customBit = 8;
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
static_assert(sizeof(SceneData) == 688 && sizeof(ParticleDrawData) == 32 && sizeof(InstanceData) == 80 && sizeof(MaterialData) == 64 &&
              sizeof(BakeParameters) == 32);

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
                                  storageEntry(8, fragment), cascades, atlas, comparison, textureEntry(12, fragment),
                                  samplerEntry(13, fragment), storageEntry(14, fragment), textureEntry(15, fragment),
                                  textureEntry(16, fragment), samplerEntry(17, fragment), storageEntry(18, both)};
    m_sceneLayout = bindGroupLayout(m_device, "Scene", sceneEntries);
    // The shaders of the project: the slots of the uniforms of a material and its textures; and what their shadows
    // read in place of the scene: the instances, the bones, the instance uniforms and the view.
    // Each texture has its sampler, which filters it as the texture asks.
    std::array<wgpu::BindGroupLayoutEntry, 1 + 2 * customTextureCount> customEntries{};
    customEntries[0] = uniformEntry(0, both, false, customSlotCount * sizeof(math::Vec4));
    for (std::uint32_t texture = 0; texture < customTextureCount; ++texture)
    {
        customEntries[1 + texture] = textureEntry(1 + texture, both);
        customEntries[1 + customTextureCount + texture] = samplerEntry(1 + customTextureCount + texture, both);
    }
    m_customMaterialLayout = bindGroupLayout(m_device, "Shader material", customEntries);
    const std::array customShadowEntries{storageEntry(2, wgpu::ShaderStage::Vertex), storageEntry(3, wgpu::ShaderStage::Vertex),
                                         storageEntry(18, both), uniformEntry(19, wgpu::ShaderStage::Vertex, true, sizeof(math::Mat4))};
    m_customShadowLayout = bindGroupLayout(m_device, "Shader shadow", customShadowEntries);
    // The blit that draws the textures of the decals into their atlases.
    const std::array blitEntries{uniformEntry(0, both, true, sizeof(BlitData)), textureEntry(1, fragment), samplerEntry(2, fragment)};
    m_blitLayout = bindGroupLayout(m_device, "Decal atlas", blitEntries);
    const wgpu::ShaderModule blit = shaderModule("webgpu_blit");
    for (const auto& [format, pipeline] : {std::pair{decalColorFormat, &m_blitColors}, std::pair{decalDataFormat, &m_blitData}})
    {
        wgpu::ColorTargetState target;
        target.format = format;
        wgpu::FragmentState fragmentState;
        fragmentState.module = blit;
        fragmentState.entryPoint = "blitFragment";
        fragmentState.targetCount = 1;
        fragmentState.targets = &target;
        const std::array groups{m_blitLayout};
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = "Decal atlas";
        descriptor.layout = pipelineLayout(m_device, groups);
        descriptor.vertex.module = blit;
        descriptor.vertex.entryPoint = "blitVertex";
        descriptor.fragment = &fragmentState;
        *pipeline = m_device.CreateRenderPipeline(&descriptor);
    }
    createTerrainPipelines();
    std::array<wgpu::BindGroupLayoutEntry, 11> materialEntries{};
    materialEntries[0] = uniformEntry(0, fragment, false, sizeof(MaterialData));
    for (std::uint32_t texture = 0; texture < 5; ++texture)
    {
        materialEntries[1 + texture] = textureEntry(1 + texture, fragment);
        materialEntries[6 + texture] = samplerEntry(6 + texture, fragment);
    }
    m_materialLayout = bindGroupLayout(m_device, "Material", materialEntries);
    const std::array drawEntries{uniformEntry(0, both, true, sizeof(DrawData))};
    m_meshDrawLayout = bindGroupLayout(m_device, "Mesh draw", drawEntries);

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
    const std::array skyGroups{m_sceneLayout};
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

WebGpuRenderer::GpuShader* WebGpuRenderer::spatialShaderOf(const MaterialDesc& material)
{
    GpuShader* const shader = material.shader.isValid() ? m_shaders.find(material.shader) : nullptr;
    return shader != nullptr && shader->kind == asset::ShaderKind::Spatial && shader->module ? shader : nullptr;
}

wgpu::BindGroup WebGpuRenderer::customMaterialGroup(GpuMaterial& material)
{
    if (material.customGroup && material.customEpoch == m_textureEpoch)
    {
        return material.customGroup;
    }
    const MaterialDesc& description = material.description;
    // The values of the uniforms, then the number of its binding in the slot of each texture.
    std::array<math::Vec4, customSlotCount> slots{};
    for (std::size_t slot = 0; slot < std::min(description.parameters.size(), slots.size()); ++slot)
    {
        slots[slot] = description.parameters[slot];
    }
    std::array<wgpu::Texture, customTextureCount> textures;
    textures.fill(m_white);
    std::array<wgpu::Sampler, customTextureCount> samplers;
    samplers.fill(m_linearSampler);
    for (std::uint32_t index = 0; index < std::min<std::size_t>(description.textures.size(), customTextureCount); ++index)
    {
        const MaterialTexture& bound = description.textures[index];
        const GpuTexture* const texture = m_textures.find(bound.texture);
        const bool bindable = texture != nullptr && texture->bindable;
        textures[index] = bindable                                    ? texture->texture
                          : bound.fallback == TextureFallback::Normal ? m_flatNormal
                          : bound.fallback == TextureFallback::Black  ? m_black
                                                                      : m_white;
        if (bindable && texture->nearest)
        {
            samplers[index] = m_nearestSampler;
        }
        if (bound.slot < slots.size())
        {
            slots[bound.slot].x = std::bit_cast<float>(index);
        }
    }
    if (!material.slots)
    {
        wgpu::BufferDescriptor descriptor;
        descriptor.label = "Shader material";
        descriptor.size = sizeof(slots);
        descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        material.slots = m_device.CreateBuffer(&descriptor);
    }
    m_queue.WriteBuffer(material.slots, 0, slots.data(), sizeof(slots));
    std::array<wgpu::BindGroupEntry, 1 + 2 * customTextureCount> entries{};
    entries[0] = bufferBinding(0, material.slots, sizeof(slots));
    for (std::uint32_t texture = 0; texture < customTextureCount; ++texture)
    {
        entries[1 + texture] = viewBinding(1 + texture, textures[texture].CreateView());
        entries[1 + customTextureCount + texture] = samplerBinding(1 + customTextureCount + texture, samplers[texture]);
    }
    material.customGroup = bindGroup(m_device, m_customMaterialLayout, entries);
    material.customEpoch = m_textureEpoch;
    return material.customGroup;
}

const wgpu::RenderPipeline& WebGpuRenderer::customMeshPipeline(GpuShader& shader, bool skinned)
{
    wgpu::RenderPipeline& pipeline = shader.meshes[skinned ? 1 : 0];
    if (pipeline)
    {
        return pipeline;
    }
    const std::array<wgpu::VertexBufferLayout, 2> buffers = meshVertexBuffers();
    // A blended surface leaves its colour premultiplied, and an additive one an alpha of zero.
    wgpu::BlendState blend;
    blend.color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One, .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha};
    blend.alpha = blend.color;
    wgpu::ColorTargetState target;
    target.format = sceneFormat;
    target.blend = shader.transparent ? &blend : nullptr;
    wgpu::FragmentState fragmentState;
    fragmentState.module = shader.module;
    fragmentState.entryPoint = "sceneFragment";
    fragmentState.targetCount = 1;
    fragmentState.targets = &target;
    wgpu::DepthStencilState depth;
    depth.format = depthFormat;
    depth.depthWriteEnabled = !shader.transparent;
    depth.depthCompare = wgpu::CompareFunction::Greater;
    const std::array groups{m_sceneLayout, m_customMaterialLayout, m_meshDrawLayout};
    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.label = "Shader of the project";
    descriptor.layout = pipelineLayout(m_device, groups);
    descriptor.vertex.module = shader.module;
    descriptor.vertex.entryPoint = skinned ? "skinnedVertex" : "meshVertex";
    descriptor.vertex.bufferCount = skinned ? 2 : 1;
    descriptor.vertex.buffers = buffers.data();
    descriptor.primitive.frontFace = wgpu::FrontFace::CCW;
    descriptor.primitive.cullMode = shader.cull == asset::ShaderCull::Front    ? wgpu::CullMode::Front
                                    : shader.cull == asset::ShaderCull::Back ? wgpu::CullMode::Back
                                                                             : wgpu::CullMode::None;
    descriptor.depthStencil = &depth;
    descriptor.fragment = &fragmentState;
    pipeline = m_device.CreateRenderPipeline(&descriptor);
    return pipeline;
}

const wgpu::RenderPipeline& WebGpuRenderer::customShadowPipeline(GpuShader& shader, bool skinned, bool atlas)
{
    wgpu::RenderPipeline& pipeline = shader.shadows[(skinned ? 1 : 0) + (atlas ? 2 : 0)];
    if (pipeline)
    {
        return pipeline;
    }
    const std::array<wgpu::VertexBufferLayout, 2> buffers = meshVertexBuffers();
    wgpu::FragmentState fragmentState;
    fragmentState.module = shader.module;
    fragmentState.entryPoint = "shadowFragment";
    wgpu::DepthStencilState depth;
    depth.format = depthFormat;
    depth.depthWriteEnabled = true;
    depth.depthCompare = wgpu::CompareFunction::LessEqual;
    depth.depthBiasSlopeScale = atlas ? 2.0f : 1.5f;
    const std::array groups{m_customShadowLayout, m_customMaterialLayout, m_meshDrawLayout};
    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.label = "Shadow of a shader of the project";
    descriptor.layout = pipelineLayout(m_device, groups);
    descriptor.vertex.module = shader.module;
    descriptor.vertex.entryPoint = skinned ? "skinnedShadowVertex" : "shadowVertex";
    descriptor.vertex.bufferCount = skinned ? 2 : 1;
    descriptor.vertex.buffers = buffers.data();
    descriptor.primitive.cullMode = wgpu::CullMode::None;
    descriptor.primitive.unclippedDepth = m_unclippedDepth;
    descriptor.depthStencil = &depth;
    descriptor.fragment = &fragmentState;
    pipeline = m_device.CreateRenderPipeline(&descriptor);
    return pipeline;
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
    // The height fog, as Vulkan packs it: a density of 0 without one.
    scene.skyInverseViewProjection = math::inverse(matrices.skyViewProjection);
    scene.inverseViewProjection = matrices.inverseViewProjection;
    const GpuAtmosphere air = packAtmosphere(m_world, matrices.cameraPosition.y).gpu;
    scene.fogColor = air.fogColor;
    scene.fogDensity = air.fogDensity;
    scene.fogHeight = air.fogHeight;
    scene.fogFalloff = air.fogFalloff;
    scene.fogStart = air.fogStart;
    scene.fogMaxOpacity = air.fogMaxOpacity;
    scene.fogSunScattering = air.fogSunScattering;
    scene.fogSunAnisotropy = air.fogSunAnisotropy;
    scene.fogSkyBlend = air.fogSkyBlend;
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
    prepareDecals();
    scene.decalCount = m_decalCount;
    scene.viewportWidth = static_cast<float>(m_sceneSize.width);
    scene.viewportHeight = static_cast<float>(m_sceneSize.height);
    scene.time = m_time;
    m_queue.WriteBuffer(m_sceneBuffer, 0, &scene, sizeof(scene));

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
        // A shader of the project chooses its sides and its blending.
        if (const GpuShader* const shader = spatialShaderOf(description))
        {
            pipeline |= customBit | (shader->transparent ? blendedBit : 0);
        }
        else
        {
            pipeline |= description.doubleSided ? bothSidesBit : 0;
            pipeline |= description.alphaMode == asset::AlphaMode::Blend ? blendedBit : 0;
        }
        visible.push_back({&instance, mesh, material, pipeline, sortDistance(math::Vec3(instance.transform[3]), matrices.cameraPosition)});
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
                             .boneCount = entry.instance->boneCount,
                             .instanceParameters = instanceParametersOf(m_world, entry.instance->instanceParameters)});
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
            const MaterialDesc& description = m_materials.find(material)->description;
            const GpuShader* const shader = spatialShaderOf(description);
            if (shader != nullptr ? shader->transparent || !shader->castsShadows : description.alphaMode == asset::AlphaMode::Blend)
            {
                continue;
            }
            const bool skinned = instance.boneCount > 0 && mesh->skin;
            if (!skinned && !mesh->bounds.isEmpty() && !view.frustum.intersects(math::transform(instance.transform, mesh->bounds)))
            {
                continue;
            }
            casters.push_back({&instance, mesh, material,
                               (skinned ? skinnedBit : 0U) | (view.atlas ? atlasBit : 0U) | (shader != nullptr ? customBit : 0U)});
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
                                 .boneCount = caster.instance->boneCount,
                                 .instanceParameters = instanceParametersOf(m_world, caster.instance->instanceParameters)});
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
    // The values objects give the instance uniforms of their shaders.
    const std::uint64_t instanceParameterBytes = std::max<std::size_t>(m_world.instanceParameters.size(), 1) * sizeof(math::Vec4);
    growBuffer(m_instanceParameterBuffer, m_instanceParameterCapacity, instanceParameterBytes, wgpu::BufferUsage::Storage,
               "Instance parameters");
    if (!m_world.instanceParameters.empty())
    {
        m_queue.WriteBuffer(m_instanceParameterBuffer, 0, m_world.instanceParameters.data(),
                            m_world.instanceParameters.size() * sizeof(math::Vec4));
    }
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
                                                                  : m_shadowBatches[batch - m_meshBatches.size()].firstInstance,
                            .time = m_time,
                            .cameraPosition = matrices.cameraPosition};
        std::memcpy(draws.data() + batch * uniformStride, &data, sizeof(data));
    }
    if (!draws.empty())
    {
        m_queue.WriteBuffer(m_meshDrawBuffer, 0, draws.data(), draws.size());
    }

    // The sky texture, or white for a sky of one colour, wrapping around but not over the poles.
    const GpuTexture* const skyTexture = m_textures.find(m_world.environment.sky);
    wgpu::SamplerDescriptor wrap;
    wrap.addressModeU = wgpu::AddressMode::Repeat;
    wrap.addressModeV = wgpu::AddressMode::ClampToEdge;
    wrap.magFilter = wrap.minFilter = wgpu::FilterMode::Linear;
    wrap.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    wgpu::TextureViewDescriptor cascadeArrayView;
    cascadeArrayView.dimension = wgpu::TextureViewDimension::e2DArray;
    const wgpu::TextureView cascadeArray = m_cascadeMap.CreateView(&cascadeArrayView);
    const std::array sceneEntries{bufferBinding(0, m_sceneBuffer, sizeof(SceneData)), bufferBinding(1, m_lightBuffer, lightBytes),
                                  bufferBinding(2, m_instanceBuffer, instanceBytes), bufferBinding(3, m_boneBuffer, boneBytes),
                                  viewBinding(4, cubeView(m_environment.irradiance)), viewBinding(5, cubeView(m_environment.specular)),
                                  viewBinding(6, m_brdfTable.CreateView()), samplerBinding(7, m_clampSampler),
                                  bufferBinding(8, m_shadowViewBuffer, shadowViewBytes), viewBinding(9, cascadeArray),
                                  viewBinding(10, m_atlasMap.CreateView()), samplerBinding(11, m_shadowSampler),
                                  viewBinding(12, skyTexture != nullptr && skyTexture->bindable ? skyTexture->texture.CreateView()
                                                                                                : m_white.CreateView()),
                                  samplerBinding(13, m_device.CreateSampler(&wrap)),
                                  bufferBinding(14, m_decalBuffer, m_decalCapacity),
                                  viewBinding(15, m_decalColors.view ? m_decalColors.view : m_white.CreateView()),
                                  viewBinding(16, m_decalData.view ? m_decalData.view : m_white.CreateView()),
                                  samplerBinding(17, m_clampSampler),
                                  bufferBinding(18, m_instanceParameterBuffer, m_instanceParameterCapacity)};
    m_sceneGroup = bindGroup(m_device, m_sceneLayout, sceneEntries);
    const std::array shadowEntries{bufferBinding(0, m_instanceBuffer, instanceBytes), bufferBinding(1, m_boneBuffer, boneBytes),
                                   bufferBinding(2, m_shadowMatrixBuffer, sizeof(math::Mat4))};
    m_shadowGroup = bindGroup(m_device, m_shadowLayout, shadowEntries);
    const std::array customShadowEntries{bufferBinding(2, m_instanceBuffer, instanceBytes), bufferBinding(3, m_boneBuffer, boneBytes),
                                         bufferBinding(18, m_instanceParameterBuffer, m_instanceParameterCapacity),
                                         bufferBinding(19, m_shadowMatrixBuffer, sizeof(math::Mat4))};
    m_customShadowGroup = bindGroup(m_device, m_customShadowLayout, customShadowEntries);
    const std::array drawEntries{bufferBinding(0, m_meshDrawBuffer, sizeof(DrawData))};
    m_meshDrawGroup = bindGroup(m_device, m_meshDrawLayout, drawEntries);

}

void WebGpuRenderer::drawSky(const wgpu::RenderPassEncoder& pass)
{
    pass.SetPipeline(m_skyPipeline);
    pass.SetBindGroup(0, m_sceneGroup);
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
            GpuShader* const shader = (drawn.pipeline & customBit) != 0 ? spatialShaderOf(material->description) : nullptr;
            if (shader != nullptr)
            {
                pass.SetPipeline(customShadowPipeline(*shader, (drawn.pipeline & skinnedBit) != 0, shadow.atlas));
                pass.SetBindGroup(0, m_customShadowGroup, 1, &view);
                bound = 0xFFFFFFFFU;
                pass.SetBindGroup(1, customMaterialGroup(*material));
            }
            else
            {
                if (drawn.pipeline != bound)
                {
                    pass.SetPipeline(m_shadowPipelines[drawn.pipeline & ~customBit]);
                    pass.SetBindGroup(0, m_shadowGroup, 1, &view);
                    bound = drawn.pipeline;
                }
                pass.SetBindGroup(1, materialGroup(*material));
            }
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
            drawTerrains(pass, shadow.view, false);
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
                drawTerrains(pass, shadow.view, true);
            }
        }
        pass.End();
    }
}

void WebGpuRenderer::drawMeshBatch(const wgpu::RenderPassEncoder& pass, std::uint32_t batch, std::uint32_t& bound)
{
    const MeshBatch& drawn = m_meshBatches[batch];
    GpuMaterial* const material = m_materials.find(drawn.material);
    if (material == nullptr)
    {
        return;
    }
    GpuShader* const shader = (drawn.pipeline & customBit) != 0 ? spatialShaderOf(material->description) : nullptr;
    if (shader != nullptr)
    {
        // Each shader of the project has pipelines of its own.
        pass.SetPipeline(customMeshPipeline(*shader, (drawn.pipeline & skinnedBit) != 0));
        pass.SetBindGroup(0, m_sceneGroup);
        bound = 0xFFFFFFFFU;
        pass.SetBindGroup(1, customMaterialGroup(*material));
    }
    else
    {
        if (drawn.pipeline != bound)
        {
            pass.SetPipeline(m_meshPipelines[drawn.pipeline & ~customBit]);
            pass.SetBindGroup(0, m_sceneGroup);
            bound = drawn.pipeline;
        }
        pass.SetBindGroup(1, materialGroup(*material));
    }
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

void WebGpuRenderer::drawMeshes(const wgpu::RenderPassEncoder& pass, bool blended)
{
    std::uint32_t bound = 0xFFFFFFFFU;
    for (std::uint32_t batch = 0; batch < m_meshBatches.size(); ++batch)
    {
        if (((m_meshBatches[batch].pipeline & blendedBit) != 0) == blended)
        {
            drawMeshBatch(pass, batch, bound);
        }
    }
}

float WebGpuRenderer::sortDistance(math::Vec3 point, math::Vec3 cameraPosition) const noexcept
{
    if (m_world.camera.projection == Projection::Orthographic)
    {
        return -(m_world.camera.view * math::Vec4(point, 1.0f)).z;
    }
    const math::Vec3 toCamera = point - cameraPosition;
    return math::dot(toCamera, toCamera);
}

core::Result<void> WebGpuRenderer::createParticlePipelines()
{
    const wgpu::ShaderStage both = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    wgpu::BindGroupLayoutEntry depth = textureEntry(5, wgpu::ShaderStage::Fragment);
    depth.texture.sampleType = wgpu::TextureSampleType::Depth;
    const std::array particleEntries{storageEntry(0, wgpu::ShaderStage::Vertex), storageEntry(1, wgpu::ShaderStage::Vertex),
                                     storageEntry(2, wgpu::ShaderStage::Vertex), textureEntry(3, wgpu::ShaderStage::Fragment),
                                     samplerEntry(4, wgpu::ShaderStage::Fragment), depth};
    m_particleLayout = bindGroupLayout(m_device, "Particles", particleEntries);
    const std::array drawEntries{uniformEntry(0, both, true, sizeof(ParticleDrawData))};
    m_particleDrawLayout = bindGroupLayout(m_device, "Particle draw", drawEntries);
    const std::array groups{m_sceneLayout, m_particleLayout, m_particleDrawLayout};
    const wgpu::PipelineLayout layout = pipelineLayout(m_device, groups);
    const wgpu::ShaderModule module = shaderModule("webgpu_particle");
    // Premultiplied colours, which additive particles leave with an alpha of zero.
    wgpu::BlendState premultiplied;
    premultiplied.color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One,
                           .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha};
    premultiplied.alpha = premultiplied.color;
    for (std::uint32_t index = 0; index < m_particlePipelines.size(); ++index)
    {
        const bool ribbons = index == 1;
        wgpu::ColorTargetState target;
        target.format = sceneFormat;
        target.blend = &premultiplied;
        wgpu::FragmentState fragment;
        fragment.module = module;
        fragment.entryPoint = ribbons ? "ribbonFragment" : "particleFragment";
        fragment.targetCount = 1;
        fragment.targets = &target;
        wgpu::DepthStencilState depthState;
        depthState.format = depthFormat;
        depthState.depthWriteEnabled = false;
        depthState.depthCompare = wgpu::CompareFunction::GreaterEqual;
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = ribbons ? "Ribbons" : "Particles";
        descriptor.layout = layout;
        descriptor.vertex.module = module;
        descriptor.vertex.entryPoint = ribbons ? "ribbonVertex" : "particleVertex";
        descriptor.depthStencil = &depthState;
        descriptor.fragment = &fragment;
        m_particlePipelines[index] = m_device.CreateRenderPipeline(&descriptor);
    }
    return {};
}

void WebGpuRenderer::prepareParticles(const ViewMatrices& matrices)
{
    packParticles(m_world, matrices.cameraPosition, m_particles, m_points);
    const std::uint64_t particleBytes = std::max<std::size_t>(m_particles.size(), 1) * sizeof(GpuParticle);
    const std::uint64_t pointBytes = std::max<std::size_t>(m_points.size(), 1) * sizeof(GpuTrailPoint);
    const std::uint64_t segmentBytes = std::max<std::size_t>(m_world.trailSegments.size(), 1) * sizeof(std::uint32_t);
    growBuffer(m_particleBuffer, m_particleCapacity, particleBytes, wgpu::BufferUsage::Storage, "Particles");
    growBuffer(m_pointBuffer, m_pointCapacity, pointBytes, wgpu::BufferUsage::Storage, "Trail points");
    growBuffer(m_segmentBuffer, m_segmentCapacity, segmentBytes, wgpu::BufferUsage::Storage, "Trail segments");
    if (!m_particles.empty())
    {
        m_queue.WriteBuffer(m_particleBuffer, 0, m_particles.data(), m_particles.size() * sizeof(GpuParticle));
    }
    if (!m_points.empty())
    {
        m_queue.WriteBuffer(m_pointBuffer, 0, m_points.data(), m_points.size() * sizeof(GpuTrailPoint));
    }
    if (!m_world.trailSegments.empty())
    {
        m_queue.WriteBuffer(m_segmentBuffer, 0, m_world.trailSegments.data(), m_world.trailSegments.size() * sizeof(std::uint32_t));
    }
    const std::vector<ParticleDraw>& draws = m_world.particleDraws;
    growBuffer(m_particleDrawBuffer, m_particleDrawCapacity, std::max<std::size_t>(draws.size(), 1) * uniformStride,
               wgpu::BufferUsage::Uniform, "Particle draws");
    std::vector<std::byte> data(draws.size() * uniformStride);
    m_particleGroups.clear();
    for (std::size_t index = 0; index < draws.size(); ++index)
    {
        const ParticleDraw& draw = draws[index];
        const GpuTexture* const texture = m_textures.find(draw.texture);
        const bool textured = texture != nullptr && texture->bindable;
        const ParticleDrawData drawData{
            .first = draw.first,
            .facing = static_cast<std::uint32_t>(draw.facing),
            .flags = (draw.blend == ParticleBlend::Additive ? 1U : 0U) | (draw.lit ? 2U : 0U),
            .textured = textured ? 1U : 0U,
            .sheet = {static_cast<float>(std::max(draw.sheetColumns, 1U)), static_cast<float>(std::max(draw.sheetRows, 1U))},
            .softness = std::max(draw.softness, 0.0f),
        };
        std::memcpy(data.data() + index * uniformStride, &drawData, sizeof(drawData));
        const std::array entries{bufferBinding(0, m_particleBuffer, particleBytes), bufferBinding(1, m_pointBuffer, pointBytes),
                                 bufferBinding(2, m_segmentBuffer, segmentBytes),
                                 viewBinding(3, textured ? texture->texture.CreateView() : m_white.CreateView()),
                                 samplerBinding(4, textured && texture->nearest ? m_nearestSampler : m_linearSampler),
                                 viewBinding(5, m_depthView)};
        m_particleGroups.push_back(bindGroup(m_device, m_particleLayout, entries));
    }
    if (!data.empty())
    {
        m_queue.WriteBuffer(m_particleDrawBuffer, 0, data.data(), data.size());
    }
    const std::array drawEntries{bufferBinding(0, m_particleDrawBuffer, sizeof(ParticleDrawData))};
    m_particleDrawGroup = bindGroup(m_device, m_particleDrawLayout, drawEntries);
}

void WebGpuRenderer::orderTransparent(const ViewMatrices& matrices)
{
    // Blended meshes and particles lie in the layer "Default", which sprites place themselves around: by layer, then
    // order, then from the farthest. The blended batches come sorted, and sprite batches in this order among themselves,
    // which a stable sort keeps.
    struct Item
    {
        std::int32_t layer = 0;
        std::int32_t order = 0;
        float distance = 0.0f;
        TransparentDraw::Kind kind = TransparentDraw::Kind::Mesh;
        std::uint32_t index = 0;
    };
    std::vector<Item> items;
    for (std::uint32_t batch = 0; batch < m_meshBatches.size(); ++batch)
    {
        if ((m_meshBatches[batch].pipeline & blendedBit) != 0)
        {
            items.push_back({.distance = m_meshBatches[batch].distance, .kind = TransparentDraw::Kind::Mesh, .index = batch});
        }
    }
    for (std::uint32_t draw = 0; draw < m_world.particleDraws.size(); ++draw)
    {
        if (m_world.particleDraws[draw].count > 0)
        {
            items.push_back({.distance = sortDistance(m_world.particleDraws[draw].center, matrices.cameraPosition),
                             .kind = TransparentDraw::Kind::Particles,
                             .index = draw});
        }
    }
    for (std::uint32_t batch = 0; batch < m_spriteFrame.batches.size(); ++batch)
    {
        const SpriteBatch& sprites = m_spriteFrame.batches[batch];
        if (sprites.count > 0)
        {
            items.push_back({.layer = sprites.layer, .order = sprites.order, .distance = sprites.distance,
                             .kind = TransparentDraw::Kind::Sprites, .index = batch});
        }
    }
    std::ranges::stable_sort(items, [](const Item& a, const Item& b) {
        if (a.layer != b.layer)
        {
            return a.layer < b.layer;
        }
        if (a.order != b.order)
        {
            return a.order < b.order;
        }
        return a.distance > b.distance;
    });

    // Sprite batches that follow each other lie one after the other among the sprites: they are drawn by runs of one
    // texture and one normal map.
    m_transparentDraws.clear();
    m_spriteRuns.clear();
    for (std::size_t at = 0; at < items.size();)
    {
        if (items[at].kind != TransparentDraw::Kind::Sprites)
        {
            m_transparentDraws.push_back({.kind = items[at].kind, .index = items[at].index});
            ++at;
            continue;
        }
        const std::uint32_t first = m_spriteFrame.batches[items[at].index].first;
        std::uint32_t end = first;
        while (at < items.size() && items[at].kind == TransparentDraw::Kind::Sprites &&
               m_spriteFrame.batches[items[at].index].first == end)
        {
            end += m_spriteFrame.batches[items[at].index].count;
            ++at;
        }
        TransparentDraw draw{.kind = TransparentDraw::Kind::Sprites, .firstRun = static_cast<std::uint32_t>(m_spriteRuns.size())};
        for (std::uint32_t index = first; index < end; ++index)
        {
            const GpuSprite& sprite = m_spriteFrame.sprites[index];
            if (m_spriteRuns.size() == draw.firstRun || m_spriteRuns.back().texture != sprite.texture ||
                m_spriteRuns.back().normalTexture != sprite.normalTexture)
            {
                m_spriteRuns.push_back({.first = index, .count = 0, .texture = sprite.texture, .normalTexture = sprite.normalTexture});
            }
            ++m_spriteRuns.back().count;
        }
        draw.runCount = static_cast<std::uint32_t>(m_spriteRuns.size()) - draw.firstRun;
        m_transparentDraws.push_back(draw);
    }
}

void WebGpuRenderer::drawTransparent(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& sprites)
{
    std::uint32_t bound = 0xFFFFFFFFU;
    for (const TransparentDraw& draw : m_transparentDraws)
    {
        switch (draw.kind)
        {
        case TransparentDraw::Kind::Mesh:
            drawMeshBatch(pass, draw.index, bound);
            break;
        case TransparentDraw::Kind::Particles:
        {
            const ParticleDraw& particles = m_world.particleDraws[draw.index];
            pass.SetPipeline(m_particlePipelines[particles.ribbons ? 1 : 0]);
            pass.SetBindGroup(0, m_sceneGroup);
            pass.SetBindGroup(1, m_particleGroups[draw.index]);
            const auto offset = static_cast<std::uint32_t>(std::uint64_t{draw.index} * uniformStride);
            pass.SetBindGroup(2, m_particleDrawGroup, 1, &offset);
            // Six corners for each particle or segment; the instance says which.
            pass.Draw(6, particles.count);
            ++m_drawCalls;
            bound = 0xFFFFFFFFU;
            break;
        }
        case TransparentDraw::Kind::Sprites:
            pass.SetPipeline(m_spritePipeline);
            for (std::uint32_t run = draw.firstRun; run < draw.firstRun + draw.runCount; ++run)
            {
                const SpriteRun& drawn = m_spriteRuns[run];
                const auto offset = static_cast<std::uint32_t>(std::uint64_t{run} * uniformStride);
                pass.SetBindGroup(0, sprites, 1, &offset);
                pass.SetBindGroup(1, drawn.group);
                pass.Draw(6, drawn.count);
                ++m_drawCalls;
            }
            bound = 0xFFFFFFFFU;
            break;
        }
    }
}

core::Result<void> WebGpuRenderer::createPostPipelines()
{
    const wgpu::ShaderStage fragment = wgpu::ShaderStage::Fragment;
    wgpu::BindGroupLayoutEntry depth = textureEntry(3, fragment);
    depth.texture.sampleType = wgpu::TextureSampleType::Depth;
    const std::array taaEntries{uniformEntry(0, fragment, false, sizeof(TaaData)), textureEntry(1, fragment),
                                textureEntry(2, fragment), depth, samplerEntry(4, fragment)};
    m_taaLayout = bindGroupLayout(m_device, "Temporal antialiasing", taaEntries);
    const std::array bloomEntries{uniformEntry(0, fragment, true, sizeof(BloomData)), textureEntry(1, fragment),
                                  samplerEntry(2, fragment)};
    m_bloomLayout = bindGroupLayout(m_device, "Bloom", bloomEntries);
    const auto fullscreen = [this](const wgpu::BindGroupLayout& layout, const wgpu::ShaderModule& module, const char* vertex,
                                   const char* fragmentEntry, const wgpu::BlendState* blend) {
        wgpu::ColorTargetState target;
        target.format = sceneFormat;
        target.blend = blend;
        wgpu::FragmentState fragmentState;
        fragmentState.module = module;
        fragmentState.entryPoint = fragmentEntry;
        fragmentState.targetCount = 1;
        fragmentState.targets = &target;
        const std::array groups{layout};
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = fragmentEntry;
        descriptor.layout = pipelineLayout(m_device, groups);
        descriptor.vertex.module = module;
        descriptor.vertex.entryPoint = vertex;
        descriptor.fragment = &fragmentState;
        return m_device.CreateRenderPipeline(&descriptor);
    };
    m_taaPipeline = fullscreen(m_taaLayout, shaderModule("webgpu_taa"), "taaVertex", "taaFragment", nullptr);
    const wgpu::ShaderModule bloom = shaderModule("webgpu_bloom");
    m_bloomThreshold = fullscreen(m_bloomLayout, bloom, "bloomVertex", "thresholdFragment", nullptr);
    m_bloomDownsample = fullscreen(m_bloomLayout, bloom, "bloomVertex", "downsampleFragment", nullptr);
    // Each level adds itself over the one above.
    wgpu::BlendState add;
    add.color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One, .dstFactor = wgpu::BlendFactor::One};
    add.alpha = add.color;
    m_bloomUpsample = fullscreen(m_bloomLayout, bloom, "bloomVertex", "upsampleFragment", &add);

    wgpu::BufferDescriptor taa;
    taa.label = "Temporal antialiasing";
    taa.size = sizeof(TaaData);
    taa.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    m_taaBuffer = m_device.CreateBuffer(&taa);
    wgpu::BufferDescriptor steps = taa;
    steps.label = "Bloom";
    steps.size = bloomSteps * uniformStride;
    m_bloomBuffer = m_device.CreateBuffer(&steps);
    // The lens: the scene image, the depth, the seven images of its chain, and the sampler.
    std::array<wgpu::BindGroupLayoutEntry, 11> lensEntries{};
    lensEntries[0] = uniformEntry(0, fragment, true, sizeof(LensData));
    lensEntries[1] = textureEntry(1, fragment);
    lensEntries[2] = textureEntry(2, fragment);
    lensEntries[2].texture.sampleType = wgpu::TextureSampleType::Depth;
    for (std::uint32_t image = 0; image < 7; ++image)
    {
        lensEntries[3 + image] = textureEntry(3 + image, fragment);
    }
    lensEntries[10] = samplerEntry(10, fragment);
    m_lensLayout = bindGroupLayout(m_device, "Lens", lensEntries);
    const wgpu::ShaderModule lens = shaderModule("webgpu_lens");
    const std::array<std::pair<const char*, wgpu::TextureFormat>, 8> lensFragments{{{"circleOfConfusionFragment", confusionFormat},
                                                                                   {"prefilterFragment", sceneFormat},
                                                                                   {"bokehFragment", sceneFormat},
                                                                                   {"filterFragment", sceneFormat},
                                                                                   {"combineFragment", sceneFormat},
                                                                                   {"tilesFragment", motionFormat},
                                                                                   {"neighboursFragment", motionFormat},
                                                                                   {"motionFragment", sceneFormat}}};
    for (std::size_t step = 0; step < lensFragments.size(); ++step)
    {
        wgpu::ColorTargetState target;
        target.format = lensFragments[step].second;
        wgpu::FragmentState fragmentState;
        fragmentState.module = lens;
        fragmentState.entryPoint = lensFragments[step].first;
        fragmentState.targetCount = 1;
        fragmentState.targets = &target;
        const std::array groups{m_lensLayout};
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = lensFragments[step].first;
        descriptor.layout = pipelineLayout(m_device, groups);
        descriptor.vertex.module = lens;
        descriptor.vertex.entryPoint = "lensVertex";
        descriptor.fragment = &fragmentState;
        m_lensPipelines[step] = m_device.CreateRenderPipeline(&descriptor);
    }
    wgpu::BufferDescriptor lensSettings = taa;
    lensSettings.label = "Lens";
    lensSettings.size = lensSteps * lensStride;
    m_lensBuffer = m_device.CreateBuffer(&lensSettings);
    // What the tonemapping reads as its halo without bloom.
    wgpu::TextureDescriptor black;
    black.size = {1, 1, 1};
    black.format = sceneFormat;
    black.usage = wgpu::TextureUsage::TextureBinding;
    m_black = m_device.CreateTexture(&black);
    return {};
}

void WebGpuRenderer::ensurePostTargets(math::Extent2D size)
{
    if (m_resolved[0] && m_bloomSizes[0].width == std::max(size.width >> 1, 1U) && m_bloomSizes[0].height == std::max(size.height >> 1, 1U))
    {
        return;
    }
    wgpu::TextureDescriptor descriptor;
    descriptor.format = sceneFormat;
    descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    descriptor.label = "Resolved";
    descriptor.size = {size.width, size.height, 1};
    for (std::size_t index = 0; index < m_resolved.size(); ++index)
    {
        m_resolved[index] = m_device.CreateTexture(&descriptor);
        m_resolvedViews[index] = m_resolved[index].CreateView();
    }
    // A history of another size cannot be followed.
    m_historyValid = false;
    descriptor.label = "Bloom";
    for (std::size_t level = 0; level < m_bloomChain.size(); ++level)
    {
        // Every level halves the one before it, down to a few pixels.
        m_bloomSizes[level] = {std::max(size.width >> (level + 1), 1U), std::max(size.height >> (level + 1), 1U)};
        descriptor.size = {m_bloomSizes[level].width, m_bloomSizes[level].height, 1};
        m_bloomChain[level] = m_device.CreateTexture(&descriptor);
        m_bloomViews[level] = m_bloomChain[level].CreateView();
    }
}

wgpu::TextureView WebGpuRenderer::resolve(const wgpu::CommandEncoder& encoder, const ViewMatrices& matrices)
{
    const bool antialiased = m_world.camera.antialiasing == Antialiasing::Temporal;
    if (!antialiased)
    {
        m_historyValid = false;
        return m_sceneView;
    }
    // An abrupt change of exposure, over a quarter stop, starts afresh: the scene mixes physical light and colours
    // relative to the exposure, which scaling the history would change.
    constexpr float exposureLimit = 1.1892071f;
    const float exposureRatio = m_exposure / m_historyExposure;
    const bool historyValid = m_historyValid && exposureRatio >= 1.0f / exposureLimit && exposureRatio <= exposureLimit;
    const TaaData data{
        .blend = taaBlend,
        .historyValid = historyValid ? 1.0f : 0.0f,
        .texelWidth = 1.0f / static_cast<float>(m_sceneSize.width),
        .texelHeight = 1.0f / static_cast<float>(m_sceneSize.height),
        .inverseViewProjection = matrices.inverseViewProjection,
        .previousViewProjection = m_previousViewProjection,
        .skyInverseViewProjection = math::inverse(matrices.skyViewProjection),
        .previousSkyViewProjection = m_previousSkyViewProjection,
    };
    m_queue.WriteBuffer(m_taaBuffer, 0, &data, sizeof(data));
    const std::size_t into = m_frameIndex % 2;
    const std::array entries{bufferBinding(0, m_taaBuffer, sizeof(TaaData)), viewBinding(1, m_sceneView),
                             viewBinding(2, m_resolvedViews[1 - into]), viewBinding(3, m_depthView),
                             samplerBinding(4, m_clampSampler)};
    const wgpu::BindGroup group = bindGroup(m_device, m_taaLayout, entries);
    wgpu::RenderPassColorAttachment color;
    color.view = m_resolvedViews[into];
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor descriptor;
    descriptor.label = "Temporal antialiasing";
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
    pass.SetPipeline(m_taaPipeline);
    pass.SetBindGroup(0, group);
    pass.Draw(3);
    pass.End();
    ++m_drawCalls;
    // What the next frame follows its pixels back to.
    m_historyValid = true;
    m_historyExposure = m_exposure;
    return m_resolvedViews[into];
}

void WebGpuRenderer::createTerrainPipelines()
{
    const wgpu::ShaderStage vertex = wgpu::ShaderStage::Vertex;
    const wgpu::ShaderStage fragment = wgpu::ShaderStage::Fragment;
    const wgpu::ShaderStage both = vertex | fragment;
    std::array<wgpu::BindGroupLayoutEntry, 13> entries{};
    entries[0] = uniformEntry(0, both, true, sizeof(TerrainUniform));
    // The heights are floats, read at their samples.
    entries[1] = textureEntry(1, vertex);
    entries[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    entries[2] = textureEntry(2, both);
    // The maps are read through one function, which the vertices name too.
    entries[3] = textureEntry(3, both);
    entries[4] = textureEntry(4, both);
    entries[5] = textureEntry(5, both);
    for (std::uint32_t array = 6; array <= 8; ++array)
    {
        entries[array] = textureEntry(array, fragment);
        entries[array].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    }
    entries[9] = storageEntry(9, vertex);
    entries[10] = storageEntry(10, vertex);
    entries[11] = samplerEntry(11, both);
    entries[12] = samplerEntry(12, fragment);
    m_terrainLayout = bindGroupLayout(m_device, "Terrain", entries);
    const std::array drawEntries{uniformEntry(0, vertex, true, sizeof(TerrainDrawData)), uniformEntry(1, vertex, true, sizeof(math::Mat4))};
    m_terrainDrawLayout = bindGroupLayout(m_device, "Terrain draw", drawEntries);
    m_emptyLayout = bindGroupLayout(m_device, "Empty", std::span<const wgpu::BindGroupLayoutEntry>{});
    m_emptyGroup = bindGroup(m_device, m_emptyLayout, std::span<const wgpu::BindGroupEntry>{});

    const wgpu::ShaderModule terrain = shaderModule("webgpu_terrain");
    const std::array sceneGroups{m_sceneLayout, m_terrainLayout, m_terrainDrawLayout};
    const wgpu::PipelineLayout sceneLayout = pipelineLayout(m_device, sceneGroups);
    for (const auto& [vertexEntry, fragmentEntry, pipeline] :
         {std::tuple{"sceneVertex", "sceneFragment", &m_terrainPipeline}, std::tuple{"grassVertex", "grassFragment", &m_grassPipeline}})
    {
        wgpu::ColorTargetState target;
        target.format = sceneFormat;
        wgpu::FragmentState fragmentState;
        fragmentState.module = terrain;
        fragmentState.entryPoint = fragmentEntry;
        fragmentState.targetCount = 1;
        fragmentState.targets = &target;
        // Reversed depth: nearer is greater.
        wgpu::DepthStencilState depth;
        depth.format = depthFormat;
        depth.depthWriteEnabled = true;
        depth.depthCompare = wgpu::CompareFunction::Greater;
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = fragmentEntry;
        descriptor.layout = sceneLayout;
        descriptor.vertex.module = terrain;
        descriptor.vertex.entryPoint = vertexEntry;
        descriptor.primitive.cullMode = wgpu::CullMode::None;
        descriptor.depthStencil = &depth;
        descriptor.fragment = &fragmentState;
        *pipeline = m_device.CreateRenderPipeline(&descriptor);
    }
    // The shadows bind nothing of the scene, whose maps they draw.
    const std::array shadowGroups{m_emptyLayout, m_terrainLayout, m_terrainDrawLayout};
    const wgpu::PipelineLayout shadowLayout = pipelineLayout(m_device, shadowGroups);
    for (std::size_t atlas = 0; atlas < m_terrainShadowPipelines.size(); ++atlas)
    {
        wgpu::FragmentState fragmentState;
        fragmentState.module = terrain;
        fragmentState.entryPoint = "shadowFragment";
        wgpu::DepthStencilState depth;
        depth.format = depthFormat;
        depth.depthWriteEnabled = true;
        depth.depthCompare = wgpu::CompareFunction::LessEqual;
        depth.depthBiasSlopeScale = atlas != 0 ? 2.0f : 1.5f;
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = "Terrain shadow";
        descriptor.layout = shadowLayout;
        descriptor.vertex.module = terrain;
        descriptor.vertex.entryPoint = "shadowVertex";
        descriptor.primitive.cullMode = wgpu::CullMode::None;
        descriptor.primitive.unclippedDepth = m_unclippedDepth;
        descriptor.depthStencil = &depth;
        descriptor.fragment = &fragmentState;
        m_terrainShadowPipelines[atlas] = m_device.CreateRenderPipeline(&descriptor);
    }

    // A block is a grid of terrainBlockCells cells on a side, its four quarters one after the other, as Vulkan has it,
    // so that a block can draw a quarter of itself where its smaller blocks are not drawn; and a blade of grass, two
    // vertices at each of three heights then its tip, or far away one triangle from its foot to its tip.
    constexpr std::uint32_t cells = terrainBlockCells;
    constexpr std::uint32_t half = cells / 2;
    std::vector<std::uint32_t> indices;
    for (std::uint32_t quarter = 0; quarter < 4; ++quarter)
    {
        const std::uint32_t firstX = (quarter & 1U) * half;
        const std::uint32_t firstZ = (quarter >> 1U) * half;
        for (std::uint32_t z = firstZ; z < firstZ + half; ++z)
        {
            for (std::uint32_t x = firstX; x < firstX + half; ++x)
            {
                const std::uint32_t corner = z * (cells + 1) + x;
                const std::uint32_t right = corner + 1;
                const std::uint32_t below = corner + cells + 1;
                indices.insert(indices.end(), {corner, below, right, right, below, below + 1});
            }
        }
    }
    constexpr std::array<std::uint32_t, 18> blade{0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5, 4, 5, 6, 0, 1, 6};
    for (const auto& [buffer, data] : {std::pair{&m_terrainGrid, std::span<const std::uint32_t>(indices)},
                                       std::pair{&m_grassBlade, std::span<const std::uint32_t>(blade)}})
    {
        wgpu::BufferDescriptor descriptor;
        descriptor.label = "Terrain";
        descriptor.size = data.size_bytes();
        descriptor.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        *buffer = m_device.CreateBuffer(&descriptor);
        m_queue.WriteBuffer(*buffer, 0, data.data(), data.size_bytes());
    }
}

void WebGpuRenderer::fillTerrainLayers(TerrainLayers& layers, const RenderTerrain& terrain)
{
    const std::uint32_t count = std::max(std::min<std::uint32_t>(terrain.layerCount, 8), 1U);
    // The textures of each layer, white or flat where its material has none.
    std::vector<std::uint64_t> key{m_textureRevision, count};
    std::vector<std::array<wgpu::Texture, 3>> sources;
    const auto keyOf = [](TextureHandle handle) { return (std::uint64_t{handle.index} << 32) | handle.generation; };
    for (std::uint32_t layer = 0; layer < count; ++layer)
    {
        const GpuMaterial* const material = layer < terrain.layerCount ? m_materials.find(terrain.layers[layer]) : nullptr;
        const auto textureOf = [&](TextureHandle handle, const wgpu::Texture& fallback) {
            const GpuTexture* const texture = material != nullptr ? m_textures.find(handle) : nullptr;
            key.push_back(texture != nullptr && texture->bindable ? keyOf(handle) : 0);
            return texture != nullptr && texture->bindable ? texture->texture : fallback;
        };
        sources.push_back({textureOf(material != nullptr ? material->description.baseColorTexture : TextureHandle{}, m_white),
                           textureOf(material != nullptr ? material->description.metallicRoughnessTexture : TextureHandle{}, m_white),
                           textureOf(material != nullptr ? material->description.normalTexture : TextureHandle{}, m_flatNormal)});
    }
    if (layers.colors && key == layers.key)
    {
        return;
    }
    layers.key = std::move(key);
    const std::array<std::pair<wgpu::Texture*, wgpu::TextureFormat>, 3> arrays{
        {{&layers.colors, decalColorFormat}, {&layers.surfaces, decalDataFormat}, {&layers.normals, decalDataFormat}}};
    for (const auto& [array, format] : arrays)
    {
        wgpu::TextureDescriptor descriptor;
        descriptor.label = "Terrain layers";
        descriptor.size = {terrainLayerSize, terrainLayerSize, count};
        descriptor.format = format;
        descriptor.mipLevelCount = terrainLayerMips;
        descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        *array = m_device.CreateTexture(&descriptor);
    }
    // Every level of every layer of every array, drawn from its texture at the mip that fits it.
    const std::size_t steps = std::size_t{count} * 3 * terrainLayerMips;
    wgpu::BufferDescriptor buffer;
    buffer.label = "Terrain layers";
    buffer.size = steps * uniformStride;
    buffer.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    const wgpu::Buffer settings = m_device.CreateBuffer(&buffer);
    std::vector<std::byte> data(steps * uniformStride);
    const wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    std::size_t step = 0;
    for (std::size_t array = 0; array < arrays.size(); ++array)
    {
        for (std::uint32_t layer = 0; layer < count; ++layer)
        {
            for (std::uint32_t mip = 0; mip < terrainLayerMips; ++mip, ++step)
            {
                const auto side = static_cast<float>(std::max(terrainLayerSize >> mip, 1U));
                const BlitData blit{.rect = math::Vec4(0.0f, 0.0f, 1.0f, 1.0f), .targetWidth = side, .targetHeight = side};
                std::memcpy(data.data() + step * uniformStride, &blit, sizeof(blit));
                wgpu::TextureViewDescriptor level;
                level.dimension = wgpu::TextureViewDimension::e2D;
                level.baseArrayLayer = layer;
                level.arrayLayerCount = 1;
                level.baseMipLevel = mip;
                level.mipLevelCount = 1;
                wgpu::RenderPassColorAttachment color;
                color.view = arrays[array].first->CreateView(&level);
                color.loadOp = wgpu::LoadOp::Clear;
                color.storeOp = wgpu::StoreOp::Store;
                wgpu::RenderPassDescriptor descriptor;
                descriptor.label = "Terrain layers";
                descriptor.colorAttachmentCount = 1;
                descriptor.colorAttachments = &color;
                const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
                pass.SetPipeline(array == 0 ? m_blitColors : m_blitData);
                // Read as the layers are, repeating, so that the edges of a tile blend into the next one.
                const std::array entries{bufferBinding(0, settings, sizeof(BlitData)), viewBinding(1, sources[layer][array].CreateView()),
                                         samplerBinding(2, m_linearSampler)};
                const auto offset = static_cast<std::uint32_t>(step * uniformStride);
                pass.SetBindGroup(0, bindGroup(m_device, m_blitLayout, entries), 1, &offset);
                pass.Draw(6);
                pass.End();
            }
        }
    }
    m_queue.WriteBuffer(settings, 0, data.data(), data.size());
    const wgpu::CommandBuffer commands = encoder.Finish();
    m_queue.Submit(1, &commands);
}

void WebGpuRenderer::prepareTerrains(const ViewMatrices& matrices)
{
    m_terrainDrawings.clear();
    if (m_world.terrains.empty())
    {
        return;
    }
    // The blocks around the camera, kept as Vulkan keeps them.
    const float aspectRatio = static_cast<float>(m_sceneSize.width) / static_cast<float>(std::max(m_sceneSize.height, 1U));
    const Frustum frustum = frustumOf(projectionMatrix(m_world.camera, aspectRatio, math::Mat4{1.0f}) * m_world.camera.view);
    const math::Vec3 eye = matrices.cameraPosition;
    m_terrainLayers.resize(m_world.terrains.size());
    std::vector<GpuTerrainNode> nodes;
    std::vector<GpuTerrainNode> tiles;
    std::vector<std::byte> uniforms;
    // The first draw is that of the blocks; three of grass follow for each terrain.
    std::vector<TerrainDrawData> draws{TerrainDrawData{.cameraPosition = eye, .time = m_time}};
    std::vector<std::array<const GpuTexture*, 5>> maps;
    std::vector<std::size_t> layerIndices;
    for (std::size_t index = 0; index < m_world.terrains.size(); ++index)
    {
        const RenderTerrain& terrain = m_world.terrains[index];
        const TerrainTextures& textures = terrain.textures;
        const std::array<const GpuTexture*, 5> found{m_textures.find(textures.heights), m_textures.find(textures.slopes),
                                                     m_textures.find(textures.paint0), m_textures.find(textures.paint1),
                                                     m_textures.find(textures.detail)};
        if (std::ranges::any_of(found, [](const GpuTexture* texture) { return texture == nullptr; }))
        {
            continue;
        }
        const std::optional<TerrainSelection> selection = selectTerrain(terrain, eye, frustum);
        if (!selection)
        {
            continue;
        }
        fillTerrainLayers(m_terrainLayers[index], terrain);
        TerrainDrawing drawing{.castShadows = terrain.castShadows};
        for (std::size_t group = 0; group < selection->nodes.size(); ++group)
        {
            drawing.firstNode[group] = static_cast<std::uint32_t>(nodes.size());
            drawing.nodeCount[group] = static_cast<std::uint32_t>(selection->nodes[group].size());
            nodes.insert(nodes.end(), selection->nodes[group].begin(), selection->nodes[group].end());
        }
        drawing.firstDraw = static_cast<std::uint32_t>(draws.size());
        for (std::size_t band = 0; band < selection->tiles.size(); ++band)
        {
            draws.push_back({.cameraPosition = eye,
                             .firstTile = static_cast<std::uint32_t>(tiles.size()),
                             .bladesPerTile = std::max(selection->blades[band], 1U),
                             .bladeWidthScale = selection->bladeWidth[band],
                             .time = m_time});
            drawing.tileCount[band] = static_cast<std::uint32_t>(selection->tiles[band].size());
            drawing.blades[band] = selection->blades[band];
            tiles.insert(tiles.end(), selection->tiles[band].begin(), selection->tiles[band].end());
        }
        TerrainUniform uniform{
            .origin = terrain.origin,
            .size = terrain.size,
            .resolution = terrain.resolution,
            .layerCount = std::min<std::uint32_t>(terrain.layerCount, 8),
            .textureSize = terrain.textureSize,
            .blendSharpness = terrain.blendSharpness,
            .triplanar = terrain.triplanar ? 1U : 0U,
            .autoBaseLayer = terrain.autoBaseLayer,
            .autoSlopeLayer = terrain.autoSlopeLayer,
            .autoSlope = terrain.autoSlope,
            .autoHeightLayer = terrain.autoHeightLayer,
            .autoHeight = terrain.autoHeight,
            .autoBlend = terrain.autoBlend,
            .windStrength = terrain.windStrength,
            .windSpeed = terrain.windSpeed,
            .grassDistance = terrain.grassDistance,
            .leafRange = selection->leafRange,
            .grassColor = terrain.grassColor,
            .grassHeight = terrain.grassHeight,
            .grassTipColor = terrain.grassTipColor,
            .grassWidth = terrain.grassWidth,
        };
        for (std::uint32_t layer = 0; layer < uniform.layerCount; ++layer)
        {
            const GpuMaterial* const material = m_materials.find(terrain.layers[layer]);
            uniform.baseColorFactors[layer] = material != nullptr ? material->description.baseColorFactor : math::Vec4{1.0f};
            uniform.layerFactors[layer] = material != nullptr ? math::Vec4(material->description.metallicFactor,
                                                                           material->description.roughnessFactor,
                                                                           material->description.normalScale, 0.0f)
                                                              : math::Vec4(0.0f, 1.0f, 1.0f, 0.0f);
        }
        uniforms.resize((m_terrainDrawings.size() + 1) * terrainStride);
        std::memcpy(uniforms.data() + m_terrainDrawings.size() * terrainStride, &uniform, sizeof(uniform));
        maps.push_back(found);
        layerIndices.push_back(index);
        m_terrainDrawings.push_back(std::move(drawing));
    }
    if (m_terrainDrawings.empty())
    {
        return;
    }
    growBuffer(m_terrainBuffer, m_terrainCapacity, uniforms.size(), wgpu::BufferUsage::Uniform, "Terrains");
    m_queue.WriteBuffer(m_terrainBuffer, 0, uniforms.data(), uniforms.size());
    std::vector<std::byte> drawBytes(draws.size() * uniformStride);
    for (std::size_t draw = 0; draw < draws.size(); ++draw)
    {
        std::memcpy(drawBytes.data() + draw * uniformStride, &draws[draw], sizeof(TerrainDrawData));
    }
    growBuffer(m_terrainDrawBuffer, m_terrainDrawCapacity, drawBytes.size(), wgpu::BufferUsage::Uniform, "Terrain draws");
    m_queue.WriteBuffer(m_terrainDrawBuffer, 0, drawBytes.data(), drawBytes.size());
    const std::uint64_t nodeBytes = std::max<std::size_t>(nodes.size(), 1) * sizeof(GpuTerrainNode);
    const std::uint64_t tileBytes = std::max<std::size_t>(tiles.size(), 1) * sizeof(GpuTerrainNode);
    growBuffer(m_terrainNodeBuffer, m_terrainNodeCapacity, nodeBytes, wgpu::BufferUsage::Storage, "Terrain blocks");
    growBuffer(m_grassTileBuffer, m_grassTileCapacity, tileBytes, wgpu::BufferUsage::Storage, "Grass tiles");
    if (!nodes.empty())
    {
        m_queue.WriteBuffer(m_terrainNodeBuffer, 0, nodes.data(), nodes.size() * sizeof(GpuTerrainNode));
    }
    if (!tiles.empty())
    {
        m_queue.WriteBuffer(m_grassTileBuffer, 0, tiles.data(), tiles.size() * sizeof(GpuTerrainNode));
    }
    wgpu::TextureViewDescriptor arrayView;
    arrayView.dimension = wgpu::TextureViewDimension::e2DArray;
    for (std::size_t drawn = 0; drawn < m_terrainDrawings.size(); ++drawn)
    {
        const std::array<const GpuTexture*, 5>& map = maps[drawn];
        const TerrainLayers& layers = m_terrainLayers[layerIndices[drawn]];
        std::array<wgpu::BindGroupEntry, 13> entries{};
        entries[0] = bufferBinding(0, m_terrainBuffer, sizeof(TerrainUniform));
        for (std::uint32_t texture = 0; texture < 5; ++texture)
        {
            entries[1 + texture] = viewBinding(1 + texture, map[texture]->texture.CreateView());
        }
        entries[6] = viewBinding(6, layers.colors.CreateView(&arrayView));
        entries[7] = viewBinding(7, layers.surfaces.CreateView(&arrayView));
        entries[8] = viewBinding(8, layers.normals.CreateView(&arrayView));
        entries[9] = bufferBinding(9, m_terrainNodeBuffer, nodeBytes);
        entries[10] = bufferBinding(10, m_grassTileBuffer, tileBytes);
        entries[11] = samplerBinding(11, m_clampSampler);
        entries[12] = samplerBinding(12, m_linearSampler);
        m_terrainDrawings[drawn].group = bindGroup(m_device, m_terrainLayout, entries);
    }
    const std::array drawEntries{bufferBinding(0, m_terrainDrawBuffer, sizeof(TerrainDrawData)),
                                 bufferBinding(1, m_shadowMatrixBuffer, sizeof(math::Mat4))};
    m_terrainDrawGroup = bindGroup(m_device, m_terrainDrawLayout, drawEntries);
}

void WebGpuRenderer::drawTerrains(const wgpu::RenderPassEncoder& pass, std::optional<std::uint32_t> shadowView, bool atlas)
{
    if (m_terrainDrawings.empty())
    {
        return;
    }
    pass.SetPipeline(shadowView ? m_terrainShadowPipelines[atlas ? 1 : 0] : m_terrainPipeline);
    pass.SetBindGroup(0, shadowView ? m_emptyGroup : m_sceneGroup);
    const std::array<std::uint32_t, 2> drawOffsets{0, static_cast<std::uint32_t>(std::uint64_t{shadowView.value_or(0)} * uniformStride)};
    pass.SetBindGroup(2, m_terrainDrawGroup, drawOffsets.size(), drawOffsets.data());
    pass.SetIndexBuffer(m_terrainGrid, wgpu::IndexFormat::Uint32);
    constexpr std::uint32_t quarterIndices = terrainBlockCells * terrainBlockCells / 4 * 6;
    for (std::size_t drawn = 0; drawn < m_terrainDrawings.size(); ++drawn)
    {
        const TerrainDrawing& drawing = m_terrainDrawings[drawn];
        if (shadowView && !drawing.castShadows)
        {
            continue;
        }
        const auto offset = static_cast<std::uint32_t>(drawn * terrainStride);
        pass.SetBindGroup(1, drawing.group, 1, &offset);
        for (std::size_t group = 0; group < drawing.nodeCount.size(); ++group)
        {
            if (drawing.nodeCount[group] == 0)
            {
                continue;
            }
            // A whole block, or one of its quarters; its blocks are its instances.
            const std::uint32_t indexCount = group == 0 ? quarterIndices * 4 : quarterIndices;
            const std::uint32_t firstIndex = group == 0 ? 0 : static_cast<std::uint32_t>(group - 1) * quarterIndices;
            pass.DrawIndexed(indexCount, drawing.nodeCount[group], firstIndex, 0, drawing.firstNode[group]);
            ++m_drawCalls;
        }
    }
}

void WebGpuRenderer::drawGrass(const wgpu::RenderPassEncoder& pass)
{
    if (m_terrainDrawings.empty())
    {
        return;
    }
    pass.SetPipeline(m_grassPipeline);
    pass.SetBindGroup(0, m_sceneGroup);
    pass.SetIndexBuffer(m_grassBlade, wgpu::IndexFormat::Uint32);
    for (std::size_t drawn = 0; drawn < m_terrainDrawings.size(); ++drawn)
    {
        const TerrainDrawing& drawing = m_terrainDrawings[drawn];
        const auto offset = static_cast<std::uint32_t>(drawn * terrainStride);
        pass.SetBindGroup(1, drawing.group, 1, &offset);
        for (std::size_t band = 0; band < drawing.tileCount.size(); ++band)
        {
            if (drawing.tileCount[band] == 0)
            {
                continue;
            }
            const std::array<std::uint32_t, 2> drawOffsets{static_cast<std::uint32_t>(std::uint64_t{drawing.firstDraw + band} * uniformStride), 0};
            pass.SetBindGroup(2, m_terrainDrawGroup, drawOffsets.size(), drawOffsets.data());
            // Far away, one triangle a blade.
            const bool far = band + 1 == drawing.tileCount.size();
            pass.DrawIndexed(far ? 3 : 15, drawing.tileCount[band] * drawing.blades[band], far ? 15 : 0);
            ++m_drawCalls;
        }
    }
}

void WebGpuRenderer::fillDecalAtlas(DecalAtlas& atlas, std::vector<TextureHandle> textures, wgpu::TextureFormat format,
                                    const wgpu::RenderPipeline& blit)
{
    const auto keyOf = [](TextureHandle handle) { return (std::uint64_t{handle.index} << 32) | handle.generation; };
    std::ranges::sort(textures, {}, keyOf);
    textures.erase(std::ranges::unique(textures, {}, keyOf).begin(), textures.end());
    std::vector<std::uint64_t> key{m_textureRevision};
    for (const TextureHandle texture : textures)
    {
        key.push_back(keyOf(texture));
    }
    if (key == atlas.key)
    {
        return;
    }
    atlas.key = std::move(key);
    atlas.rects.clear();
    if (textures.empty())
    {
        return;
    }
    if (!atlas.texture)
    {
        wgpu::TextureDescriptor descriptor;
        descriptor.label = "Decal atlas";
        descriptor.size = {decalAtlasSize, decalAtlasSize, 1};
        descriptor.format = format;
        descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        atlas.texture = m_device.CreateTexture(&descriptor);
        atlas.view = atlas.texture.CreateView();
    }
    // On shelves, the tallest first, each texture at its size but no larger than half the atlas, all halved together
    // until they fit; a texel of room around each keeps its neighbours from bleeding in.
    std::vector<math::Extent2D> sizes;
    for (const TextureHandle texture : textures)
    {
        const GpuTexture* const found = m_textures.find(texture);
        sizes.push_back(found != nullptr ? found->size : math::Extent2D{1, 1});
    }
    std::vector<std::size_t> order(textures.size());
    for (std::size_t index = 0; index < order.size(); ++index)
    {
        order[index] = index;
    }
    std::ranges::sort(order, std::greater{}, [&](std::size_t index) { return sizes[index].height; });
    std::vector<math::Vec4> placed(textures.size());
    for (float scale = 1.0f; scale >= 1.0f / 64.0f; scale *= 0.5f)
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t shelf = 0;
        bool fits = true;
        for (const std::size_t index : order)
        {
            const math::Extent2D size = sizes[index];
            const float largest = static_cast<float>(std::max(size.width, size.height));
            const float shrink = std::min(1.0f, static_cast<float>(decalAtlasSize / 2) / largest) * scale;
            const auto width = std::max(static_cast<std::uint32_t>(std::lround(static_cast<float>(size.width) * shrink)), 1U);
            const auto height = std::max(static_cast<std::uint32_t>(std::lround(static_cast<float>(size.height) * shrink)), 1U);
            if (x + width + 2 > decalAtlasSize)
            {
                x = 0;
                y += shelf;
                shelf = 0;
            }
            if (y + height + 2 > decalAtlasSize)
            {
                fits = false;
                break;
            }
            const float atlasSize = static_cast<float>(decalAtlasSize);
            placed[index] = math::Vec4(static_cast<float>(x + 1) / atlasSize, static_cast<float>(y + 1) / atlasSize,
                                       static_cast<float>(width) / atlasSize, static_cast<float>(height) / atlasSize);
            x += width + 2;
            shelf = std::max(shelf, height + 2);
        }
        if (fits)
        {
            break;
        }
    }
    wgpu::BufferDescriptor buffer;
    buffer.label = "Decal atlas";
    buffer.size = textures.size() * uniformStride;
    buffer.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    const wgpu::Buffer settings = m_device.CreateBuffer(&buffer);
    std::vector<std::byte> steps(textures.size() * uniformStride);
    const wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::RenderPassColorAttachment color;
    color.view = atlas.view;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor descriptor;
    descriptor.label = "Decal atlas";
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
    pass.SetPipeline(blit);
    for (std::size_t index = 0; index < textures.size(); ++index)
    {
        const GpuTexture* const found = m_textures.find(textures[index]);
        if (found == nullptr || !found->bindable || placed[index].z <= 0.0f)
        {
            continue;
        }
        const BlitData data{.rect = placed[index], .targetWidth = static_cast<float>(decalAtlasSize), .targetHeight = static_cast<float>(decalAtlasSize)};
        std::memcpy(steps.data() + index * uniformStride, &data, sizeof(data));
        const std::array entries{bufferBinding(0, settings, sizeof(BlitData)), viewBinding(1, found->texture.CreateView()),
                                 samplerBinding(2, m_clampSampler)};
        const auto offset = static_cast<std::uint32_t>(index * uniformStride);
        pass.SetBindGroup(0, bindGroup(m_device, m_blitLayout, entries), 1, &offset);
        pass.Draw(6);
        atlas.rects.emplace_back(textures[index], placed[index]);
    }
    pass.End();
    m_queue.WriteBuffer(settings, 0, steps.data(), steps.size());
    const wgpu::CommandBuffer commands = encoder.Finish();
    m_queue.Submit(1, &commands);
}

void WebGpuRenderer::prepareDecals()
{
    const auto usable = [this](TextureHandle texture) {
        const GpuTexture* const found = m_textures.find(texture);
        return found != nullptr && found->bindable;
    };
    std::vector<TextureHandle> colors;
    std::vector<TextureHandle> data;
    for (const RenderDecal& decal : m_world.decals)
    {
        for (const TextureHandle texture : {decal.albedo, decal.emission})
        {
            if (usable(texture))
            {
                colors.push_back(texture);
            }
        }
        for (const TextureHandle texture : {decal.normal, decal.orm})
        {
            if (usable(texture))
            {
                data.push_back(texture);
            }
        }
    }
    fillDecalAtlas(m_decalColors, std::move(colors), decalColorFormat, m_blitColors);
    fillDecalAtlas(m_decalData, std::move(data), decalDataFormat, m_blitData);
    // Where a texture lies in its atlas; nothing for one it does not hold.
    const auto rectOf = [&usable](const DecalAtlas& atlas, TextureHandle texture) {
        if (!usable(texture))
        {
            return math::Vec4{0.0f};
        }
        const auto found = std::ranges::find_if(atlas.rects, [&](const auto& placed) { return placed.first == texture; });
        return found != atlas.rects.end() ? found->second : math::Vec4{0.0f};
    };
    std::vector<AtlasDecalData> packed;
    for (const RenderDecal& decal : m_world.decals)
    {
        const std::optional<DecalBox> box = decalBox(decal);
        if (!box)
        {
            continue;
        }
        packed.push_back({.worldToDecal = box->worldToDecal,
                          .axisX = box->axisX,
                          .axisZ = box->axisZ,
                          .axisY = box->axisY,
                          .modulate = decal.modulate,
                          .albedoMix = decal.albedoMix,
                          .emissionEnergy = decal.emissionEnergy,
                          .normalFade = decal.normalFade,
                          .upperFade = decal.upperFade,
                          .lowerFade = decal.lowerFade,
                          .fadeBegin = decal.fadeBegin,
                          .fadeLength = decal.fadeLength,
                          .rects = {rectOf(m_decalColors, decal.albedo), rectOf(m_decalData, decal.normal), rectOf(m_decalData, decal.orm),
                                    rectOf(m_decalColors, decal.emission)}});
    }
    m_decalCount = static_cast<std::uint32_t>(packed.size());
    const std::uint64_t bytes = std::max<std::size_t>(packed.size(), 1) * sizeof(AtlasDecalData);
    if (!m_decalBuffer || m_decalCapacity < bytes)
    {
        wgpu::BufferDescriptor descriptor;
        descriptor.label = "Decals";
        descriptor.size = bytes;
        descriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        m_decalBuffer = m_device.CreateBuffer(&descriptor);
        m_decalCapacity = bytes;
    }
    if (!packed.empty())
    {
        m_queue.WriteBuffer(m_decalBuffer, 0, packed.data(), packed.size() * sizeof(AtlasDecalData));
    }
}

void WebGpuRenderer::ensureLensTargets(math::Extent2D size)
{
    if (m_lensOutput && m_lensSize == size)
    {
        return;
    }
    m_lensSize = size;
    // A tile is as wide as the longest blur: a fifty-sixth of the height of the image.
    m_lensTileSize = static_cast<std::uint32_t>(std::max(8.0f, std::round(static_cast<float>(size.height) / 56.0f)));
    const math::Extent2D half{std::max((size.width + 1) / 2, 1U), std::max((size.height + 1) / 2, 1U)};
    const math::Extent2D tiles{(size.width + m_lensTileSize - 1) / m_lensTileSize, (size.height + m_lensTileSize - 1) / m_lensTileSize};
    const std::array<std::pair<math::Extent2D, wgpu::TextureFormat>, 7> images{
        {{size, confusionFormat}, {half, sceneFormat}, {half, sceneFormat}, {half, sceneFormat}, {size, sceneFormat}, {tiles, motionFormat}, {tiles, motionFormat}}};
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "Lens";
    descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    for (std::size_t image = 0; image < images.size(); ++image)
    {
        descriptor.size = {images[image].first.width, images[image].first.height, 1};
        descriptor.format = images[image].second;
        m_lensChain[image] = m_device.CreateTexture(&descriptor);
        m_lensViews[image] = m_lensChain[image].CreateView();
    }
    descriptor.size = {size.width, size.height, 1};
    descriptor.format = sceneFormat;
    m_lensOutput = m_device.CreateTexture(&descriptor);
    m_lensOutputView = m_lensOutput.CreateView();
}

wgpu::TextureView WebGpuRenderer::drawLens(const wgpu::CommandEncoder& encoder, const ViewMatrices& matrices, const wgpu::TextureView& source)
{
    const RenderCamera& camera = m_world.camera;
    const bool focusing = camera.depthOfField > 0.0f && camera.aperture > 0.0f && camera.projection == Projection::Perspective;
    const bool blurring = camera.motionBlur > 0.0f;
    if (!focusing && !blurring)
    {
        return source;
    }
    ensureLensTargets(m_sceneSize);
    const math::Extent2D full = m_sceneSize;
    const math::Extent2D half{std::max((full.width + 1) / 2, 1U), std::max((full.height + 1) / 2, 1U)};
    const math::Extent2D tiles{(full.width + m_lensTileSize - 1) / m_lensTileSize, (full.height + m_lensTileSize - 1) / m_lensTileSize};
    // A full-frame sensor, 24 mm high, behind a lens whose focal length gives the field of view, as Vulkan has it.
    const float focalLength = 0.012f / std::tan(std::max(camera.verticalFov, 1e-3f) * 0.5f);
    const LensData base{
        .imageWidth = static_cast<float>(full.width),
        .imageHeight = static_cast<float>(full.height),
        .aperture = focalLength / std::max(camera.aperture, 0.5f),
        .focalLength = focalLength,
        .focusDistance = std::max(camera.focusDistance, 0.01f),
        .amount = std::clamp(camera.depthOfField, 0.0f, 1.0f),
        .maxRadius = static_cast<float>(full.height) / 60.0f,
        .autoFocus = camera.autoFocus ? 1U : 0U,
        .shutter = std::clamp(camera.motionBlur, 0.0f, 1.0f),
        .tileSize = static_cast<float>(m_lensTileSize),
        .fromDepthOfField = focusing ? 1U : 0U,
        .frame = static_cast<std::uint32_t>(m_frameIndex),
        .inverseViewProjection = matrices.inverseViewProjection,
        .previousViewProjection = m_previousViewProjection,
        .view = camera.view,
        .skyInverseViewProjection = math::inverse(matrices.skyViewProjection),
        .previousSkyViewProjection = m_previousSkyViewProjection,
    };
    std::vector<std::byte> steps(lensSteps * lensStride);
    std::uint32_t step = 0;
    const wgpu::TextureView blank = m_black.CreateView();
    // A step into one image, every image of the chain bound but the one it writes.
    const auto pass = [&](std::size_t pipeline, const wgpu::TextureView& into, math::Extent2D size, std::optional<std::size_t> written) {
        LensData data = base;
        data.targetWidth = static_cast<float>(size.width);
        data.targetHeight = static_cast<float>(size.height);
        std::memcpy(steps.data() + std::size_t{step} * lensStride, &data, sizeof(data));
        std::array<wgpu::BindGroupEntry, 11> entries{};
        entries[0] = bufferBinding(0, m_lensBuffer, sizeof(LensData));
        entries[1] = viewBinding(1, source);
        entries[2] = viewBinding(2, m_depthView);
        for (std::size_t image = 0; image < m_lensViews.size(); ++image)
        {
            entries[3 + image] = viewBinding(static_cast<std::uint32_t>(3 + image), written == image ? blank : m_lensViews[image]);
        }
        entries[10] = samplerBinding(10, m_clampSampler);
        const wgpu::BindGroup group = bindGroup(m_device, m_lensLayout, entries);
        wgpu::RenderPassColorAttachment color;
        color.view = into;
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor descriptor;
        descriptor.label = "Lens";
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        const wgpu::RenderPassEncoder encoded = encoder.BeginRenderPass(&descriptor);
        encoded.SetPipeline(m_lensPipelines[pipeline]);
        const auto offset = static_cast<std::uint32_t>(std::uint64_t{step} * lensStride);
        encoded.SetBindGroup(0, group, 1, &offset);
        encoded.Draw(3);
        encoded.End();
        ++m_drawCalls;
        ++step;
    };
    if (focusing)
    {
        pass(0, m_lensViews[lensCircleOfConfusion], full, lensCircleOfConfusion);
        pass(1, m_lensViews[lensHalved], half, lensHalved);
        pass(2, m_lensViews[lensBokeh], half, lensBokeh);
        pass(3, m_lensViews[lensFiltered], half, lensFiltered);
        pass(4, blurring ? m_lensViews[lensFocused] : m_lensOutputView, full,
             blurring ? std::optional<std::size_t>(lensFocused) : std::nullopt);
    }
    if (blurring)
    {
        pass(5, m_lensViews[lensTiles], tiles, lensTiles);
        pass(6, m_lensViews[lensNeighbours], tiles, lensNeighbours);
        pass(7, m_lensOutputView, full, std::nullopt);
    }
    m_queue.WriteBuffer(m_lensBuffer, 0, steps.data(), steps.size());
    return m_lensOutputView;
}

wgpu::TextureView WebGpuRenderer::drawBloom(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& finished)
{
    if (m_world.camera.bloom <= 0.0f)
    {
        return nullptr;
    }
    // The settings of every step at offsets of their own: the threshold, then each halving, then each addition.
    std::vector<std::byte> steps(bloomSteps * uniformStride);
    const auto setStep = [&steps](std::uint32_t step, math::Extent2D read, float strength) {
        const BloomData data{.texelWidth = 1.0f / static_cast<float>(read.width),
                             .texelHeight = 1.0f / static_cast<float>(read.height),
                             .strength = strength};
        std::memcpy(steps.data() + std::size_t{step} * uniformStride, &data, sizeof(data));
    };
    setStep(0, m_sceneSize, m_world.camera.bloomThreshold);
    for (std::uint32_t level = 1; level < m_bloomChain.size(); ++level)
    {
        setStep(level, m_bloomSizes[level - 1], 1.0f);
    }
    for (std::uint32_t level = static_cast<std::uint32_t>(m_bloomChain.size()) - 1; level > 0; --level)
    {
        setStep(static_cast<std::uint32_t>(m_bloomChain.size()) + (static_cast<std::uint32_t>(m_bloomChain.size()) - 1 - level),
                m_bloomSizes[level], 1.0f);
    }
    m_queue.WriteBuffer(m_bloomBuffer, 0, steps.data(), steps.size());
    const auto pass = [&](const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& read, const wgpu::TextureView& into,
                          std::uint32_t step, bool keep) {
        const std::array entries{bufferBinding(0, m_bloomBuffer, sizeof(BloomData)), viewBinding(1, read),
                                 samplerBinding(2, m_clampSampler)};
        const wgpu::BindGroup group = bindGroup(m_device, m_bloomLayout, entries);
        wgpu::RenderPassColorAttachment color;
        color.view = into;
        color.loadOp = keep ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor descriptor;
        descriptor.label = "Bloom";
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        const wgpu::RenderPassEncoder encoded = encoder.BeginRenderPass(&descriptor);
        encoded.SetPipeline(pipeline);
        const auto offset = static_cast<std::uint32_t>(std::uint64_t{step} * uniformStride);
        encoded.SetBindGroup(0, group, 1, &offset);
        encoded.Draw(3);
        encoded.End();
        ++m_drawCalls;
    };
    // The first step reads the finished image; each halving reads the level above; the last steps add themselves back.
    pass(m_bloomThreshold, finished, m_bloomViews[0], 0, false);
    for (std::uint32_t level = 1; level < m_bloomChain.size(); ++level)
    {
        pass(m_bloomDownsample, m_bloomViews[level - 1], m_bloomViews[level], level, false);
    }
    for (std::uint32_t level = static_cast<std::uint32_t>(m_bloomChain.size()) - 1; level > 0; --level)
    {
        pass(m_bloomUpsample, m_bloomViews[level], m_bloomViews[level - 1],
             static_cast<std::uint32_t>(m_bloomChain.size()) + (static_cast<std::uint32_t>(m_bloomChain.size()) - 1 - level), true);
    }
    return m_bloomViews[0];
}

} // namespace devex::render::webgpu
